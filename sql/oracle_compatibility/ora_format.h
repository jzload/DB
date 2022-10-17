
#ifndef ORA_FORMAT_H
#define ORA_FORMAT_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ----------
 * Routines type
 * ----------
 */
#define DTF_TYPE        1        /* DATE-TIME Format version    */
#define NUM_TYPE        2        /* NUMBER version    */

/* KeyWord Index (ascii from position 32 (' ') to 126 (~))
 */
#define KeyWord_INDEX_SIZE          ('~' - ' ')
#define KeyWord_INDEX_FILTER(_c)    ((_c) <= ' ' || (_c) >= '~' ? 0 : 1)

/* the max length of the format keyword in TO_DATE, TO_TIMESTAMP, TO_NUMBER,
 * normal character */
const size_t MAX_MULTIBYTE_CHAR_LEN = 4;

/* the max size of DTF's item, for example, day name(Monday...)  */
const size_t DTF_MAX_ITEM_SIZE = 14;

/* -------------------------------
 *   Common structures of format
 * -------------------------------
 */

enum class SuffixType
{
  NONE = 0,
  PREFIX = 1,
  POSTFIX
};


/* KeySuffix: prefix or postfix
 */
struct KeySuffix
{
  const char       *name;           /* name of suffix        */
  size_t            len;            /* length of suffix      */
  int               id;             /* used in node->suffix  */
  SuffixType        type;           /* prefix / postfix type */
};

/* DateFormatMode
 *
 * This value is used to nominate one of several distinct (and mutually
 * exclusive) date conventions that a keyword can belong to.
 */
enum class DateFormatMode
{
  NONE = 0,        /* Value does not affect date mode. */
  GREGORIAN,       /* Gregorian (day, month, year) style date */
  ISOWEEK          /* ISO 8601 week date */
};

typedef struct
{
  const char     *name;      /* name of keyword */
  size_t          len;       /* length of keyword */
  int             id;        /* identifier, DTF_* or NUM_* */
  bool            is_digit;  /* the value is digit or not */
  DateFormatMode  date_mode; /* Used in date format. If it is other,
                                set to DateFormatMode::NONE */
} KeyWord;

enum class FormatNodeType
{
  END = 1,    /* end of format tree */
  ACTION,     /* action type means this format need process its value */
  CHAR,       /* normal character */
  /*
   * String enclosed in double quotes.
   * 双引号中的字符必须完全一样（除了大小写）
   * oracle 不支持下面语法
   * select to_date('ba2010', '"a-"yyyy');
   */
  STR
};

enum class NumBaseType {
  DEC = 10, /* Decimal:10-base type number */
  HEX = 16  /* Hexadecimal:16-base type number */
};

typedef struct
{
  const char      *symbol;    /* name of the currency */
  size_t           len;      /* lenth of the currency symbol */
} CurrencyType;

typedef struct
{
  FormatNodeType  type;           /* NODE_TYPE_XXX, see below */
  const KeyWord   *key;            /* if type is ACTION */
  char character[MAX_MULTIBYTE_CHAR_LEN + 1];    /* if type is CHAR */
  int             suffix;         /* keyword prefix/suffix code, if any */
  int             precision;      /* for Keyword FF:how many bits in the microseconds */
} FormatNode;


/* ------------------------------------------------
 *   KeyWord definitions (DTF: Date time format)
 * ------------------------------------------------
 */

/*
 * Suffixes
 */
enum DTF_SUFFIX
{
  DTF_S_none_ = 0x0,

  DTF_S_FM    = 0x1,
  DTF_S_TH    = (0x1 << 1),
  DTF_S_th    = (0x1 << 2),
  DTF_S_SP    = (0x1 << 3),
  DTF_S_TM    = (0x1 << 4),
};

/*
 * Suffix tests
 */
#define S_THth(_s)     ((((_s) & DTF_S_TH) || ((_s) & DTF_S_th)) ? 1 : 0)
#define S_TH(_s)       (((_s) & DTF_S_TH) ? 1 : 0)
#define S_th(_s)       (((_s) & DTF_S_th) ? 1 : 0)
#define S_TH_TYPE(_s)  (((_s) & DTF_S_TH) ? TH_UPPER : TH_LOWER)

