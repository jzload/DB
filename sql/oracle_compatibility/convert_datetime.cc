/* Implement of TO_DATE(), TO_TIMESTAMP()
 *
 */

#include "m_string.h"
#include "my_dbug.h"
#include "my_macros.h"  // array_elements
#include "my_sys.h"
#include "mysqld_error.h"  // ER_...

#include "my_time.h"
#include "sql/current_thd.h"  // current_thd
#include "sql/sql_class.h"
#include "sql/sql_locale.h"
#include "sql/strfunc.h"  // find_type
#include "sql/tztime.h"   // Time_zone

#include "sql/oracle_compatibility/convert_datetime.h"
#include "sql/oracle_compatibility/ora_format.h"

using std::min;

static const char g_arr_delimiters[] = {
    '`', '~', '!', '@', '#',  '$', '%', '^',  '&',  '*',  '(',  ')',
    '-', '_', '=', '+', '\\', '|', '{', '}',  '[',  ']',  ':',  ';',
    '<', '>', ',', '.', '?',  '/', ' ', '\r', '\n', '\t', '\v', '\f'};

static int s_days_in_month[2][13] = {
    /* dummy, January, Feburary, ...     December */
    {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};

/* Which func needs to extract date time?  */
enum class Func_Type { TO_DATE, TO_TIMESTAMP, UNKNOWN };

enum class CLOCK_TYPE {
  CLOCK_UNINITIALIZED = INT_MAX32,
  CLOCK_24_HOUR = 0,
  CLOCK_12_HOUR
};

/*
 * UNINITIALIZED value for int members in TmFromStr.
 * UNINITIALIZED_INT_IN_TMFS 不能为-1，否则年是-1就不报错了，
 * 详见函数 convert_year_month_day
 */
static const int UNINITIALIZED_INT_IN_TMFS = INT_MAX32;

/*
 * Structure For char->date/time conversion
 *
 * TmFromStr(TMFS)
 */
struct TmFromStr {
  DateFormatMode mode;
  int hh;    /* hour */
  int pm;    /* 0: am, 1: pm */
  int mi;    /* minute */
  int ss;    /* second */
  int sssss; /* Seconds past midnight */
  int d;     /* stored as 1-7, Sunday = 1, 0 means missing */
  int dd;    /* day of month */
  int ddd;   /* day of year */
  int mm;    /* month */
  int ms;    /* microsecond */
  int year;
  int bc;
  int ww;
  int w;
  int cc;
  int j;
  int us;
  int yysz;         /* size of year, ie, 4(digits), 2(digits) */
  CLOCK_TYPE clock; /* 12 or 24 hour clock?
                       set value when processing format HH/HH12/HH24 */
  int tzsign;       /* +1, -1 or 0 if timezone info is absent */
  int tzh;
  int tzm;

  TmFromStr() {
    mode = DateFormatMode::NONE;
    hh = UNINITIALIZED_INT_IN_TMFS;
    pm = UNINITIALIZED_INT_IN_TMFS;
    mi = UNINITIALIZED_INT_IN_TMFS;
    ss = UNINITIALIZED_INT_IN_TMFS;
    sssss = UNINITIALIZED_INT_IN_TMFS;
    d = UNINITIALIZED_INT_IN_TMFS;
    dd = UNINITIALIZED_INT_IN_TMFS;
    ddd = UNINITIALIZED_INT_IN_TMFS;
    mm = UNINITIALIZED_INT_IN_TMFS;
    ms = UNINITIALIZED_INT_IN_TMFS;
    year = UNINITIALIZED_INT_IN_TMFS;
    bc = UNINITIALIZED_INT_IN_TMFS;
    ww = UNINITIALIZED_INT_IN_TMFS;
    w = UNINITIALIZED_INT_IN_TMFS;
    cc = UNINITIALIZED_INT_IN_TMFS;
    j = UNINITIALIZED_INT_IN_TMFS;
    us = UNINITIALIZED_INT_IN_TMFS;
    yysz = UNINITIALIZED_INT_IN_TMFS;
    clock = CLOCK_TYPE::CLOCK_UNINITIALIZED;
    tzsign = UNINITIALIZED_INT_IN_TMFS;
    tzh = UNINITIALIZED_INT_IN_TMFS;
    tzm = UNINITIALIZED_INT_IN_TMFS;
  }
};

#define HOURS_PER_DAY 24
#define SECS_PER_HOUR 3600
#define SECS_PER_MINUTE 60
#define MINS_PER_HOUR 60
#define MONTHS_PER_YEAR 12
#define MICROSECS_PER_SEC 1000000L

#define INT64CONST(x) (x##L)
#define USECS_PER_SEC INT64CONST(1000000)

/* is leap year? */
#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

/*
 * Skip TM / th in FROM_CHAR
 *
 * If S_THth is on, skip two chars, assuming there are two available
 */
#define SKIP_THth(ptr, _suf)               \
  do {                                     \
    if (S_THth(_suf)) {                    \
      if (*(ptr)) (ptr) += get_mblen(ptr); \
      if (*(ptr)) (ptr) += get_mblen(ptr); \
    }                                      \
  } while (0)

static const char *ampm_strings[] = {"AM", "PM", "am", "pm", NullS};

static const char *ampm_dot_strings[] = {"A.M.", "P.M.", "a.m.", "p.m.", NullS};

static TYPELIB typelib_ampm = {array_elements(ampm_strings) - 1, "",
                               ampm_strings, NULL};

static TYPELIB typelib_ampm_dot = {array_elements(ampm_dot_strings) - 1, "",
                                   ampm_dot_strings, NULL};

/*
 * Determine whether it is a delimiter
 */
static inline bool isdelimiter(const char c) {
  for (char ch : g_arr_delimiters) {
    if (c == ch) return true;
  }

  return false;
}

/*
 * Get function type according to the function name.
 *
 * func_name(in): "to_date" or "to_timestamp"
 * return: Func_Type
 */
static Func_Type get_func_type(const char *func_name) {
  if (strcasecmp(func_name, "to_date") == 0) {
    return Func_Type::TO_DATE;
  }

  if (strcasecmp(func_name, "to_timestamp") == 0) {
    return Func_Type::TO_TIMESTAMP;
  }

  DBUG_ASSERT(0);

  return Func_Type::UNKNOWN;
}

/*
 * Get the month and day based on the number of days in the year
 * @year : year
 * @ddd  : days in the year
 * @mon[out]    : month
 * @day[out]    : day
 */
bool process_ddd_in_year(int year, int ddd, int &mon, int &day) {
  if (ddd < 1 || ddd > 366) return true;

  /* 平年没有366天 */
  if (ddd == 366 && !isleap(year)) return true;

  int *month = isleap(year) ? s_days_in_month[1] : s_days_in_month[0];

  int i = 0;
  do {
    ddd -= month[i];

    i++;
    if (ddd <= month[i]) break;
  } while (i < 12);

  day = ddd;
  mon = i;

  return false;
}

/*
 * Check if the first word in a string is one of the ones in TYPELIB
 *
 * Refert to prototype function check_word().
 *
 * lib(in): TYPELIB
 * val(in): String to check
 * end(in): End of input
 * end_of_word Store(out): value of last used byte here if we found word

 * return:
 *  0    No matching value
 *  > 1  lib->type_names[#-1] matched
 *       end_of_word will point to separator character/end in 'val'
 */
static uint check_keyword(TYPELIB *lib, const char *val, const char *end,
                          const char **end_of_word) {
  int res;
  const char *ptr;

  /* Find end of keyword */
  for (ptr = val; ptr < end;) {
    /* some keyword contains '.', ie. "P.M." */
    if (isalpha(*ptr) || *ptr == '.') {
      ++ptr;
    } else {
      break;
    }
  }

  if ((res = find_type(lib, val, (uint)(ptr - val), 1)) > 0) {
    *end_of_word = ptr;
  }

  return res;
}

/* pos starts from 1, not 0
 *
 * pos_in_ampm_string(in): pos of ampm_strings/ampm_dot_strings
 * return:
 *   true  -- is pm
 *   false -- is not pm
 *
 */
static bool is_pm(unsigned int pos) {
  if (pos % 2 == 0) {
    return true;
  }

  return false;
}

/*
 * get current year
 */
static int get_current_year() {
  struct timeval time_value;
  my_micro_time_to_timeval(my_micro_time(), &time_value);

  MYSQL_TIME current_time;
  Time_zone *tz = current_thd->time_zone();
  tz->gmt_sec_to_TIME(&current_time, time_value);

  return current_time.year;
}

/*
 * get current century
 * 2013 -> 20
 * 3150 -> 31
 */
static inline int get_current_century() {
  int year = get_current_year();

  return year / 100;
}

/*
 * get current decade
 * 2013 -> 1
 * 3150 -> 5
 */
static inline int get_current_decade() {
  int year = get_current_year();

  return year / 10 % 10;
}

/*
 * get current datetime.
 *
 * ltime(out):
 */
static void get_current_datetime(MYSQL_TIME *ltime) {
  struct timeval time_value;
  my_micro_time_to_timeval(my_micro_time(), &time_value);

  Time_zone *tz = current_thd->time_zone();
  tz->gmt_sec_to_TIME(ltime, time_value);
}

/*
 * get the bytes of one character
 */
static size_t get_mblen(const char *mbstr) {
  return my_mbcharlen(system_charset_info, (unsigned char)(*mbstr));
}

/* Parse one normal char into format node, and advance string and format node.
 *
 * str(in/out): one normal char
 * n(in/out): format tree node
 * type: format node type.
 */
static void parse_format_one_normal_char(const char *&str, FormatNode *&n,
                                         FormatNodeType type) {
  size_t ch_len;

  ch_len = get_mblen(str);
  n->type = type;

  char *dst = &n->character[0];
  size_t len_to_copy = std::min(sizeof(n->character) - 1, ch_len);

  my_stpncpy(dst, str, len_to_copy);
  n->character[len_to_copy] = '\0';
  n->key = NULL;
  n->suffix = 0;

  ++n;
  str += ch_len;
}

/* Parse normal chars into format node, and advance string and format node.
 *
 * There are two kinds of noraml chars:
 *   quoted characters like: "test"
 *   one character like: '-', '/', ',', '.', ':', and so on
 *
 * str(in/out):
 * n(in/out): format node
 *
 * return: true  -- has error
 *         false -- ok
 */
static bool parse_format_normal_chars(const char *&str, FormatNode *&n) {
  /*
   * Process double-quoted literal string, if any
   */
  if (*str == '"') {
    ++str;
    while (*str) {
      if (*str == '"') {
        ++str;
        break;
      }

      /* backslash quotes the next character, if any */
      if (*str == '\\' && *(str + 1)) {
        ++str;
      }

      parse_format_one_normal_char(str, n, FormatNodeType::STR);
    }

    return false;
  }

  /* select to_date('(2010', '"yyyy');
   * select to_date('(2010', '\'yyyy');
   * 这种语句oracle是不支持的，因为oracle不认为 " 以及 '
   * 是分隔符，所以修改了下面逻辑。
   */
  if (isdelimiter(*str)) {
    parse_format_one_normal_char(str, n, FormatNodeType::CHAR);

    return false;
  }

  my_error(ER_INVALID_FORMAT, MYF(0), str);
  return true;
}

/* Parse the digit of ff format. Judge if the format is ff[1..9]
 *
 * str(in/out):
 * n(in/out): format node
 */
static void parse_format_ff(const char *&str, FormatNode *&n) {
  /*
   * judge if FF[1..9]
   */
  if (n->key->id == DTF_FF) {
    if ('\0' != *str) {
      if ((*str > '0') && (*str <= '9')) {
        n->precision = (*str) - '0';
        ++str;
        return;
      }
    }
  }

  return;
}

/*
 * Format parser: search small keywords and keyword's suffixes,
 * and make format-node tree.
 *
 * return: true  -- error
 *         false -- ok
 */
static bool parse_format(FormatNode *node, LEX_STRING *fmt_string,
                         const KeyWord *kw, const KeySuffix *suf,
                         const int *index) {
  FormatNode *n = node;
  const char *str = fmt_string->str;
  const char *str_end = fmt_string->str + fmt_string->length;
  bool has_err = false;

  while (str != str_end) {
    int suffix = DTF_S_none_;
    const KeySuffix *s;

    /*
     * Prefix
     */
    if ((s = suff_search(str, suf, SuffixType::PREFIX)) != NULL) {
      suffix |= s->id;
      str += s->len;
    }

    if (str == str_end) {  // keey coverity quiet: '\0' == *str
      break;
    }

    /*
     * Keyword
     */
    if ((n->key = index_seq_search(str, kw, index)) != NULL) {
      n->type = FormatNodeType::ACTION;
      n->suffix = suffix;
      n->precision = 0;
      str += n->key->len;

      parse_format_ff(str, n);

      /*
       * Postfix
       */
      if (str != str_end &&
          (s = suff_search(str, suf, SuffixType::POSTFIX)) != NULL) {
        n->suffix |= s->id;
        str += s->len;
      }

      n++;
      continue;
    }

    /*
     * normal char(s)
     */
    has_err = parse_format_normal_chars(str, n);
    if (has_err) {
      return true;
    }
  }

  n->type = FormatNodeType::END;
  n->suffix = 0;

  return false;
}

/* check the format in TO_DATE()/TO_TIMESTAMP() whether ok or not.
 *
 * node(in): format tree node
 * func_type(in): TO_DATE or TO_TIMESTAMP
 * return:
 *   true  -- error in format
 *   false -- no error
 */
static bool check_format(FormatNode *node, Func_Type func_type) {
  FormatNode *n;

  for (n = node; n->type != FormatNodeType::END; ++n) {
    if (n->type != FormatNodeType::ACTION) {
      continue;
    }

    switch (n->key->id) {
      // year, month, day
      case DTF_YYYY:
      case DTF_YYY:
      case DTF_YY:
      case DTF_Y:
      case DTF_Y_YYY:
      case DTF_MM:
      case DTF_MON:
      case DTF_mon:
      case DTF_MONTH:
      case DTF_month:
      case DTF_DDD:
      case DTF_DD:  // fall through

      // hour, minute, second
      case DTF_HH24:
      case DTF_HH12:
      case DTF_HH:
      case DTF_MI:
      case DTF_SS:  // fall through

      // am, pm
      case DTF_AM:
      case DTF_am:
      case DTF_A_M:
      case DTF_a_m:
      case DTF_PM:
      case DTF_pm:
      case DTF_P_M:
      case DTF_p_m:
        break;

      case DTF_FF:
        /* TO_DATE() doesn't support format FF */
        if (func_type == Func_Type::TO_DATE) {
          my_error(ER_INVALID_FORMAT, MYF(0), n->key->name);
          return true;
        }

        break;

      default:
        my_error(ER_INVALID_FORMAT, MYF(0), n->key->name);
        return true;
    }

    if (n->suffix != DTF_S_none_) {
      if (n->suffix & DTF_S_FM) {
        my_error(ER_INVALID_FORMAT, MYF(0), "FM");
        return true;
      }

      if (n->suffix & DTF_S_TH) {
        my_error(ER_INVALID_FORMAT, MYF(0), "TH");
        return true;
      }

      if (n->suffix & DTF_S_th) {
        my_error(ER_INVALID_FORMAT, MYF(0), "th");
        return true;
      }

      if (n->suffix & DTF_S_SP) {
        my_error(ER_INVALID_FORMAT, MYF(0), "SP");
        return true;
      }

      if (n->suffix & DTF_S_TM) {
        my_error(ER_INVALID_FORMAT, MYF(0), "TM");
        return true;
      }

      my_error(ER_INVALID_FORMAT, MYF(0), "unknown");
      return true;
    }
  }

  return false;
}

/*
 * Calculate the length of space in the beginning of str
 */
static size_t strspace_len(const char *str) {
  size_t len = 0;

  while (*str && isspace(*str)) {
    ++str;
    ++len;
  }

  return len;
}

/* Set the date mode of a from-char conversion.
 *
 * If the date mode has already been set, and the caller attempts to set,
 * return error.
 *
 * tmfs(in/out):
 * mode(in):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool set_date_mode_in_conversion(TmFromStr *tmfs,
                                        const DateFormatMode mode) {
  if (mode != DateFormatMode::NONE) {
    if (tmfs->mode == DateFormatMode::NONE) {
      tmfs->mode = mode;
    } else if (tmfs->mode != mode) {
      my_error(
          ER_CONVERT_DATETIME_FAIL, MYF(0),
          "invalid combination of date conventions (Gregorian and ISO week)");
      return true;
    }
  }

  return false;
}

/* Set the int value to dest. If check fail, report error with the format name.
 * Only used for TMFS.
 *
 * dest(in/out):
 * value(in):
 * node(in):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool set_int_with_check(int *dest, int value, const FormatNode *node) {
  if (*dest != UNINITIALIZED_INT_IN_TMFS) {
    std::ostringstream out;
    out << "format code appears twice: " << node->key->name;
    std::string errmsg(out.str());
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0), errmsg.c_str());

    return true;
  }

  *dest = value;
  return false;
}

/* Parse src string to int, and store it in dest.
 *
 * dest(out):
 * src(in/out):
 * src_end(in): the end of src, point to '\0'
 * len(in): the max length need to be parsed
 * node(in):
 *
 * return: matched chars
 */
static int parse_int_with_len(int *dest, const char *src, const char *src_end,
                              size_t len, const FormatNode *node) {
  int result;
  int error = 0;
  const char *new_src;

  DBUG_ASSERT(dest != NULL);
  DBUG_ASSERT(src != NULL);
  DBUG_ASSERT(src_end != NULL);
  DBUG_ASSERT(src_end >= src);

  // Oracle will treat '-/+' as a negative/positive sign
  // and subsititute it into the evaluation.
  // We follow this rule.
  if (src[0] == '-' || src[0] == '+') {
    len++;

    if (src_end == src + 1) {
      goto error_label;
    }
  }

  new_src = src + len < src_end ? src + len : src_end;
  result = static_cast<int>(my_strtoll10(src, &new_src, &error));

  if (new_src == src || error > 0) {
    goto error_label;
  }

  *dest = result;

  return new_src - src;

error_label:
  my_error(ER_INVALID_VALUE_FOR_FORMAT, MYF(0), node->key->name);

  return 0;
}

/*
 * Convert month word to number
 * Support grammars like:
 * select to_date('2020-Jan-20','yyyy-Mon-dd') from dual;
 * select to_date('2020-January-20','yyyy-Month-dd') from dual;
 * select to_date('Jan','Mon') from dual;
 *
 * parameter:
 * @dst: [out] value of month.
 * @src: [in/out] beginning of date_string.
 * @src_end: [in] ending of date_string.
 * @node: [in] month formatNode.
 *
 * return:
 * 0: format error.
 * positive number: parsed length
 */
static int parse_month_str_to_num(int &dst, const char *src,
                                  const char *src_end) {
  const int MIN_MON_LEN = 3;
  const int MAX_MON_LEN = 9;
  const int MON_NUMBR = 13;

  const char *months[2][MON_NUMBR] = {
      /* dummy, January, Feburary, ...     December */
      {0, "January", "Feburary", "March", "April", "May", "June", "July",
       "August", "September", "October", "November", "December"},
      {0, "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
       "Nov", "Dec"}};

  const char *point = src;

  while (point < src_end) {
    if ((!isalpha(*point))) break;

    point++;
  }

  do {
    int len = point - src;

    /*
     * The longest word of the month is 9 (September) and
     * the shortest word of the month is 3 (abbreviate)
     */
    if (len < MIN_MON_LEN || len > MAX_MON_LEN) break;

    char buf[MAX_MON_LEN + 1] = {0};
    point = src;

    int i = 0;
    while (i < len) buf[i++] = *point++;

    const char **month = len > MIN_MON_LEN ? months[0] : months[1];

    for (i = 1; i < MON_NUMBR; i++) {
      if (strcasecmp(buf, month[i]) == 0) break;
    }

    if (MON_NUMBR == i) break;

    dst = i;

    return len;
  } while (0);

  my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
           "literal does not match format string");

  return 0;
}

