/* Implement of TO_NUMBER()
 *
 */

#include "m_string.h"
#include "my_dbug.h"
#include "my_macros.h"
#include "my_sys.h"
#include "mysqld_error.h"  // ER_...
#include "sql_string.h"

#include "sql/oracle_compatibility/convert_number.h"
#include "sql/oracle_compatibility/ora_format.h"
#include "sql/sql_class.h"

/* ----------
 * Flags for NUMBER version
 * ----------
 */
#define NUM_F_DECIMAL (1 << 1)
#define NUM_F_LDECIMAL (1 << 2)
#define NUM_F_DIGIT_0 (1 << 3)
#define NUM_F_DIGIT_9 (1 << 4)
#define NUM_F_BLANK (1 << 5)
#define NUM_F_LSIGN (1 << 6)
#define NUM_F_BRACKET (1 << 7)
#define NUM_F_MINUS (1 << 8)
#define NUM_F_PLUS (1 << 9)
#define NUM_F_ROMAN (1 << 10)
#define NUM_F_MULTI (1 << 11)
#define NUM_F_PLUS_POST (1 << 12)
#define NUM_F_MINUS_POST (1 << 13)
#define NUM_F_EEEE (1 << 14)
#define NUM_F_GROUP (1 << 15)
#define NUM_F_LGROUP (1 << 16)
#define NUM_F_LCURRENCY (1 << 17)
#define NUM_F_X (1 << 18)
#define NUM_F_DOLLAR (1 << 19)
#define NUM_F_CCURRENCY (1 << 20)
#define NUM_F_UCURRENCY (1 << 21)
#define NUM_F_BEGIN_S (1 << 22)
#define NUM_F_END_S (1 << 23)
#define NUM_F_CURRENCY_MID (1 << 24)

#define NUM_LSIGN_NONE 0

/* ----------
 * Tests
 * ----------
 */
#define IS_DECIMAL(_f) ((_f)->flag & NUM_F_DECIMAL)
#define IS_LDECIMAL(_f) ((_f)->flag & NUM_F_LDECIMAL)
#define IS_DIGIT_0(_f) ((_f)->flag & NUM_F_DIGIT_0)
#define IS_DIGIT_9(_f) ((_f)->flag & NUM_F_DIGIT_9)
#define IS_BLANK(_f) ((_f)->flag & NUM_F_BLANK)
#define IS_BRACKET(_f) ((_f)->flag & NUM_F_BRACKET)
#define IS_MINUS(_f) ((_f)->flag & NUM_F_MINUS)
#define IS_LSIGN(_f) ((_f)->flag & NUM_F_LSIGN)
#define IS_PLUS(_f) ((_f)->flag & NUM_F_PLUS)
#define IS_ROMAN(_f) ((_f)->flag & NUM_F_ROMAN)
#define IS_MULTI(_f) ((_f)->flag & NUM_F_MULTI)
#define IS_EEEE(_f) ((_f)->flag & NUM_F_EEEE)
#define IS_GROUP(_f) ((_f)->flag & NUM_F_GROUP)
#define IS_LGROUP(_f) ((_f)->flag & NUM_F_LGROUP)
#define IS_LCURRENCY(_f) ((_f)->flag & NUM_F_LCURRENCY)
#define IS_CCURRENCY(_f) ((_f)->flag & NUM_F_CCURRENCY)
#define IS_UCURRENCY(_f) ((_f)->flag & NUM_F_UCURRENCY)
#define IS_X(_f) ((_f)->flag & NUM_F_X)
#define IS_DOLLAR(_f) ((_f)->flag & NUM_F_DOLLAR)
#define IS_BEGIN_S(_f) ((_f)->flag & NUM_F_BEGIN_S)
#define IS_END_S(_f) ((_f)->flag & NUM_F_END_S)
#define IS_SSIGN(_f) ((_f)->flag & (NUM_F_BEGIN_S | NUM_F_END_S))
#define IS_CURRENCY(_f) \
  ((_f)->flag &         \
   (NUM_F_DOLLAR | NUM_F_LCURRENCY | NUM_F_CCURRENCY | NUM_F_UCURRENCY))
#define IS_DIGIT(_f) (IS_DIGIT_0(_f) || IS_DIGIT_9(_f))
#define IS_G(_f) (IS_GROUP(_f) && IS_LGROUP(_f))
#define IS_COMMA(_f) (IS_GROUP(_f) && !IS_LGROUP(_f))
#define IS_CURRENCY_MID(_f) ((_f)->flag & NUM_F_CURRENCY_MID)
#define IS_X_NOT_COMPATIBLE(_f) ((_f)->flag & (~(NUM_F_X)))
#define IS_EEEE_NOT_COMPATIBLE(_f) \
  (((_f)->flag & (NUM_F_EEEE | NUM_F_X)) || IS_COMMA(_f))

/*
 * These macros are used in NUM_processor() and its subsidiary routines.
 * OVERLOAD_TEST: true if we've reached end of input string
 * AMOUNT_TEST(s): true if at least s bytes remain in string
 */
#define OVERLOAD_TEST (Np->inout_p >= Np->inout + input_len)
#define AMOUNT_TEST(s) (Np->inout_p <= Np->inout + (input_len - (s)))

#define initialize_NUM(_n)   \
  do {                       \
    (_n)->flag = 0;          \
    (_n)->cur_pos = 0;       \
    (_n)->x_len = 0;         \
    (_n)->cnt_G = 0;         \
    (_n)->isbegin = 0;       \
    (_n)->lsign = 0;         \
    (_n)->pre = 0;           \
    (_n)->post = 0;          \
    (_n)->pre_lsign_num = 0; \
    (_n)->need_locale = 0;   \
    (_n)->multi = 0;         \
    (_n)->fmt_len = 0;       \
  } while (0)

/* ----------
 * Number description struct
 * ----------
 */
typedef struct {
  int pre;     /* (count) numbers before decimal */
  int post;    /* (count) numbers after decimal  */
  int x_len;   /* or character in 'X' fmt(including 0' and 'X') */
  int cnt_G;   /* (count) numbers of group separator,including 'G' or ',' */
  int cur_pos; /* the position of the procesing character in the fmt */
  int lsign;   /* want locales sign          */
  int flag;    /* number parameters          */
  int isbegin; /* if is begin to match the value */
  int pre_lsign_num; /* tmp value for lsign          */
  int multi;         /* multiplier for 'V'          */
  int need_locale;   /* needs it locale          */
  int fmt_len;       /* the length of fmt string */
} NUMDesc;

/* ----------
 * Number processor struct
 * ----------
 */