/* Oracle toggles FM behavior, we don't. */
#define S_FM(_s)    (((_s) & DTF_S_FM) ? 1 : 0)
#define S_SP(_s)    (((_s) & DTF_S_SP) ? 1 : 0)
#define S_TM(_s)    (((_s) & DTF_S_TM) ? 1 : 0)

/*
 * Suffixes definition for DATE-TIME
 */
static const KeySuffix DTF_suff[] =
{
  {"FM", 2, DTF_S_FM, SuffixType::PREFIX},
  {"fm", 2, DTF_S_FM, SuffixType::PREFIX},
  {"TM", 2, DTF_S_TM, SuffixType::PREFIX},
  {"tm", 2, DTF_S_TM, SuffixType::PREFIX},
  {"TH", 2, DTF_S_TH, SuffixType::POSTFIX},
  {"th", 2, DTF_S_th, SuffixType::POSTFIX},
  {"SP", 2, DTF_S_SP, SuffixType::POSTFIX},

  /* last */
  {NULL, 0, DTF_S_none_, SuffixType::NONE}
};

enum DTF
{
  DTF_A_D,
  DTF_A_M,
  DTF_AD,
  DTF_AM,
  DTF_B_C,
  DTF_BC,
  DTF_CC,
  DTF_DAY,
  DTF_DDD,
  DTF_DD,
  DTF_DY,
  DTF_Day,
  DTF_Dy,
  DTF_D,
  DTF_FF,
  DTF_FX,                   /* global suffix */
  DTF_HH24,
  DTF_HH12,
  DTF_HH,
  DTF_IDDD,
  DTF_ID,
  DTF_IW,
  DTF_IYYY,
  DTF_IYY,
  DTF_IY,
  DTF_I,
  DTF_J,
  DTF_MI,
  DTF_MM,
  DTF_MONTH,
  DTF_MON,
  DTF_MS,
  DTF_Month,
  DTF_Mon,
  DTF_OF,
  DTF_P_M,
  DTF_PM,
  DTF_Q,
  DTF_RM,
  DTF_RRRR,
  DTF_RR,
  DTF_SSSS,
  DTF_SS,
  DTF_TZH,
  DTF_TZM,
  DTF_TZ,
  DTF_US,
  DTF_WW,
  DTF_W,
  DTF_Y_YYY,
  DTF_YYYY,
  DTF_YYY,
  DTF_YY,
  DTF_Y,
  DTF_a_d,
  DTF_a_m,
  DTF_ad,
  DTF_am,
  DTF_b_c,
  DTF_bc,
  DTF_cc,
  DTF_day,
  DTF_ddd,
  DTF_dd,
  DTF_dy,
  DTF_d,
  DTF_ff,
  DTF_fx,
  DTF_hh24,
  DTF_hh12,
  DTF_hh,
  DTF_iddd,
  DTF_id,
  DTF_iw,
  DTF_iyyy,
  DTF_iyy,
  DTF_iy,
  DTF_i,
  DTF_j,
  DTF_mi,
  DTF_mm,
  DTF_month,
  DTF_mon,
  DTF_ms,
  DTF_p_m,
  DTF_pm,
  DTF_q,
  DTF_rm,
  DTF_rrrr,
  DTF_rr,
  DTF_ssss,
  DTF_ss,
  DTF_tz,
  DTF_us,
  DTF_ww,
  DTF_w,
  DTF_y_yyy,
  DTF_yyyy,
  DTF_yyy,
  DTF_yy,
  DTF_y,

  /* last */
  DTF_last_
};

typedef enum
{
    NUM_COMMA,
    NUM_DEC,
    NUM_DOLLAR,
    NUM_0,
    NUM_9,
    NUM_B,
    NUM_C,
    NUM_D,
    NUM_E,
    NUM_FM,
    NUM_G,
    NUM_L,
    NUM_MI,
    NUM_PL,
    NUM_PR,
    NUM_RN,
    NUM_SP,
    NUM_S,
    NUM_TH,
    NUM_U,
    NUM_V,
    NUM_X,
    NUM_b,
    NUM_c,
    NUM_d,
    NUM_e,
    NUM_fm,
    NUM_g,
    NUM_l,
    NUM_mi,
    NUM_pl,
    NUM_pr,
    NUM_rn,
    NUM_sp,
    NUM_s,
    NUM_th,
    NUM_u,
    NUM_v,
    NUM_x,

    /* last */
    _NUM_last_
} NUM_poz;