/*
 * Process a string as denoted by AM/PM/am/pm
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_ampm_string(const char **src, const char *src_end,
                                TmFromStr *out) {
  unsigned int pos;

  const char *s = *src;

  pos = check_keyword(&typelib_ampm, s, src_end, &s);
  if (pos <= 0) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "the value of format AM/PM/am/pm is wrong");
    return true;
  }

  if (out->pm != UNINITIALIZED_INT_IN_TMFS) {
    std::ostringstream ss;
    ss << "format code appears twice: A.M./P.M./a.m./p.m.";
    std::string errmsg(ss.str());
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0), errmsg.c_str());

    return true;
  }

  out->pm = is_pm(pos) ? 1 : 0;

  *src += s - (*src);

  return false;
}

/*
 * Process a string as denoted by A.M./P.M./a.m./p.m.
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_ampm_dot_string(const char **src, const char *src_end,
                                    TmFromStr *out) {
  unsigned int pos;

  const char *s = *src;

  pos = check_keyword(&typelib_ampm_dot, s, src_end, &s);
  if (pos <= 0) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "the value of format A.M./P.M./a.m./p.m. is wrong");
    return true;
  }

  if (out->pm != UNINITIALIZED_INT_IN_TMFS) {
    std::ostringstream ss;
    ss << "format code appears twice: A.M./P.M./a.m./p.m.";
    std::string errmsg(ss.str());
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0), errmsg.c_str());

    return true;
  }

  out->pm = is_pm(pos) ? 1 : 0;

  *src += s - (*src);

  return false;
}

/*
 * Process a string as denoted by Y
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_y_string(const char **src, const char *src_end,
                             const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 1, n);
  if (0 == matched_chars || value < 0) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "year must be between 1 and 9999");

    return true;
  }

  value += get_current_decade() * 10;
  value += get_current_century() * 100;

  if (set_int_with_check(&out->year, value, n)) {
    return true;
  }

  out->yysz = 4;

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/**
  @brief Get the number length from DATE_STRING relative to FORMAT_STRING:'YY'.

  YY uses 2 digits to analysis DATE_STRING default, but it also can suppport
  grammars such as:
  select to_date('12', 'YY') from dual;
  select to_date('123', 'YYMM') from dual;
  select to_date('1204', 'YYMM') from dual;
  select to_date('123-12', 'YY-MM') from dual;
  select to_date('1234-12', 'YY-MM') from dual;
  The following grammars are supported too:
  select to_date('123', 'YY') from dual;
  select to_date('1204', 'YY') from dual;
  select to_date('   1204', 'YY    -- ') from dual;

  @param  src       Point point to start of DATE_STRING.
  @param  src_end   Point to start of DATE_STRING.
  @param  n         Point to FORMAT NODE.

  @return number length to 'YY'.
*/
static int calc_yy_digit_len(const char **src, const char *src_end,
                              const FormatNode *n) {
  const char *point = *src;

  if (point[0] == '-' || point[0] == '+') point++;

  int i = 0;
  while (i < 5 && point != src_end && isdigit(*point)) {
    point++;
    i++;
  }

  if (point == src_end) {
    const FormatNode *next = n;

    while (++next) {
      switch (next->type) {
        case FormatNodeType::ACTION:
          return 2;

        case FormatNodeType::END:
          return 4;

        default:
          break;
      }
    }
  }

  /*
   * Calculate 3 or 4 numbers should return 4. Such as:
   * to_date('123', 'YY'), to_date('1234', 'YY')
   * to_date('123-01', 'YY-mm'), to_date('1234-01', 'YY-mm')
   */
  if (i == 3 || i == 4) return 4;

  return 2;
}