typedef struct NUMProc {
  char is_to_char; /* 0 means to_number, others means to_char */
  NUMDesc *Num;    /* number description        */

  int sign;           /* '-' or '+'            */
  int sign_wrote;     /* was sign write        */
  int num_count;      /* number of write digits    */
  int num_in;         /* is inside number        */
  int num_curr;       /* current position in number    */
  int out_pre_spaces; /* spaces before first digit    */

  int read_post;  /* to_number - number of dec. digit */
  int read_pre;   /* to_number - number non-dec. digit */
  int xmatch_len; /* the number of characters that participate match with 'X'
                     fmt in input string */

  NumBaseType in_type; /* input number base type */

  char *number;           /* string with number    */
  char *number_p;         /* pointer to current number position */
  const char *number_end; /* end position of number */
  const char *inout;      /* in / out buffer    */
  const char *inout_p;    /* pointer to current inout position */
  const char *inout_end;  /* end position of inout */
  const char *exp_end; /* pointer to end position of exponent in value string*/

  const char *L_negative_sign; /* Locale */
  const char *L_positive_sign;
  const char *decimal;
  const char *L_thousands_sep;

  const CurrencyType *Local_currency;
  const CurrencyType *ISO_currency;
  const CurrencyType *Dual_currency;

  bool read_dec;       /* to_number - if has read dec. point    */
  bool is_single_sign; /* if the value string is just one sign symbol, like '+'
                          or '-'. Default:false */
  bool is_valid_exp;   /* check if it is valid exponent in value string. Default
                         true */

} NUMProc;

/**
  Enums the type of a character
*/
enum class enum_value_type {
  V_DIGIT,  // digit
  V_DECI,   // decimal point
  V_SIGN,   // sign symbol
  V_SPACE,  // space
  V_CURR,   // currency symbol
  V_E,      // character 'E' or 'e', used for scientific notation

  _V_LAST
};

/*
 * Prepare the num_9
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_9(NUMDesc *num) {
  if (IS_X(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }

  if (IS_EEEE(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"EEEE\" format.");
    return true;
  }

  if (IS_DECIMAL(num))
    ++num->post;
  else
    ++num->pre;

  num->flag |= NUM_F_DIGIT_9;
  return false;
}

/*
 * Prepare the num_0
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_0(NUMDesc *num) {
  if (IS_X(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }

  if (IS_EEEE(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"EEEE\" format.");
    return true;
  }

  if (IS_DECIMAL(num))
    ++num->post;
  else
    ++num->pre;

  num->flag |= NUM_F_DIGIT_0;
  return false;
}

/* To check if the fmt start with group separactor:
    'G' (start 'G') or ',' (start ',').
  Example:
    Here we define start 'G' in following situations:
        1、'G' is the first in a fmt.
        2. if the first position is occupied by start 'S' or currency character
   like 'L'/'C'/'U''$','G' must be the second in the fmt.
*/
static inline bool is_start_separactor(NUMDesc *num) {
  DBUG_ASSERT(NULL != num);
  if (0 == num->cur_pos) return true;
  if (1 == num->cur_pos && (IS_BEGIN_S(num) || IS_CURRENCY(num))) return true;
  return false;
}

/*
 * Prepare the num_comma
 * NUM_F_GROUP:NUM_F_LGROUP=1:0
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_comma(NUMDesc *num) {
  // start ',' is not valid in a fmt
  if (is_start_separactor(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\",\" cannot at be first or second with some particular "
             "character in fmt.");
    return true;
  }

  if (IS_DECIMAL(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\",\" must be ahead of decimal point.");
    return true;
  }

  if (IS_LGROUP(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\",\" should not use with \"G\".");
    return true;
  }

  if (IS_CURRENCY_MID(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\",\" should not use with middle \"L\",\"C\",\"U\".");
    return true;
  }
  ++num->cnt_G;
  num->flag |= NUM_F_GROUP;
  return false;
}

/**
  prepare_to_num_E:check if 'E'-fmt is valid
   @param num Pointer to the NUMDesc struct
   @returns true if error ,false is success
*/
static bool prepare_to_num_E(NUMDesc *num) {
  if (IS_EEEE_NOT_COMPATIBLE(num) || 0 == num->pre) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"EEEE\" format.");
    return true;
  }

  num->flag |= NUM_F_EEEE;
  return false;
}

/*
 * Prepare the num_dollar
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_dollar(NUMDesc *num) {
  if (IS_CURRENCY(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "repeated currency character in fmt.");
    return true;
  }

  if (IS_X(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }

  if (IS_EEEE(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"EEEE\" format.");
    return true;
  }

  num->flag |= NUM_F_DOLLAR;
  return false;
}

/*
 * Prepare the num_G
 * NUM_F_GROUP:NUM_F_LGROUP=1:1
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_G(NUMDesc *num) {
  // start 'G' is not valid in a fmt
  if (is_start_separactor(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"G\" cannot at be first or second with some particular "
             "character in fmt.");
    return true;
  }

  if (IS_DECIMAL(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"G\" must be ahead of decimal point.");
    return true;
  }

  if (IS_GROUP(num) && !IS_LGROUP(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"G\" should not use with \",\".");
    return true;
  }
  if (IS_CURRENCY_MID(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"G\" should not behind middle \"L\",\"C\",\"U\".");
    return true;
  }
  ++num->cnt_G;
  num->flag |= NUM_F_GROUP;
  num->flag |= NUM_F_LGROUP;
  num->need_locale = true;
  return false;
}

/*
 * Prepare the num_D
 * NUM_F_DECIMAL:NUM_F_LDECIMAL=1:1
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_D(NUMDesc *num) {
  if (IS_DECIMAL(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "repeated decimal character in fmt.");
    return true;
  }
  if (IS_GROUP(num) && !IS_LGROUP(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"D\" should not use with \",\"");
    return true;
  }
  if (IS_CURRENCY_MID(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"D\" should not use with middle currency character in fmt.");
    return true;
  }

  num->flag |= NUM_F_LDECIMAL;
  num->flag |= NUM_F_DECIMAL;
  num->need_locale = true;
  return false;
}

/*
 * Prepare the num_DEC
 * NUM_F_DECIMAL:NUM_F_LDECIMAL=1:0
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_dec(NUMDesc *num) {
  if (IS_DECIMAL(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "repeated decimal character in fmt.");
    return true;
  }
  if (IS_LGROUP(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\".\" should not use with \"G\" in fmt");
    return true;
  }
  if (IS_CURRENCY_MID(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\".\" should not use with middle currency character in fmt.");
    return true;
  }

  num->flag |= NUM_F_DECIMAL;
  return false;
}

static bool inline is_repeated_sign(NUMDesc *num) {
  if (IS_SSIGN(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "\"S\" repeated in fmt.");
    return true;
  }
  if (IS_MINUS(num) || IS_BRACKET(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"S\" can't be use with \"MI\" or \"PR\" in fmt.");
    return true;
  }
  return false;
}

/*
 * Prepare the num_S
 * begin S：NUM_LSIGN:NUM_LSIGN_POST=1：0
 * end S: NUM_LSIGN:NUM_LSIGN_POST=0：1
 * both：NUM_LSIGN:NUM_LSIGN_POST=1：1
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_S(NUMDesc *num) {
  if (0 != num->cur_pos && num->fmt_len != num->cur_pos + 1) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"S\" should only in the first or last position in fmt.");
    return true;
  }

  if (is_repeated_sign(num)) return true;

  if (IS_X(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }
  if (num->fmt_len == num->cur_pos + 1) {
    num->flag |= NUM_F_END_S;
  }
  if (0 == num->cur_pos) {
    num->flag |= NUM_F_BEGIN_S;
  }

  return false;
}

/*
 * Prepare the num_MI
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_MI(NUMDesc *num) {
  if (num->fmt_len != num->cur_pos + 2) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"MI\" should only in the last position in fmt.");
    return true;
  }
  if (IS_MINUS(num) || IS_SSIGN(num) || IS_BRACKET(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"MI\" repeated or be used with \"S\" or \"PR\" in fmt.");
    return true;
  }
  if (IS_X(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }

  num->flag |= NUM_F_MINUS;
  return false;
}

/*
 * Prepare the num_PR
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_PR(NUMDesc *num) {
  if (num->fmt_len != num->cur_pos + 2) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"PR\" should only in the last position in fmt.");
    return true;
  }
  if (IS_BRACKET(num) || IS_SSIGN(num) || IS_MINUS(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "\"PR\" repeated or be used with \"S\" or \"MI\" in fmt.");
    return true;
  }
  if (IS_X(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }

  num->flag |= NUM_F_BRACKET;
  return false;
}

inline bool is_middle_currency(NUMDesc *num) {
  if (0 != num->cur_pos && num->fmt_len != num->cur_pos + 1) {
    // select to_number('-$1','SL9') from dual;
    if (IS_BEGIN_S(num) && 1 == num->cur_pos) {
      return false;
    }
    return true;
  }
  return false;
}

static inline bool check_middle_currency(NUMDesc *num) {
  if (is_middle_currency(num)) {
    if (IS_GROUP(num) && !IS_LGROUP(num)) {
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
               "middle \"L/C/U\" can't use with \",\" in fmt.");
      return true;
    }

    if (IS_DECIMAL(num)) {
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
               "middle \"L/C/U\" can't use with decimal character in fmt.");
      return true;
    }

    num->flag |= NUM_F_CURRENCY_MID;
    num->flag |= NUM_F_DECIMAL;
    num->flag |= NUM_F_LDECIMAL;
  }
  return false;
}

/*
 * Prepare the num_L/C/U
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_LCU(NUMDesc *num, int id) {
  if (IS_CURRENCY(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "repeated currency character in fmt.");
    return true;
  }

  if (IS_X(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }

  if (IS_EEEE(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"EEEE\" format.");
    return true;
  }

  if (check_middle_currency(num)) return true;

  switch (id) {
    case NUM_L: {
      num->flag |= NUM_F_LCURRENCY;
      break;
    }
    case NUM_C: {
      num->flag |= NUM_F_CCURRENCY;
      break;
    }
    case NUM_U: {
      num->flag |= NUM_F_UCURRENCY;
      break;
    }
    default:
      break;
  }

  num->need_locale = true;
  return false;
}

/*
 * Prepare the num_X
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_X(NUMDesc *num) {
  if (IS_X_NOT_COMPATIBLE(num)) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "invalid \"X\" format.");
    return true;
  }
  ++num->x_len;
  num->flag |= NUM_F_X;
  return false;
}

/*
 * Prepare the to_number not support formats
 * return: true  -- error
 *         false -- ok
 */