/*
 * KeyWords for DATE-TIME version
 */
static const KeyWord DTF_keywords[] =
{
  /* name,  len, id,      is_digit, date_mode */
  {"A.D.",  4, DTF_A_D,   false, DateFormatMode::NONE},      /* A */
  {"A.M.",  4, DTF_A_M,   false, DateFormatMode::NONE},
  {"AD",    2, DTF_AD,    false, DateFormatMode::NONE},
  {"AM",    2, DTF_AM,    false, DateFormatMode::NONE},
  {"B.C.",  4, DTF_B_C,   false, DateFormatMode::NONE},      /* B */
  {"BC",    2, DTF_BC,    false, DateFormatMode::NONE},
  {"CC",    2, DTF_CC,    true,  DateFormatMode::NONE},      /* C */
  {"DAY",   3, DTF_DAY,   false, DateFormatMode::NONE},      /* D */
  {"DDD",   3, DTF_DDD,   true,  DateFormatMode::GREGORIAN},
  {"DD",    2, DTF_DD,    true,  DateFormatMode::GREGORIAN},
  {"DY",    2, DTF_DY,    false, DateFormatMode::NONE},
  {"Day",   3, DTF_Day,   false, DateFormatMode::NONE},
  {"Dy",    2, DTF_Dy,    false, DateFormatMode::NONE},
  {"D",     1, DTF_D,     true,  DateFormatMode::GREGORIAN},
  {"FF",    2, DTF_FF,    true,  DateFormatMode::NONE},      /* F */
  {"FX",    2, DTF_FX,    false, DateFormatMode::NONE},
  {"HH24",  4, DTF_HH24,  true,  DateFormatMode::NONE},      /* H */
  {"HH12",  4, DTF_HH12,  true,  DateFormatMode::NONE},
  {"HH",    2, DTF_HH,    true,  DateFormatMode::NONE},
  {"IDDD",  4, DTF_IDDD,  true,  DateFormatMode::ISOWEEK},   /* I */
  {"ID",    2, DTF_ID,    true,  DateFormatMode::ISOWEEK},
  {"IW",    2, DTF_IW,    true,  DateFormatMode::ISOWEEK},
  {"IYYY",  4, DTF_IYYY,  true,  DateFormatMode::ISOWEEK},
  {"IYY",   3, DTF_IYY,   true,  DateFormatMode::ISOWEEK},
  {"IY",    2, DTF_IY,    true,  DateFormatMode::ISOWEEK},
  {"I",     1, DTF_I,     true,  DateFormatMode::ISOWEEK},
  {"J",     1, DTF_J,     true,  DateFormatMode::NONE},      /* J */
  {"MI",    2, DTF_MI,    true,  DateFormatMode::NONE},      /* M */
  {"MM",    2, DTF_MM,    true,  DateFormatMode::GREGORIAN},
  {"MONTH", 5, DTF_MONTH, false, DateFormatMode::GREGORIAN},
  {"MON",   3, DTF_MON,   false, DateFormatMode::GREGORIAN},
  {"MS",    2, DTF_MS,    true,  DateFormatMode::NONE},
  {"Month", 5, DTF_Month, false, DateFormatMode::GREGORIAN},
  {"Mon",   3, DTF_Mon,   false, DateFormatMode::GREGORIAN},
  {"OF",    2, DTF_OF,    false, DateFormatMode::NONE},      /* O */
  {"P.M.",  4, DTF_P_M,   false, DateFormatMode::NONE},      /* P */
  {"PM",    2, DTF_PM,    false, DateFormatMode::NONE},
  {"Q",     1, DTF_Q,     true,  DateFormatMode::NONE},      /* Q */
  {"RM",    2, DTF_RM,    false, DateFormatMode::GREGORIAN}, /* R */
  {"RRRR",  4, DTF_RRRR,  true,  DateFormatMode::GREGORIAN},
  {"RR",    2, DTF_RR,    true,  DateFormatMode::GREGORIAN},
  {"SSSS",  4, DTF_SSSS,  true,  DateFormatMode::NONE},      /* S */
  {"SS",    2, DTF_SS,    true,  DateFormatMode::NONE},
  {"TZH",   3, DTF_TZH,   false, DateFormatMode::NONE},      /* T */
  {"TZM",   3, DTF_TZM,   true,  DateFormatMode::NONE},
  {"TZ",    2, DTF_TZ,    false, DateFormatMode::NONE},
  {"US",    2, DTF_US,    true,  DateFormatMode::NONE},      /* U */
  {"WW",    2, DTF_WW,    true,  DateFormatMode::GREGORIAN}, /* W */
  {"W",     1, DTF_W,     true,  DateFormatMode::GREGORIAN},
  {"Y,YYY", 5, DTF_Y_YYY, true,  DateFormatMode::GREGORIAN}, /* Y */
  {"YYYY",  4, DTF_YYYY,  true,  DateFormatMode::GREGORIAN},
  {"YYY",   3, DTF_YYY,   true,  DateFormatMode::GREGORIAN},
  {"YY",    2, DTF_YY,    true,  DateFormatMode::GREGORIAN},
  {"Y",     1, DTF_Y,     true,  DateFormatMode::GREGORIAN},
  {"a.d.",  4, DTF_a_d,   false, DateFormatMode::NONE},      /* a */
  {"a.m.",  4, DTF_a_m,   false, DateFormatMode::NONE},
  {"ad",    2, DTF_ad,    false, DateFormatMode::NONE},
  {"am",    2, DTF_am,    false, DateFormatMode::NONE},
  {"b.c.",  4, DTF_b_c,   false, DateFormatMode::NONE},      /* b */
  {"bc",    2, DTF_bc,    false, DateFormatMode::NONE},
  {"cc",    2, DTF_CC,    true,  DateFormatMode::NONE},      /* c */
  {"day",   3, DTF_day,   false, DateFormatMode::NONE},      /* d */
  {"ddd",   3, DTF_DDD,   true,  DateFormatMode::GREGORIAN},
  {"dd",    2, DTF_DD,    true,  DateFormatMode::GREGORIAN},
  {"dy",    2, DTF_dy,    false, DateFormatMode::NONE},
  {"d",     1, DTF_D,     true,  DateFormatMode::GREGORIAN},
  {"ff",    2, DTF_FF,    true,  DateFormatMode::NONE},      /* f */
  {"fx",    2, DTF_FX,    false, DateFormatMode::NONE},
  {"hh24",  4, DTF_HH24,  true,  DateFormatMode::NONE},      /* h */
  {"hh12",  4, DTF_HH12,  true,  DateFormatMode::NONE},
  {"hh",    2, DTF_HH,    true,  DateFormatMode::NONE},
  {"iddd",  4, DTF_IDDD,  true,  DateFormatMode::ISOWEEK},   /* i */
  {"id",    2, DTF_ID,    true,  DateFormatMode::ISOWEEK},
  {"iw",    2, DTF_IW,    true,  DateFormatMode::ISOWEEK},
  {"iyyy",  4, DTF_IYYY,  true,  DateFormatMode::ISOWEEK},
  {"iyy",   3, DTF_IYY,   true,  DateFormatMode::ISOWEEK},
  {"iy",    2, DTF_IY,    true,  DateFormatMode::ISOWEEK},
  {"i",     1, DTF_I,     true,  DateFormatMode::ISOWEEK},
  {"j",     1, DTF_J,     true,  DateFormatMode::NONE},      /* j */
  {"mi",    2, DTF_MI,    true,  DateFormatMode::NONE},      /* m */
  {"mm",    2, DTF_MM,    true,  DateFormatMode::GREGORIAN},
  {"month", 5, DTF_month, false, DateFormatMode::GREGORIAN},
  {"mon",   3, DTF_mon,   false, DateFormatMode::GREGORIAN},
  {"ms",    2, DTF_MS,    true,  DateFormatMode::NONE},
  {"p.m.",  4, DTF_p_m,   false, DateFormatMode::NONE},      /* p */
  {"pm",    2, DTF_pm,    false, DateFormatMode::NONE},
  {"q",     1, DTF_Q,     true,  DateFormatMode::NONE},      /* q */
  {"rm",    2, DTF_rm,    false, DateFormatMode::GREGORIAN}, /* r */
  {"rrrr",  4, DTF_RRRR,  true,  DateFormatMode::GREGORIAN},
  {"rr",    2, DTF_RR,    true,  DateFormatMode::GREGORIAN},
  {"ssss",  4, DTF_SSSS,  true,  DateFormatMode::NONE},      /* s */
  {"ss",    2, DTF_SS,    true,  DateFormatMode::NONE},
  {"tz",    2, DTF_tz,    false, DateFormatMode::NONE},      /* t */
  {"us",    2, DTF_US,    true,  DateFormatMode::NONE},      /* u */
  {"ww",    2, DTF_WW,    true,  DateFormatMode::GREGORIAN}, /* w */
  {"w",     1, DTF_W,     true,  DateFormatMode::GREGORIAN},
  {"y,yyy", 5, DTF_Y_YYY, true,  DateFormatMode::GREGORIAN}, /* y */
  {"yyyy",  4, DTF_YYYY,  true,  DateFormatMode::GREGORIAN},
  {"yyy",   3, DTF_YYY,   true,  DateFormatMode::GREGORIAN},
  {"yy",    2, DTF_YY,    true,  DateFormatMode::GREGORIAN},
  {"y",     1, DTF_Y,     true,  DateFormatMode::GREGORIAN},

  /* last */
  {NULL,    0, DTF_last_,  false, DateFormatMode::NONE}
};