/*
 * Process a string as denoted by YY
 *
  ---------------------------------------------------------------------------------
  Note:

  Oracle recommends that you use the 4-digit year element (YYYY) instead of the
 shorter year elements for these reasons:
  1. The 4-digit year element eliminates ambiguity.
  2. The shorter year elements may affect query optimization because the year is
 not known at query compile time and can only be determined at run time.
  ---------------------------------------------------------------------------------
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_yy_string(const char **src, const char *src_end,
                              const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;
  int len = calc_yy_digit_len(src, src_end, n);

  matched_chars = parse_int_with_len(&value, *src, src_end, len, n);
  if (0 == matched_chars || value < 0) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "year must be between 1 and 9999");

    return true;
  }

  int matched_digit = (**src == '+') ? matched_chars - 1 : matched_chars;

  if (matched_digit <= 2) value += get_current_century() * 100;

  if (set_int_with_check(&out->year, value, n)) {
    return true;
  }

  out->yysz = 4;

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/*
 * Process a string as denoted by YYY
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_yyy_string(const char **src, const char *src_end,
                               const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 3, n);
  if (0 == matched_chars || value < 0) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "year must be between 1 and 9999");

    return true;
  }

  value += get_current_century() * 100;

  if (set_int_with_check(&out->year, value, n)) {
    return true;
  }

  out->yysz = 4;

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/*
 * Process a string as denoted by YYYY
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_yyyy_string(const char **src, const char *src_end,
                                const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 4, n);
  if (0 == matched_chars || value <= 0) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "year must be between 1 and 9999");

    return true;
  }

  if (set_int_with_check(&out->year, value, n)) {
    return true;
  }

  out->yysz = 4;

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Process a string as denoted by Y,YYY
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_y_yyy_string(const char **src, const char *src_end,
                                 const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;
  const size_t max_num_digits = 4;
  char year_str[max_num_digits + 1] = {'\0'};

  size_t i = 0;  // the num of digits already copied

  const char *s = *src;
  s += strspace_len(s);

  while (*s != '\0' && s < src_end && i < max_num_digits) {
    if (*s == ',') {
      ++s;
      continue;
    }

    if (!isdigit(*s)) {
      break;
    }

    year_str[i++] = *s;
    ++s;
  }

  if (i == 0) {
    my_error(ER_INVALID_VALUE_FOR_FORMAT, MYF(0), n->key->name);
    return true;
  }

  char *year_str_end = year_str + i;

  matched_chars =
      parse_int_with_len(&value, year_str, year_str_end, max_num_digits, n);
  if (0 == matched_chars || value <= 0) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "year must be between 1 and 9999");

    return true;
  }

  if (set_int_with_check(&out->year, value, n)) {
    return true;
  }

  out->yysz = 4;

  *src = s;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Process a string as denoted by MM
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_mm_string(const char **src, const char *src_end,
                              const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  if (isalpha(**src)) {
    matched_chars = parse_month_str_to_num(value, *src, src_end);
  } else {
    matched_chars = parse_int_with_len(&value, *src, src_end, 2, n);
  }

  if (0 == matched_chars) {
    return true;
  }

  if (set_int_with_check(&out->mm, value, n)) {
    return true;
  }

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Process a string as denoted by DD
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_dd_string(const char **src, const char *src_end,
                              const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 2, n);
  if (0 == matched_chars) {
    return true;
  }

  if (set_int_with_check(&out->dd, value, n)) {
    return true;
  }

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Process a string as denoted by DDD
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_ddd_string(const char **src, const char *src_end,
                               const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 3, n);

  if (0 == matched_chars) {
    return true;
  }

  if (set_int_with_check(&out->ddd, value, n)) {
    return true;
  }

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Process a string as denoted by HH
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_hh_string(const char **src, const char *src_end,
                              const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 2, n);
  if (0 == matched_chars) {
    return true;
  }

  CLOCK_TYPE clock_type = CLOCK_TYPE::CLOCK_24_HOUR;
  if (n->key->id == DTF_HH || n->key->id == DTF_HH12) {
    clock_type = CLOCK_TYPE::CLOCK_12_HOUR;
  }

  if (CLOCK_TYPE::CLOCK_UNINITIALIZED != out->clock) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0), "HH/HH12/HH24 duplicate");
    return true;
  }

  out->clock = clock_type;

  if (set_int_with_check(&out->hh, value, n)) {
    return true;
  }

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Process a string as denoted by MI
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_mi_string(const char **src, const char *src_end,
                              const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 2, n);
  if (0 == matched_chars) {
    return true;
  }

  if (set_int_with_check(&out->mi, value, n)) {
    return true;
  }

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Process a string as denoted by SS
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_ss_string(const char **src, const char *src_end,
                              const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;

  matched_chars = parse_int_with_len(&value, *src, src_end, 2, n);
  if (0 == matched_chars) {
    return true;
  }

  if (set_int_with_check(&out->ss, value, n)) {
    return true;
  }

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

static bool modify_precision(int &value, int current_chars) {
  if (current_chars < DATETIME_MAX_DECIMALS) {
    for (int i = 0; i < DATETIME_MAX_DECIMALS - current_chars; i++)
      value = value * 10;
  } else {
    for (int i = 0; i < current_chars - DATETIME_MAX_DECIMALS; i++)
      value = value / 10;
  }
  return true;
}

/* Process a string as denoted by FF
 *
 * src(in/out): after processing, advance it.
 * src_end(in): the end of src, point to '\0'
 * n(in): format tree node
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_ff_string(const char **src, const char *src_end,
                              const FormatNode *n, TmFromStr *out) {
  int value;
  int matched_chars = 0;
  size_t precision;

  precision = n->precision;
  if (0 == precision) {
    precision = 9; /* the default is 9 */
  }

  matched_chars = parse_int_with_len(&value, *src, src_end, precision, n);

  if (0 == matched_chars) {
    return true;
  }

  modify_precision(value, matched_chars);

  if (set_int_with_check(&out->ms, value, n)) {
    return true;
  }

  *src += matched_chars;

  SKIP_THth((*src), n->suffix);

  return false;
}