static bool prepare_to_num_others(int id) {
  switch (id) {
    case NUM_B:
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "not support \"B\"");
      return true;

    case NUM_FM:
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "not support \"FM\"");
      return true;

    case NUM_V:
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "not support \"V\"");
      return true;

    case NUM_RN:
    case NUM_rn:
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "not support \"RN\"/\"rn\"");
      return true;

    default:
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "not support other formats");
      return true;
  }
}

/* ----------
 * Prepare NUMDesc (number description struct) via FormatNode struct
 * ----------
 * Only for to_number function.
 * return: true  -- error
 *         false -- ok
 */
static bool NUMDesc_prepare_for_to_num(NUMDesc *num, FormatNode *n) {
  bool res = false;
  switch (n->key->id) {
    case NUM_9:
      res = prepare_to_num_9(num);
      break;

    case NUM_0:
      res = prepare_to_num_0(num);
      break;

    case NUM_COMMA:
      res = prepare_to_num_comma(num);
      break;

    case NUM_E:
      res = prepare_to_num_E(num);
      break;

    case NUM_G:
      res = prepare_to_num_G(num);
      break;

    case NUM_D:
      res = prepare_to_num_D(num);
      break;

    case NUM_DEC:
      res = prepare_to_num_dec(num);
      break;

    case NUM_DOLLAR:
      res = prepare_to_num_dollar(num);
      break;

    case NUM_S:
      res = prepare_to_num_S(num);
      break;

    case NUM_MI:
      res = prepare_to_num_MI(num);
      break;
    case NUM_PR:
      res = prepare_to_num_PR(num);
      break;

    case NUM_L:
    case NUM_C:
    case NUM_U:
      res = prepare_to_num_LCU(num, n->key->id);
      break;

    case NUM_X:
      res = prepare_to_num_X(num);
      break;

    default:
      res = prepare_to_num_others(n->key->id);
      break;
  }

  return res;
}

/* ----------
 * Format parser, search small keywords and make format-node tree.
 * ----------
 * return: true  -- error
 *         false -- ok
 */
static bool parse_format_for_to_num(FormatNode *node, const char *str,
                                    const KeyWord *kw, const int *index,
                                    NUMDesc *Num) {
  FormatNode *n = node;
  const char *format = str;

  while (*str) {
    if ((n->key = index_seq_search(str, kw, index)) != NULL) {
      n->type = FormatNodeType::ACTION;
      n->suffix = 0;
      if (n->key->len) str += n->key->len;

      if (NUMDesc_prepare_for_to_num(Num, n)) {
        return true;
      }
      Num->cur_pos += n->key->len;
      n++;
    } else {
      my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), format);
      return true;
    }
  }

  Num->cur_pos = 0;  // reset cur_pos in fmt for matching value later
  n->type = FormatNodeType::END;
  n->suffix = 0;

  return false;
}

/* ----------
 * Locale
 * ----------
 */
static inline void TO_NUM_prepare_locale(NUMProc *Np) {
  Np->L_negative_sign = "-";
  Np->L_positive_sign = "+";
  Np->decimal = ".";
  Np->L_thousands_sep = ",";
  Np->Local_currency = &Currency_symbol[0];
  Np->ISO_currency = &Currency_symbol[1];
  Np->Dual_currency = &Currency_symbol[0];
  return;
}

/*
 * This function is used to judge if the character is the last
 * in the fmt except end_S.
 */
static bool is_end_fmt(NUMProc *Np) {
  int len = 0;
  if (IS_EEEE(Np->Num)) len = 4;
  if (Np->Num->cur_pos == Np->Num->fmt_len - len - 1) {
    return true;
  } else if (Np->Num->cur_pos == Np->Num->fmt_len - len - 2) {
    if (IS_END_S(Np->Num)) return true;
  }

  return false;
}