/* ----------
 * KeyWords for NUMBER version
 *
 * The is_digit and date_mode fields are not relevant here.
 * ----------
 */
static const KeyWord NUM_keywords[] = {
    /* name, len, id       is in Index */
  {",",    1, NUM_COMMA,  true, DateFormatMode::NONE},  /* , */
  {".",    1, NUM_DEC,    true, DateFormatMode::NONE},  /* . */
  {"$",    1, NUM_DOLLAR, true, DateFormatMode::NONE},  /* $ */
  {"0",    1, NUM_0,      true, DateFormatMode::NONE},  /* 0 */
  {"9",    1, NUM_9,      true, DateFormatMode::NONE},  /* 9 */
  {"B",    1, NUM_B,      true, DateFormatMode::NONE},  /* B */
  {"C",    1, NUM_C,      true, DateFormatMode::NONE},  /* C */
  {"D",    1, NUM_D,      true, DateFormatMode::NONE},  /* D */
  {"EEEE", 4, NUM_E,      true, DateFormatMode::NONE},  /* E */
  {"FM",   2, NUM_FM,     true, DateFormatMode::NONE},  /* F */
  {"G",    1, NUM_G,      true, DateFormatMode::NONE},  /* G */
  {"L",    1, NUM_L,      true, DateFormatMode::NONE},  /* L */
  {"MI",   2, NUM_MI,     true, DateFormatMode::NONE},  /* M */
  {"PL",   2, NUM_PL,     true, DateFormatMode::NONE},  /* P */
  {"PR",   2, NUM_PR,     true, DateFormatMode::NONE},
  {"RN",   2, NUM_RN,     true, DateFormatMode::NONE},  /* R */
  {"SP",   2, NUM_SP,     true, DateFormatMode::NONE},  /* S */
  {"S",    1, NUM_S,      true, DateFormatMode::NONE},
  {"TH",   2, NUM_TH,     true, DateFormatMode::NONE},  /* T */
  {"U",    1, NUM_U,      true, DateFormatMode::NONE},  /* U */
  {"V",    1, NUM_V,      true, DateFormatMode::NONE},  /* V */
  {"X",    1, NUM_X,      true, DateFormatMode::NONE},  /* X */
  {"b",    1, NUM_B,      true, DateFormatMode::NONE},  /* b */
  {"c",    1, NUM_C,      true, DateFormatMode::NONE},  /* c */
  {"d",    1, NUM_D,      true, DateFormatMode::NONE},  /* d */
  {"eeee", 4, NUM_E,      true, DateFormatMode::NONE},  /* e */
  {"fm",   2, NUM_FM,     true, DateFormatMode::NONE},  /* f */
  {"g",    1, NUM_G,      true, DateFormatMode::NONE},  /* g */
  {"l",    1, NUM_L,      true, DateFormatMode::NONE},  /* l */
  {"mi",   2, NUM_MI,     true, DateFormatMode::NONE},  /* m */
  {"pl",   2, NUM_PL,     true, DateFormatMode::NONE},  /* p */
  {"pr",   2, NUM_PR,     true, DateFormatMode::NONE},
  {"rn",   2, NUM_rn,     true, DateFormatMode::NONE},  /* r */
  {"sp",   2, NUM_SP,     true, DateFormatMode::NONE},  /* s */
  {"s",    1, NUM_S,      true, DateFormatMode::NONE},
  {"th",   2, NUM_th,     true, DateFormatMode::NONE},  /* t */
  {"u",    1, NUM_U,      true, DateFormatMode::NONE},  /* u */
  {"v",    1, NUM_V,      true, DateFormatMode::NONE},  /* v */
  {"x",    1, NUM_X,      true, DateFormatMode::NONE},  /* x */
    
    /* last */
  {NULL,   0, _NUM_last_, true, DateFormatMode::NONE}};