/* Has YEAR, MONTH, or DAY format in the format tree ?
 *
 * node(in): format tree node
 *
 * return:
 *   true  -- has one of YEAR, MONTH, DAY format
 *   false -- not exists
 */
static bool has_format_year_month_day(const FormatNode *node) {
  const FormatNode *n = node;

  for (; n->type != FormatNodeType::END; n++) {
    if (n->type != FormatNodeType::ACTION) {
      continue;
    }

    switch (n->key->id) {
      case DTF_YYYY:
      case DTF_YYY:
      case DTF_YY:
      case DTF_Y:
      case DTF_Y_YYY:
      case DTF_MM:
      case DTF_MON:
      case DTF_mon:
      case DTF_MONTH:
      case DTF_month:
      case DTF_DD:
      case DTF_DDD:
        return true;

      default:
        break;
    }
  }

  return false;
}

/*
 * 正常来说，date_string的分隔符不应该超过format_string的分隔符，
 * 也就是说，下面的语法是错误的：
 * select to_date('2010-05-01', 'yyyymm-dd') from dual;
 * select to_date('2010+=----05-01', 'yyyy--mm-dd') from dual;
 *
 * 但分隔符如果是空格的话，处理就不一样了：
 * 1.如果还存在其它分隔符，空格可以全部省略，如：
 * select to_date('201    -  05-01', 'yyyy+mm-dd') from dual;
 * 这与 select to_date('201-05-01', 'yyyy+mm-dd') from dual;
 * select to_date('201    -  05-01', 'yyyy       +      mm-dd') from dual;
 * 效果是一样的。
 * 2.如果不存在其它分隔符，空格要保留一位当分隔符，如：
 * select to_date('201           05-01', 'yyyymm-dd') from dual;
 * 这与 select to_date('201 05-01', 'yyyymm-dd') from dual;一样，
 * 它们输出的结果都是：0201-05-01 00:00:00
 * 而select to_date('20105-01', 'yyyymm-dd') from dual;
 * 输出的结果是：0201-05-01 00:00:00
 *
 */