/* skip consecutive ',' in value string */
static inline void skip_consecutive_comma(NUMProc *Np) {
  while (Np->inout_p < Np->inout_end && *Np->L_thousands_sep == *Np->inout_p)
    ++Np->inout_p;
}

/* skip '.' in value string */
static inline void skip_decimal(NUMProc *Np) {
  DBUG_ASSERT(Np->inout_p <= Np->inout_end);
  if (*Np->decimal == *Np->inout_p) ++Np->inout_p;
}

/* skip ',' and '.' invalue matching with fmt between 'G' and 'D'
or ',' and '.'*/
static inline void skip_specified_comma_and_decimal(NUMProc *Np) {
  if (0 == Np->Num->cnt_G) skip_consecutive_comma(Np);

  if (!IS_DECIMAL(Np->Num)) skip_decimal(Np);
}

/* process the integer part of digit */
static inline bool process_integer_part_for_digit(NUMProc *Np) {
  /*
  Need to skip consecutive commma in integer part of value when there is no
  group separator ('G' or ',') in the left unmatched part of the fmt.
  for example:
        select to_number('12,,,3.4','99G9D9') from dual; --- ok
  we need to skip the second and third ',' in the value when we macth the '9'
  after 'G' in fmt */
  if (0 == Np->Num->cnt_G) {
    skip_consecutive_comma(Np);
  }
  if (!isdigit(*Np->inout_p)) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "number not match the format!");
    return true;
  }
  DBUG_ASSERT(Np->read_pre > 0);
  DBUG_ASSERT(Np->Num->pre > 0);

  DBUG_ASSERT(Np->number_p < Np->number_end);
  *Np->number_p = *Np->inout_p;
  ++Np->number_p;
  ++Np->inout_p;
  --Np->Num->pre;
  --Np->read_pre;
  /* If there is no more integer need to be matched in value string and we reach
  end_fmt, we need to skip the extra comma(',') and one decimal point('.').
  eg:
    select to_number('1,,','9') from dual; --- ok
    select to_number('1,,+','9S') from dual; --- ok
  */
  if (0 == Np->read_pre) {
    if (is_end_fmt(Np)) skip_specified_comma_and_decimal(Np);
  }
  return false;
}

/* process the decimal part of digit */
static inline bool process_decimal_part_for_digit(NUMProc *Np) {
  DBUG_ASSERT(Np->read_pre == 0);
  DBUG_ASSERT(Np->Num->pre == 0);

  if (Np->read_post > Np->Num->post) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "number not match the format!");
    return true;
  }

  if (0 == Np->read_post) return false;

  if (!isdigit(*Np->inout_p)) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "number not match the format!");
    return true;
  }

  DBUG_ASSERT(Np->number_p < Np->number_end);
  *Np->number_p = *Np->inout_p;
  ++Np->number_p;
  ++Np->inout_p;
  --Np->Num->post;
  --Np->read_post;
  return false;
}

static bool process_digit_for_E(NUMProc *Np) {
  DBUG_ASSERT(NULL != Np);
  if (Np->read_pre > 1) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
             "invalid value match with \"EEEE\".");
    return true;
  }

  if (Np->Num->pre > 1) {
    if (Np->Num->cnt_G > 0) {
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
               "invalid value match with \"EEEE\".");
      return true;
    }
    if (Np->is_single_sign) {
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
               "invalid value match with \"EEEE\".");
      return true;
    }
  }

  if (Np->read_pre < Np->Num->pre) {
    --Np->Num->pre;
    return false;
  }

  Np->Num->isbegin = true;
  if (Np->read_dec)
    return process_decimal_part_for_digit(Np);
  else
    return process_integer_part_for_digit(Np);
}

/*
 * process the to_num_0 format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_0(NUMProc *Np) {
  if (IS_EEEE(Np->Num)) return process_digit_for_E(Np);

  Np->Num->isbegin = true;
  if (Np->read_pre < Np->Num->pre) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "number not match the format!");
    return true;
  }

  if (Np->inout_p == Np->inout_end) {
    DBUG_ASSERT(0 == Np->read_pre && 0 == Np->read_post);
    DBUG_ASSERT(0 == Np->Num->pre);
    return false;
  }

  if (Np->read_dec)
    return process_decimal_part_for_digit(Np);
  else
    return process_integer_part_for_digit(Np);
}
/*
 * process the to_num_9 format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_9(NUMProc *Np) {
  if (IS_EEEE(Np->Num)) return process_digit_for_E(Np);

  if (Np->read_pre < Np->Num->pre) {
    if (Np->is_single_sign) {
      // select to_number('-','S999') as result from dual; --- error num
      // select to_number('-.1','S9.9') from dual; --- ok
      // select to_number('+,,.','S99') from dual; --- ok
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "number not match the format!");
      return true;
    }
    --Np->Num->pre;
    return false;
  }

  if (Np->inout_p == Np->inout_end) {
    DBUG_ASSERT(0 == Np->read_pre);
    DBUG_ASSERT(0 == Np->read_post);
    DBUG_ASSERT(0 == Np->Num->pre);
    return false;
  }

  Np->Num->isbegin = true;

  if (Np->read_dec)
    return process_decimal_part_for_digit(Np);
  else
    return process_integer_part_for_digit(Np);
}

/*
 * process the format('.' or ‘D’)
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_decimal_point(NUMProc *Np) {
  // DBUG_ASSERT(0 == Np->read_pre);
  DBUG_ASSERT(0 == Np->Num->pre);

  /*
  skip consecutive commma before decimal point,
    for example: select to_number('123,,,.4','999.9') from dual;
    we need to skip three consecutive ',' in value when match the
    '.' in fmt.
  */
  skip_consecutive_comma(Np);

  if (0 == Np->read_post) {
    // select to_number('-','.') from dual; --- error num
    if (Np->is_single_sign) {
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error number.");
      return true;
    }
    // select to_number('-1.','9.') from dual;
    // select to_number('-1','9.') from dual;
    skip_decimal(Np);
    return false;
  }
  if (*Np->decimal != *Np->inout_p) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
             "not match \"D\" or \".\" in the format!");
    return true;
  }

  Np->read_dec = true;
  DBUG_ASSERT(Np->number_p < Np->number_end);
  *Np->number_p = *Np->inout_p;
  ++Np->number_p;
  ++Np->inout_p;
  return false;
}