/*
 * KeyWords index for DATE-TIME version
 */
static const int DTF_index[KeyWord_INDEX_SIZE] =
{
  /*
  0    1    2    3    4    5    6    7    8    9
  */
  /*---- first 0..31 chars are skipped ----*/

  -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, DTF_A_D, DTF_B_C, DTF_CC, DTF_DAY, -1,
  DTF_FF, -1, DTF_HH24, DTF_IDDD, DTF_J, -1, -1, DTF_MI, -1, DTF_OF,
  DTF_P_M, DTF_Q, DTF_RM, DTF_SSSS, DTF_TZH, DTF_US, -1, DTF_WW, -1, DTF_Y_YYY,
  -1, -1, -1, -1, -1, -1, -1, DTF_a_d, DTF_b_c, DTF_cc,
  DTF_day, -1, DTF_ff, -1, DTF_hh24, DTF_iddd, DTF_j, -1, -1, DTF_mi,
  -1, -1, DTF_p_m, DTF_q, DTF_rm, DTF_ssss, DTF_tz, DTF_us, -1, DTF_ww,
  -1, DTF_y_yyy, -1, -1, -1, -1

  /*---- chars over 126 are skipped ----*/
};

/* ----------
 * KeyWords index for NUMBER version
 * ----------
 */