static bool process_date_string_delimiter(const char *&s, const char *s_end,
                                          const FormatNode *n) {
  DBUG_ASSERT(n->type != FormatNodeType::ACTION);

  if (n->type == FormatNodeType::STR) {
    size_t ch_len = get_mblen(s);
    char character[MAX_MULTIBYTE_CHAR_LEN + 1] = {0};
    size_t len_to_copy =
        std::min(MAX_MULTIBYTE_CHAR_LEN * sizeof(char), ch_len);

    my_stpncpy(character, s, len_to_copy);

    if (my_strcasecmp(system_charset_info, n->character, character))
      return true;

    s += ch_len;
  } else {
    while (isspace(*s)) s += get_mblen(s);

    if (s == s_end) return false;
    if (isdelimiter(*s)) s += get_mblen(s);
  }

  return false;
}

/*
 * Determine whether the DATA STRING matches, only allow trailing spaces.
 * return:
 * true: does not matche.
 * false: matches.
 */
static bool date_string_check_match(const char *s_begin, const char *s_end) {
  /*
   * when s != s_end and is all spaces, then return false, else report error:
   *  "date format picture ends before converting entire input string"
   */
  while (s_begin != s_end && isspace(*s_begin)) s_begin++;

  if (s_begin != s_end) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "date format picture ends before converting entire input string");

    return true;
  }

  return false;
}