/*
 * process the to_num_dollar format('$')
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_dollar(NUMProc *Np) {
  int x = 0;
  if (' ' != *Np->number) {
    ++x;
  }
  DBUG_ASSERT(Np->inout + x <= Np->inout_end);
  if ('$' != *(Np->inout + x)) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "\"$\" not matched.");
    return true;
  }
  if (Np->inout + x == Np->inout_end - 1) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
             "\"$\" can't be the last in value");
    return true;
  }
  return false;
}

/*
 * process the to_num_group_separator format('G' or ',')
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_group_separator(NUMProc *Np) {
  --Np->Num->cnt_G;
  if (!Np->Num->isbegin) {
    return false;
  }

  /* If the fmt has 'G' and mid-currency, the extra 'G's(not include 'G' before
  isbegin that when isbegin is false.) dont need to match ',' in the value
  string.
    eg:
      select to_number('2233$8','9ggGGggggg0900U9') "Amount" from dual;--- ok
  However if the currency is not in the middle, we can do this.
    eg:
      select to_number('2233USD','9ggg090C') "Amount" from dual; --- error num
  Explanation for
    'select to_number('2233USD','9ggGGggggg0900C') "Amount" from dual;--- ok'
  The extra 'G's are skipped because of there are before isbegin(isbegin now is
  false)
  */

  DBUG_ASSERT(Np->inout_p <= Np->inout_end);
  if (*Np->L_thousands_sep != *Np->inout_p) {
    if (IS_G(Np->Num) && IS_CURRENCY_MID(Np->Num)) {
      return false;
    }
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
             "not match \"G\" or \",\" in fmt");
    return true;
  }
  ++Np->inout_p;
  /* When there is no more 'G' or ',' need to match in fmt and the current 'G'
   * or ',' is the end_fmt, we need to skip the extra ',' in value string here.
   * eg:
   *    select to_number('1,1,,','9G9G') from dual;
   *    select to_number('1,1,,,,+','9,9,S') from dual;
   */
  if (0 == Np->Num->cnt_G) {
    if (is_end_fmt(Np)) skip_consecutive_comma(Np);
  }
  return false;
}

/*
 * process the to_num_L/C/U format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_LCU(NUMProc *Np, int id) {
  skip_consecutive_comma(Np);

  const CurrencyType *c_symbol = &Currency_symbol[0];
  switch (id) {
    case NUM_L:
      c_symbol = Np->Local_currency;
      break;
    case NUM_C:
      c_symbol = Np->ISO_currency;
      break;
    case NUM_U:
      c_symbol = Np->Dual_currency;
      break;
    default:
      DBUG_ASSERT(0);
      break;
  }

  if (Np->inout_p <= Np->inout_end - c_symbol->len &&
      strncmp(Np->inout_p, c_symbol->symbol, c_symbol->len) == 0) {
    Np->inout_p += c_symbol->len;
    if (IS_CURRENCY_MID(Np->Num)) {
      DBUG_ASSERT(Np->number_p < Np->number_end);
      *Np->number_p++ = *Np->decimal;
      Np->read_dec = true;
    }
  } else {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
             "error \"L\",\"C\" or \"U\",value");
    return true;
  }
  return false;
}

/*
 * preprocess the to_num_X format:
 * calculate the number of characters in value string that used to
 * matching the 'X' format. The comma and leading zeros dont participate
 * in matching.
 */
static void preprocess_to_num_X(NUMProc *Np, int input_len) {
  if (!IS_X(Np->Num)) return;

  Np->xmatch_len = input_len;
  Np->in_type = NumBaseType::HEX;
  const char *p = Np->inout;

  for (; p < Np->inout_end; ++p)
    if (*p == ',') --Np->xmatch_len;

  return;
}

static inline void preprocess_to_num_single_sign(NUMProc *Np) {
  if (Np->inout_end - Np->inout == 1) {
    if (*Np->L_positive_sign == *Np->inout ||
        *Np->L_negative_sign == *Np->inout)
      Np->is_single_sign = true;
  }
}

/*
 * process the to_num_X1 format
 *  when matching lenth not larger than 16
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_X_1(NUMProc *Np, ulonglong &sum) {
  ulonglong tmp = 0;
  if ('0' <= *Np->inout_p && *Np->inout_p <= '9') {
    tmp = *Np->inout_p - '0';
  } else if ('A' <= *Np->inout_p && *Np->inout_p <= 'F') {
    tmp = *Np->inout_p - 'A' + 10;
  } else if ('a' <= *Np->inout_p && *Np->inout_p <= 'f') {
    tmp = *Np->inout_p - 'a' + 10;
  } else {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"X\" value");
    return true;
  }

  sum = (sum << 4) + tmp;
  ++Np->inout_p;
  return false;
}

/*
 * process the to_num_X1 format
 *  when matching lenth larger than 16
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_X_2(NUMProc *Np, my_decimal &dec_sum) {
  uint mask = 0x1F;
  bool unsigned_flag = true;
  my_decimal dec_tmp;
  my_decimal mul_tmp;
  my_decimal weight;
  int2my_decimal(mask, 16, unsigned_flag, &weight);

  if ('0' <= *Np->inout_p && *Np->inout_p <= '9') {
    int2my_decimal(mask, *Np->inout_p - '0', unsigned_flag, &dec_tmp);
  } else if ('A' <= *Np->inout_p && *Np->inout_p <= 'F') {
    int2my_decimal(mask, *Np->inout_p - 'A' + 10, unsigned_flag, &dec_tmp);
  } else if ('a' <= *Np->inout_p && *Np->inout_p <= 'f') {
    int2my_decimal(mask, *Np->inout_p - 'a' + 10, unsigned_flag, &dec_tmp);
  } else {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"X\" value");
    return true;
  }
  my_decimal_mul(mask, &mul_tmp, &dec_sum, &weight);
  my_decimal_add(mask, &dec_sum, &mul_tmp, &dec_tmp);
  Np->inout_p++;
  return false;
}

/*
 * process the to_num_X format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_X(NUMProc *Np, ulonglong &ullsum,
                             my_decimal &dec_sum) {
  // Now we dont support negative value in matching 'X' fmt
  if ('-' == *Np->inout) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"X\" negative value");
    return true;
  }
  // skip extra 'X'.
  if (Np->xmatch_len < Np->Num->x_len) {
    --Np->Num->x_len;
    return false;
  }
  // return if reach the end of value string
  if (Np->inout_end == Np->inout_p) return false;

  skip_consecutive_comma(Np);
  /* if xmatch_len <= 16, we can use ulonglong type variable to store the
   temporary sum and take part in calculating which would have higher
   efficiency. Otherwise we need decimal type variable to store the temporary
   sum. */
  if (Np->xmatch_len <= 16) {
    return process_to_num_X_1(Np, ullsum);
  } else {
    return process_to_num_X_2(Np, dec_sum);
  }
}

/**
  process_to_num_E:matching method for 'EEEE'-fmt
  Here we only need to check if 'is_valid_exp' is true.
  If 'is_valid_exp' is true, it means the exponent in the value string is valid.
  Then we only need to transfer the original character to number array and later
  the method in my_decimal class can automatically calculate the value of the
  scientific string.

  @param Np   Pointer to the NUMProc struct
  @returns true if error ,false is success
*/
static bool process_to_num_E(NUMProc *Np) {
  if (!Np->is_valid_exp) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
             "error \"EEEE\": exponent is not valid in value string.");
    return true;
  }

  while (Np->inout_p < Np->exp_end) *Np->number_p++ = *Np->inout_p++;

  return false;
}