static const int NUM_index[KeyWord_INDEX_SIZE] = {
    /*
    0    1    2    3    4    5    6    7    8    9
    */
    /*---- first 0..31 chars are skipped ----*/

    -1,     -1,    -1,     -1,     NUM_DOLLAR, -1,     -1,      -1,
    -1,     -1,    -1,     -1,     NUM_COMMA,  -1,     NUM_DEC, -1,
    NUM_0,  -1,    -1,     -1,     -1,         -1,     -1,      -1,
    -1,     NUM_9, -1,     -1,     -1,         -1,     -1,      -1,
    -1,     -1,    NUM_B,  NUM_C,  NUM_D,      NUM_E,  NUM_FM,  NUM_G,
    -1,     -1,    -1,     -1,     NUM_L,      NUM_MI, -1,      -1,
    NUM_PL, -1,    NUM_RN, NUM_SP, NUM_TH,     NUM_U,  NUM_V,   -1,
    NUM_X,  -1,    -1,     -1,     -1,         -1,     -1,      -1,
    -1,     -1,    NUM_b,  NUM_c,  NUM_d,      NUM_e,  NUM_fm,  NUM_g,
    -1,     -1,    -1,     -1,     NUM_l,      NUM_mi, -1,      -1,
    NUM_pl, -1,    NUM_rn, NUM_sp, NUM_th,     NUM_u,  NUM_v,   -1,
    NUM_x,  -1,    -1,     -1,     -1,         -1

    /*---- chars over 126 are skipped ----*/
};

/* ----------
 * CurrencyType Ararry for NUMBER version
 * ----------
 */
static const CurrencyType Currency_symbol[]=
{
  {"$",     1},
  {"USD",   3},
  
  /* last */
  {NULL,    0}
};


/* ---------------------------------
 *   Functions
 * ---------------------------------
 */

extern const KeyWord *index_seq_search(const char *str, const KeyWord *kw,
                                       const int *index);

extern const KeySuffix *suff_search(const char *str, const KeySuffix *suf,
                                    SuffixType type);


#endif // ORA_FORMAT_H