/* Process a string as denoted by a list of FormatNodes.
 *
 * node(in): format tree node
 * in_string(in): input date/time string
 * out(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool process_DTF_from_str(const FormatNode *node,
                                 const LEX_STRING *in_string, TmFromStr *out) {
  const FormatNode *n = node;
  bool fx_mode = false;
  bool has_err;
  const char *s = in_string->str;
  const char *s_end = in_string->str + in_string->length;

  for (; n->type != FormatNodeType::END && s != s_end; n++) {
    if (n->type != FormatNodeType::ACTION) {
      if (process_date_string_delimiter(s, s_end, n)) {
        my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
                 "literal does not match format string");
        return true;
      }

      continue;
    }

    /* Ignore spaces before fields when not in FX(fixed width) mode */
    if (!fx_mode && n->key->id != DTF_FX) {
      s += strspace_len(s);
    }

    has_err = set_date_mode_in_conversion(out, n->key->date_mode);
    if (has_err) {
      return true;
    }

    switch (n->key->id) {
      case DTF_FX:
        fx_mode = true;
        break;

      case DTF_A_M:
      case DTF_P_M:
      case DTF_a_m:
      case DTF_p_m:
        has_err = process_ampm_dot_string(&s, s_end, out);
        break;

      case DTF_AM:
      case DTF_PM:
      case DTF_am:
      case DTF_pm:
        has_err = process_ampm_string(&s, s_end, out);
        break;

      case DTF_Y:
        has_err = process_y_string(&s, s_end, n, out);
        break;

      case DTF_YY:
        has_err = process_yy_string(&s, s_end, n, out);
        break;

      case DTF_YYY:
        has_err = process_yyy_string(&s, s_end, n, out);
        break;

      case DTF_YYYY:
        has_err = process_yyyy_string(&s, s_end, n, out);
        break;

      case DTF_Y_YYY:
        has_err = process_y_yyy_string(&s, s_end, n, out);
        break;

      case DTF_MM:
      case DTF_MON:
      case DTF_mon:
      case DTF_MONTH:
      case DTF_month:
        has_err = process_mm_string(&s, s_end, n, out);
        break;

      case DTF_DD:
        has_err = process_dd_string(&s, s_end, n, out);
        break;

      case DTF_DDD:
        has_err = process_ddd_string(&s, s_end, n, out);
        break;

      case DTF_HH:
      case DTF_HH12:
      case DTF_HH24:
        has_err = process_hh_string(&s, s_end, n, out);
        break;

      case DTF_MI:
        has_err = process_mi_string(&s, s_end, n, out);
        break;

      case DTF_SS:
        has_err = process_ss_string(&s, s_end, n, out);
        break;

      case DTF_FF:
        has_err = process_ff_string(&s, s_end, n, out);
        break;

      default:
        my_error(ER_INVALID_FORMAT, MYF(0), n->key->name);
        return true;
    }

    if (has_err) {
      return true;
    }
  }

  if (n->type != FormatNodeType::END) {
    if (has_format_year_month_day(n)) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "input value not long enough for date format");
      return true;
    }
  }

  return date_string_check_match(s, s_end);
}