/* process special situation for 'S'*/
static inline bool process_for_sepcial_S(NUMProc *Np) {
  /* fmt is begin 'S' */
  if (IS_BEGIN_S(Np->Num)) {
    if (*Np->number == ' ') {
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"S\" value");
      return true;
    }
    // select to_number('-','S') as result from dual; --- error num
    // select to_number('-,,.','S') as result from dual; --- ok
    /* fmt is begin 'S' and end 'S' */
    else if (IS_END_S(Np->Num)) {
      if (!Np->is_single_sign) {
        skip_specified_comma_and_decimal(Np);
        return false;
      }
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error number value.");
      return true;
    }
  }
  return false;
}

/*
 * process the to_num_S format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_S(NUMProc *Np) {
  if (process_for_sepcial_S(Np)) return true;

  // begin_S has been processed at process_for_sepcial_S
  if (IS_BEGIN_S(Np->Num)) {
    return false;
  }

  if (Np->inout_p == Np->inout_end) return false;

  if (*Np->L_positive_sign == *Np->inout_p ||
      *Np->L_negative_sign == *Np->inout_p) {
    *Np->number = *Np->inout_p;
    ++Np->inout_p;
  } else {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"S\" value");
    return true;
  }
  return false;
}

/*
 * process the to_num_MI format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_MI(NUMProc *Np) {
  DBUG_ASSERT(*Np->number == ' ');

  // select to_number(' ','MI') from dual --- error num
  if (' ' == *Np->inout_p && Np->inout == Np->inout_end - 1) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"MI\" value");
    return true;
  }

  // select to_number(',. ','MI') as result from dual; --- ok
  skip_specified_comma_and_decimal(Np);
  DBUG_ASSERT(Np->inout_p <= Np->inout_end);
  if (*Np->inout_p == '-') {
    *Np->number = '-';
    ++Np->inout_p;
  } else if (' ' == *Np->inout_p || '+' == *Np->inout_p) {
    *Np->number = '+';
    ++Np->inout_p;
  } else {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"MI\" value");
    return true;
  }
  return false;
}

/*
 * process the to_num_PR format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_PR(NUMProc *Np) {
  // select to_number(',.','PR') as result from dual; --- ok
  // select to_number('<,.>','PR') as result from dual; --- ok
  // select to_number(',.+','PR') as result from dual; --- error num
  // select to_number(',. ','PR') as result from dual; --- error num
  skip_specified_comma_and_decimal(Np);

  DBUG_ASSERT(Np->inout_p <= Np->inout_end);
  if (*Np->number == '-' && *Np->inout_p == '>')
    Np->inout_p++;
  else if (*Np->number == ' ' && Np->inout_p == Np->inout_end) {
    return false;
  } else {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error \"PR\" value");
    return true;
  }
  return false;
}

/**
 *  Read begin sign before number
 *  return: true  -- error
 *          false -- ok
 **/
static void preprocess_to_num_begin_sign(NUMProc *Np) {
  if (IS_BRACKET(Np->Num)) {
    // simple < >
    if (*Np->inout_p == '<') {
      *Np->number = '-';
      Np->inout_p++;
    }
  } else if (IS_BEGIN_S(Np->Num)) {
    if ('+' == *Np->inout_p || '-' == *Np->inout_p) {
      *Np->number = *Np->inout_p;
      Np->inout_p++;
    }
  } else if (!IS_MINUS(Np->Num)) {
    // simple + -
    if (*Np->inout_p == '-') {
      *Np->number = '-';
      Np->inout_p++;
    }
  }

  return;
}

/**
 *  Read currency sign before process number
 *  return: true  -- error
 *          false -- ok
 **/
static inline void preprocess_to_num_currency(NUMProc *Np) {
  if (Np->inout_p == Np->inout_end) return;
  // select to_number('+$0','S9$') as result from dual; --- ok
  if ('$' == *Np->inout_p && IS_DOLLAR(Np->Num)) {
    ++Np->inout_p;
    return;
  }
}

/**
  is_value_currency: check if p starts with a currency symbol

  @param    p  Pointer to the position to check
  @returns  true    if p starts with a currency symbol
            false   if not
*/
static bool is_value_currency(const char *p) {
  DBUG_ASSERT(NULL != p);
  for (int i = 0; NULL != Currency_symbol[i].symbol; ++i) {
    if (!native_strncasecmp(p, Currency_symbol[i].symbol,
                            Currency_symbol[i].len)) {
      return true;
    }
  }
  return false;
}

/**
  is_sign: check if the character is a sign symbol

  @param    p  Pointer to a character
  @returns  true  if it is a sign symbol
            false  if not
*/
static inline bool is_sign(const char *p) {
  DBUG_ASSERT(NULL != p);

  if ('+' == *p || '-' == *p) return true;
  return false;
}

/**
  get_value_type: return the type of a character

  @param    p  Pointer to a character
  @returns  the type of a character
*/
static enum_value_type get_value_type(const char *p) {
  DBUG_ASSERT(NULL != p);

  if (isdigit(*p)) return enum_value_type::V_DIGIT;
  if (is_sign(p)) return enum_value_type::V_SIGN;
  if ('.' == *p) return enum_value_type::V_DECI;
  if (' ' == *p) return enum_value_type::V_SPACE;
  if (is_value_currency(p)) return enum_value_type::V_CURR;
  if (!native_strncasecmp(p, "E", 1)) return enum_value_type::V_E;

  return enum_value_type::_V_LAST;
}

/**
  check_valid_exponent: check if the string is a valid exponent

  @param    p         Pointer to start position of a string
  @param    p_end     Pointer to terminal null position of a string
  @returns  true      if it is valid exponent string
            false     if it is not a valid exponenet string
*/
static bool check_valid_exponent(const char *p, const char *p_end) {
  DBUG_ASSERT(NULL != p);
  DBUG_ASSERT(NULL != p_end);
  DBUG_ASSERT(p <= p_end);

  if (p == p_end) return false;

  bool find_digit = false;
  if (is_sign(p)) ++p;
  while (p < p_end) {
    if (!isdigit(*p)) return false;
    find_digit = true;
    ++p;
  }
  if (!find_digit) return false;
  return true;
}

/**
 *  preprocess the digit in value
 *  return: true  -- error
 *          false -- ok
 **/
static void preprocess_to_num_digit(NUMProc *Np) {
  if (IS_X(Np->Num)) return;

  const char *p = Np->inout_p;
  const char *p_end = Np->inout_end;
  bool read_dec = false;
  enum_value_type val_type = enum_value_type::_V_LAST;

  while (p < p_end) {
    val_type = get_value_type(p);
    switch (val_type) {
      case enum_value_type::V_DIGIT:
        if (read_dec)
          ++Np->read_post;
        else
          ++Np->read_pre;
        break;

      case enum_value_type::V_DECI:
        read_dec = true;
        break;

      case enum_value_type::V_CURR:
        // select to_number('2233$8','9ggGGggggg0900U9') "Amount" from dual;
        if (IS_CURRENCY_MID(Np->Num)) {
          read_dec = true;
        }
        break;

      case enum_value_type::V_E:
        Np->is_valid_exp = check_valid_exponent(p + 1, Np->exp_end);
        return;

      default:
        break;
    }
    ++p;
  }
}

/**
  preprocess_to_num: preprocess before matching.This is just an entrance of
  serval preprocessing methods, including:
    preprocessing for begin sign,
    preprocessing for currency,
    preprocessing for digit,
    preprocessing for X-fmt,
    preprocessing for single sign in value string.

  @param Np            Pointer to the NUMProc struct
  @param input_len     length of input value that paticipate into calculating in
                      'X'-format .
  @note Here we ONLY do Pre-processing operations and DONT judge whether the
  match was successful or failed.
*/
static void preprocess_to_num(NUMProc *Np, int input_len) {
  DBUG_ASSERT(Np->inout_p == Np->inout);
  DBUG_ASSERT(Np->inout_p < Np->inout_end);

  preprocess_to_num_begin_sign(Np);

  preprocess_to_num_currency(Np);

  preprocess_to_num_digit(Np);

  preprocess_to_num_X(Np, input_len);

  preprocess_to_num_single_sign(Np);
}

/**
 *  init the NUMProc struct
 **/
static void init_NUMProc(NUMProc *Np, NUMDesc *Num, const char *inout,
                         int inout_len, char *number, size_t num_len) {
  memset(Np, 0, sizeof(NUMProc));

  Np->Num = Num;
  Np->is_to_char = 0;
  Np->number = number;
  Np->inout = inout;
  Np->inout_p = Np->inout;
  Np->inout_end = Np->inout + inout_len;

  if (IS_END_S(Num) || IS_MINUS(Num) ||
      (IS_BRACKET(Num) && '>' == *(Np->inout_end - 1)))
    Np->exp_end = Np->inout_end - 1;
  else
    Np->exp_end = Np->inout_end;

  Np->read_post = 0;
  Np->read_pre = 0;
  Np->xmatch_len = 0;
  Np->read_dec = false;
  Np->in_type = NumBaseType::DEC;

  Np->sign = false;
  Np->is_single_sign = false;
  Np->is_valid_exp = true;

  Np->out_pre_spaces = 0;
  *Np->number = ' ';             /* sign space */
  Np->number_p = Np->number + 1; /* first char is space for sign */
  Np->number_end = Np->number + num_len - 1;

  Np->num_in = 0;
  Np->num_curr = 0;

  TO_NUM_prepare_locale(Np);

  return;
}

/*process_to_num_empty_fmt(NUMProc* Np)
When matching empty fmt:
If there is only one '-' in the value string, we need to return error.
However if it is special, we need to skip consecutive comma and one decimal
point immediately behind(if there is).
examples:
    select to_number(',,','') from dual;
    select to_number('-.','') from dual;
    select to_number('-,,.','') from dual;
    select to_number('-','') as result from dual; --- error num
*/
static inline bool process_to_num_empty_fmt(NUMProc *Np) {
  if (0 == Np->Num->fmt_len) {
    if (Np->is_single_sign) {
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error number.");
      return true;
    }
  }
  return false;
}

/**
 *  recheck if the value is matched with the fmt
 *  return: true  -- error
 *          false -- ok
 **/
static inline bool process_to_num_end_fmt(NUMProc *Np) {
  // when fmt is ''
  if (process_to_num_empty_fmt(Np)) return true;

  // select to_number('-,,.','S99') from dual;
  skip_specified_comma_and_decimal(Np);

  // where value is longger than fmt. eg:select to_number('12 ','99') from dual;
  if (Np->inout_p != Np->inout_end) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "error number.");
    return true;
  }
  return false;
}

/**
 *  convert ulonglong to a char string
 *  return: true  -- error
 *          false -- ok
 **/
static void ull2char(const ulonglong *from, char *&to) {
  if (NULL == from || NULL == to) {
    return;
  }
  ulonglong tmp = *from;
  char *num_p = to;
  int x = 0, y = 0;
  if (0 == tmp) *num_p++ = '0';
  while (tmp) {
    *num_p++ = '0' + (char)(tmp % 10);
    tmp /= 10;
    ++y;
  }
  *num_p = '\0';
  --y;
  while (x < y) {
    char c = to[x];
    to[x] = to[y];
    to[y] = c;
    ++x;
    --y;
  }
  to = num_p;
  return;
}

static bool is_specified_number(const char *p) {
  if (NULL == p) return false;
  if ('.' == *p || ' ' == *p || '+' == *p || '-' == *p) return true;
  return false;
}
/**
 *  finish NUMProc
 *   return: true  -- error
 *          false -- ok
 **/
static void finish_NUMProc(NUMProc *Np, const ulonglong &ull_hexsum,
                           const my_decimal &dec_hexsum) {
  DBUG_ASSERT(Np->in_type == NumBaseType::DEC ||
              Np->in_type == NumBaseType::HEX);

  if (Np->in_type == NumBaseType::DEC) {
    // select to_number('$','L999') as result from dual;->" " -> " 0"
    // select to_number('+$','SL999') as result from dual;->"+." -> "+0"
    // fix Warning | 1366 | Incorrect DECIMAL value: '0' for column '' at row -1
    if (is_specified_number(Np->number_p - 1)) {
      DBUG_ASSERT(Np->number_p < Np->number_end);
      *Np->number_p = '0';
      ++Np->number_p;
    }
    DBUG_ASSERT(Np->number_p <= Np->number_end);
    *Np->number_p = '\0';
    /*
     * Correction - precision of dec. number
     */
    Np->Num->post = Np->read_post;

  } else if (Np->in_type == NumBaseType::HEX) {
    if (Np->xmatch_len <= 16) {
      DBUG_ASSERT(Np->number_p < Np->number_end);
      ull2char(&ull_hexsum, Np->number_p);
      DBUG_ASSERT(Np->number_p <= Np->number_end);
    } else {
      String strtmp, *pStr = &strtmp;
      my_decimal2string(0x1F, &dec_hexsum, 0, 0, 0, pStr);
      char *pstr = pStr->c_ptr_safe();
      int str_len = pStr->length();
      DBUG_ASSERT(Np->number_p < Np->number_end);
      Np->number_p = my_stpncpy(Np->number, pstr, str_len);
      DBUG_ASSERT(Np->number_p <= Np->number_end);
      *Np->number_p = '\0';
    }
  }

  return;
}

/*
 * process the NUM_PR, NUM_S, NUM_MI format
 * return: true  -- error
 *         false -- ok
 */
static bool process_to_num_sign(NUMProc *Np, int id) {
  bool res = true;
  DBUG_ASSERT(NUM_S == id || NUM_MI == id || NUM_PR == id);

  switch (id) {
    case NUM_S:
      res = process_to_num_S(Np);
      break;
    case NUM_MI:
      res = process_to_num_MI(Np);
      break;
    case NUM_PR:
      res = process_to_num_PR(Np);
      break;
    default:
      DBUG_ASSERT(0);
      break;
  }

  return res;
}

/**
 * Process the to num value and format.
 * return: true  -- error
 *         false -- ok
 */