/*
 * Convert hour in TmFromStr to MYSQL_TIME
 *
 * tmfs(in):
 * ltime(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool convert_hour(TmFromStr *tmfs, MYSQL_TIME *ltime) {
  /* hour */
  if (tmfs->hh == UNINITIALIZED_INT_IN_TMFS) {
    ltime->hour = 0;
    return false;
  }

  if (tmfs->clock == CLOCK_TYPE::CLOCK_12_HOUR) {
    if (tmfs->hh < 1 || tmfs->hh > HOURS_PER_DAY / 2) {
      // hour must be between 1 and 12
      // errhint: Use the 24-hour clock, or give an hour between 1 and 12.
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "hour must be between 1 and 12 in 12-Hour Clock");
      return true;
    }

    ltime->hour = tmfs->hh;

    if (tmfs->pm != UNINITIALIZED_INT_IN_TMFS) {
      if (tmfs->pm && ltime->hour < HOURS_PER_DAY / 2) {
        ltime->hour += HOURS_PER_DAY / 2;
      } else if (!tmfs->pm && ltime->hour == HOURS_PER_DAY / 2) {
        ltime->hour = 0;
      }
    }

    return false;
  }

  /* below: In clock 24 hours */
  if (tmfs->pm != UNINITIALIZED_INT_IN_TMFS) {
    /* HH24 and meridian indicator(AM/PM/A.M./P.M.) conflict */
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "'HH24' precludes use of meridian indicator");
    return true;
  }

  if (tmfs->hh < 0 || tmfs->hh > HOURS_PER_DAY - 1) {
    // hour must be between 0 and 23
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "hour must be between 0 and 23 in 24-Hour Clock");
    return true;
  }

  ltime->hour = tmfs->hh;

  return false;
}

/*
 * Convert minute, second in TmFromStr to MYSQL_TIME
 *
 * tmfs(in):
 * ltime(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool convert_minute_second(TmFromStr *tmfs, MYSQL_TIME *ltime) {
  /* minute */
  if (tmfs->mi == UNINITIALIZED_INT_IN_TMFS) {
    ltime->minute = 0;
  } else {
    if (tmfs->mi < 0 || tmfs->mi >= MINS_PER_HOUR) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "minutes must be between 0 and 59");
      return true;
    }

    ltime->minute = tmfs->mi;
  }

  /* second */
  if (tmfs->ss == UNINITIALIZED_INT_IN_TMFS) {
    ltime->second = 0;
  } else {
    if (tmfs->ss < 0 || tmfs->ss >= SECS_PER_MINUTE) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "seconds must be between 0 and 59");
      return true;
    }

    ltime->second = tmfs->ss;
  }

  /*microsecond */
  if (tmfs->ms == UNINITIALIZED_INT_IN_TMFS) {
    ltime->second_part = 0;
  } else {
    if (tmfs->ms < 0 || tmfs->ms >= MICROSECS_PER_SEC) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "microseconds must be between 0 and 999999");
      return true;
    }

    ltime->second_part = tmfs->ms;
  }

  return false;
}

/*
 * Get the max days by year and month
 *
 * year(in):
 * month(in):
 *
 * return: max days
 */
int get_max_days_of_month(unsigned int year, unsigned int month) {
  DBUG_ASSERT(year > 0);
  DBUG_ASSERT(1 <= month && month <= MONTHS_PER_YEAR);

  if (year <= 0) {
    return 31;
  }

  if (!(1 <= month && month <= MONTHS_PER_YEAR)) {
    return 31;
  }

  return s_days_in_month[isleap(year)][month];
}