static bool TO_NUM_processor(FormatNode *node, NUMDesc *Num, const char *inout,
                             char *number, size_t num_len, int input_len) {
  DBUG_ASSERT(NULL != node);
  DBUG_ASSERT(NULL != Num);
  DBUG_ASSERT(NULL != inout);
  DBUG_ASSERT(NULL != number);
  DBUG_ASSERT(input_len >= 0);

  FormatNode *n = node;
  NUMProc _Np;
  NUMProc *Np = &_Np;

  ulonglong ullsum = 0;
  my_decimal dec_sum;
  int2my_decimal(0x1F, 0, false, &dec_sum);

  bool res = false;

  init_NUMProc(Np, Num, inout, input_len, number, num_len);

  preprocess_to_num(Np, input_len);

  for (; n->type != FormatNodeType::END; n++) {
    DBUG_ASSERT(n->type == FormatNodeType::ACTION);
    DBUG_ASSERT(Np->inout_p <= Np->inout_end);

    switch (n->key->id) {
      case NUM_0:
        res = process_to_num_0(Np);
        break;

      case NUM_9:
        res = process_to_num_9(Np);
        break;

      case NUM_DEC:
      case NUM_D:
        res = process_to_num_decimal_point(Np);
        break;

      case NUM_DOLLAR:
        res = process_to_num_dollar(Np);
        break;

      case NUM_COMMA:
      case NUM_G:
        res = process_to_num_group_separator(Np);
        break;

      case NUM_S:
      case NUM_MI:
      case NUM_PR:
        res = process_to_num_sign(Np, n->key->id);
        break;

      case NUM_L:
      case NUM_C:
      case NUM_U:
        res = process_to_num_LCU(Np, n->key->id);
        break;

      case NUM_X:
        res = process_to_num_X(Np, ullsum, dec_sum);
        break;

      case NUM_E:
        res = process_to_num_E(Np);
        break;

      default:
        my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), n->key->name);
        return true;
    }
    Np->Num->cur_pos += n->key->len;

    // Process the error result.
    if (res) return true;
  }

  if (process_to_num_end_fmt(Np)) return true;
  finish_NUMProc(Np, ullsum, dec_sum);
  return false;
}

/**
 * remove the pre space of the value string.
 * If this string is full of spaces, we reserve one space for later processsing.
 * return: char*
 */
static const char *preprocess_value_leading_spaces(const char *pstr,
                                                   size_t &pstr_len) {
  DBUG_ASSERT(NULL != pstr);
  // Remove ' ' at the begining the args[0].
  // select to_number(' ','9') as result from dual;
  // select to_number('   ') as result from dual;
  while (*pstr == ' ' && pstr_len > 1) {
    pstr++;
    pstr_len--;
  }

  return pstr;
}

static inline bool is_start_sign(const char *p, const char *p_end,
                                 size_t p_len) {
  DBUG_ASSERT(NULL != p);
  DBUG_ASSERT(p < p_end);
  DBUG_ASSERT('+' == *p || '-' == *p);

  if (p + p_len == p_end) return true;
  return false;
}

static bool is_end_space(const char *p, const char *p_end) {
  DBUG_ASSERT(NULL != p);
  DBUG_ASSERT(p < p_end);

  while (p < p_end)
    if (' ' != *(p++)) return false;

  return true;
}

/**
  process_one_param_E: one parameter processing method for scientific notation
  @param  p          Pointer to the start of the exponent string
  @param  p_end      Pointer to the one position after end of the exponent string
  @param  valid_num  flag if the string before exponent is valid
  @returns   true if error ,false is success
*/
static bool process_one_param_E(const char *p, const char *p_end,
                                bool valid_num) {
  DBUG_ASSERT(NULL != p);
  DBUG_ASSERT(p < p_end);

  // select to_number('.E+3') from dual;
  if (!valid_num) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "invalid number.");
    return true;
  }

  ++p;
  while (' ' == *(p_end - 1)) --p_end;

  if (!check_valid_exponent(p, p_end)) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "invalid number.");
    return true;
  }

  return false;
}

/**
  parse_to_num_one_param_str: check if the value string is valid.

  @param p_str      pointer to the input value string.
  @param str_len    length of the value string
  @returns  false   if the value string is valid,
            true    if it is invalid
*/
bool parse_to_num_one_param_str(const char *p_str, size_t str_len) {
  DBUG_ASSERT(NULL != p_str);
  DBUG_ASSERT(0 != str_len);

  size_t p_len = str_len;
  const char *p = p_str;
  const char *p_end = p_str + p_len;

  p = preprocess_value_leading_spaces(p, p_len);

  bool res = false;
  bool find_num = false;
  bool find_dec = false;
  enum_value_type val_type = enum_value_type::_V_LAST;
  while (p < p_end) {
    val_type = get_value_type(p);
    switch (val_type) {
      case enum_value_type::V_DIGIT:
        find_num = true;
        break;

      case enum_value_type::V_DECI:
        if (find_dec) res = true;
        find_dec = true;
        break;

      case enum_value_type::V_SIGN:
        if (!is_start_sign(p, p_end, p_len)) res = true;
        break;

      case enum_value_type::V_SPACE:
        if (!is_end_space(p, p_end)) res = true;
        break;

      case enum_value_type::V_E:
        return process_one_param_E(p, p_end, find_num);

      default:
        res = true;
        break;
    }
    if (res) {
      my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "invalid number.");
      return true;
    }
    ++p;
  }

  if (!find_num) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "invalid number.");
    return true;
  }

  return false;
}

/* -------------------
 * Process the to num value and format.
 * -------------------
 * return: true  -- error
 *         false -- ok
 */
bool process_to_num_two_params(String *value, String *fmt, char *out_value,
                               size_t out_len) {
  NUMDesc Num;
  FormatNode *format = NULL;
  bool result = true;

  DBUG_ASSERT(NULL != value && NULL != fmt && NULL != out_value);

  const char *pstr = const_cast<const char *>(value->c_ptr_safe());
  size_t pstr_len = value->length();
  const char *fmtstr = fmt->c_ptr_safe();
  size_t fmtstr_len = fmt->length();

  DBUG_ASSERT(NULL != pstr && NULL != fmtstr);

  if (fmtstr_len >= TO_NUM_MAX_STR_LEN) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "error format length");
    return true;
  }

  initialize_NUM(&Num);
  Num.fmt_len = fmtstr_len;

  if (NULL == current_thd || NULL == current_thd->mem_root) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0),
             "to_number get current_thd->mem_root failed");
    return true;
  }

  size_t mem_size = (fmtstr_len + 1) * sizeof(FormatNode);
  /* thd->mem_root mem will be released by thd. */
  format = (FormatNode *)current_thd->mem_root->Alloc(mem_size);
  if (NULL == format) {
    my_error(ER_TO_NUM_FORMAT_INVALID, MYF(0), "to_number format malloc error");
    return true;
  }

  if ((result = parse_format_for_to_num(format, fmtstr, NUM_keywords, NUM_index,
                                        &Num))) {
    goto end;
  }

  pstr = preprocess_value_leading_spaces(pstr, pstr_len);

  result = TO_NUM_processor(format, &Num, pstr, out_value, out_len, pstr_len);

end:
  return result;
}