/*
 * Convert ddd in TmFromStr to MYSQL_TIME
 *
 * tmfs(in):
 * ltime(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool convert_ddd(TmFromStr *tmfs, MYSQL_TIME *ltime) {
  if (tmfs->ddd != UNINITIALIZED_INT_IN_TMFS) {
    if (tmfs->mm != UNINITIALIZED_INT_IN_TMFS) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "format code appears twice: MM");
      return true;
    }

    if (tmfs->dd != UNINITIALIZED_INT_IN_TMFS) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "format code appears twice: DD");
      return true;
    }

    int month = 0;
    int day = 0;

    if (process_ddd_in_year(ltime->year, tmfs->ddd, month, day)) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "day of year must be between 1 and 365 (366 for leap year)");
      return true;
    }

    tmfs->mm = month;
    tmfs->dd = day;
  }

  return false;
}

/*
 * Convert year, month, day in TmFromStr to MYSQL_TIME
 *
 * tmfs(in):
 * ltime(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool convert_year_month_day(TmFromStr *tmfs, MYSQL_TIME *ltime) {
  /* year */
  if (tmfs->year == UNINITIALIZED_INT_IN_TMFS) {
    ltime->year = get_current_year();
  } else {
    // year must be between 1(oracle is -4713) and +9999, and not be 0
    if (tmfs->year < 1 || tmfs->year > 9999) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "year must be between 1 and 9999");
      return true;
    }

    ltime->year = tmfs->year;
  }

  /* ddd */
  if (convert_ddd(tmfs, ltime)) return true;

  /* month */
  if (tmfs->mm == UNINITIALIZED_INT_IN_TMFS) {
    MYSQL_TIME ltime_tmp;
    get_current_datetime(&ltime_tmp);
    ltime->month = ltime_tmp.month;
  } else {
    if (tmfs->mm <= 0 || tmfs->mm > MONTHS_PER_YEAR) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0), "not a valid month");
      return true;
    }
    ltime->month = tmfs->mm;
  }

  /* day */
  if (tmfs->dd == UNINITIALIZED_INT_IN_TMFS) {
    ltime->day = 1;
  } else {
    if (tmfs->dd <= 0 || tmfs->dd > 31) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "day of month must be between 1 and last day of month");
      return true;
    }

    if (tmfs->dd > get_max_days_of_month(ltime->year, ltime->month)) {
      my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
               "date not valid for month specified");
      return true;
    }

    ltime->day = tmfs->dd;
  }

  return false;
}

/*
 * Convert TmFromStr to MYSQL_TIME
 *
 * tmfs(in):
 * ltime(out):
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool convert_tm_from_string(TmFromStr *tmfs, MYSQL_TIME *ltime) {
  if (convert_hour(tmfs, ltime)) {
    return true;
  }

  if (convert_minute_second(tmfs, ltime)) {
    return true;
  }

  if (convert_year_month_day(tmfs, ltime)) {
    return true;
  }

  return false;
}

/*
 * Check the space length of data string and format string.
 *
 * return:
 *   true  -- error
 *   false -- no error
 */
static bool check_space_len(LEX_STRING *date_string, LEX_STRING *fmt_string) {
  size_t date_space_len = strspace_len(date_string->str);
  size_t fmt_space_len = strspace_len(fmt_string->str);

  if (unlikely(date_string->length == 0 || fmt_string->length == 0)) {
    return true;  // no error. Return true means NULL value.
  }

  /*
   * date string is space string, and fmt string is space string
   * oracle: return current datetime.
   */
  if (unlikely(date_space_len == date_string->length &&
               fmt_space_len == fmt_string->length)) {
    return false;
  }

  /* date string is space string, and fmt string is not */
  if (unlikely(date_space_len == date_string->length &&
               fmt_space_len != fmt_string->length)) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "literal does not match format string");
    return true;
  }

  /* fmt string is space string, and date string is not */
  if (unlikely(date_space_len != date_string->length &&
               fmt_space_len == fmt_string->length)) {
    my_error(ER_CONVERT_DATETIME_FAIL, MYF(0),
             "literal does not match format string");
    return true;
  }

  /* 1. data, fmt string are both space string;
   * 2. neith nor.
   * Pass */
  return false;
}

/*
 * ora_extract_date_time: shared code for to_timestamp and to_date
 *
 * Parse the 'date_txt' according to 'fmt', return results as a struct
 * formatted_tm and fractional seconds.
 *
 * We parse 'fmt' into a list of FormatNodes, which is then passed to
 * process_DTF_from_str to populate a TmFromStr with the parsed contents of
 * 'date_txt'.
 *
 * The TmFromStr is then analysed and converted into the final results in
 * struct MYSQL_TIME.
 *
 * date_string(in): the str must end with '\0'
 * fmt_string(in): the str must end with '\0'
 * func_name(in): Which function use this function? to_date or to_timestamp
 * ltime(out):
 *
 * return: true  -- error
 *         false -- ok
 */
bool ora_extract_date_time(LEX_STRING *date_string, LEX_STRING *fmt_string,
                           const char *func_name, MYSQL_TIME *ltime) {
  FormatNode *format;
  size_t fmt_len;
  TmFromStr tmfs;
  bool has_err;

  DBUG_ASSERT(date_string != NULL);
  DBUG_ASSERT(fmt_string != NULL);
  DBUG_ASSERT(ltime != NULL);

  if (check_space_len(date_string, fmt_string)) {
    return true;
  }

  fmt_len = fmt_string->length;

  size_t mem_size = (fmt_len + 1) * sizeof(FormatNode);
  format = (FormatNode *)current_thd->alloc(mem_size);
  if (nullptr == format) {
    my_error(ER_OUTOFMEMORY, MYF(ME_FATALERROR), mem_size);
    return true;
  }

  has_err = parse_format(format, fmt_string, DTF_keywords, DTF_suff, DTF_index);
  if (has_err) {
    return true;
  }

  has_err = check_format(format, get_func_type(func_name));
  if (has_err) {
    return true;
  }

  has_err = process_DTF_from_str(format, date_string, &tmfs);
  if (has_err) {
    return true;
  }

  has_err = convert_tm_from_string(&tmfs, ltime);
  if (has_err) {
    return true;
  }

  return false;
}