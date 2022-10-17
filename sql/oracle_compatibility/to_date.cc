
#include "sql/sql_class.h"           // set_var.h: THD
#include "sql/set_var.h"
#include "sql/sql_locale.h"          // MY_LOCALE my_locale_en_US
#include "sql/strfunc.h"             // check_word
#include "sql/sql_time.h"

#include "m_ctype.h"
#include "m_string.h"                // strend

#include "sql/derror.h"
#include "sql/oracle_compatibility/convert_datetime.h"   // ora_extract_date_time
#include "sql/oracle_compatibility/to_date.h"

using std::min;
using std::max;

/* Get the value string and format string.
 *
 * out_val(out): the str in out_val must end with '\0'
 * out_fmt(out): the str in out_fmt must end with '\0'
 * return:
 *   true: has NULL arg.
 *         No error msg was set, so TO_DATE()/TO_TIMESTAMP() can return NULL.
 *   false: ok
 */
bool Item_func_oracle_to_date::get_args(LEX_STRING *out_val,
                                        LEX_STRING *out_fmt)
{
  char val_buf[64];
  char fmt_buf[64];

  String val_string(val_buf, sizeof(val_buf), &my_charset_bin);
  String fmt_string(fmt_buf, sizeof(fmt_buf), &my_charset_bin);
  String *val;
  String *fmt;

  char *c_fmt;
  size_t c_fmt_len;

  THD *thd = current_thd;

  /* val string */
  val = args[0]->val_str(&val_string);
  if (args[0]->null_value || nullptr == val) {
    /* when first arg is a field and it is NULL, the val is null */
    return true;
  }

  lex_string_strmake(thd->mem_root, out_val, val->ptr(), val->length());

  /* format string */
  if (arg_count == 1) {
    c_fmt = get_default_format();
    c_fmt_len = my_strnlen(c_fmt, SIZE_MAX - 1);
    lex_string_strmake(thd->mem_root, out_fmt, c_fmt, c_fmt_len);

  } else if (arg_count == 2) {
    fmt = args[1]->val_str(&fmt_string);
    if (args[1]->null_value || nullptr == fmt) {
      /* when second arg is a field and it is NULL, the fmt is null */
      return true;
    }

    c_fmt = fmt->ptr();
    c_fmt_len = fmt->length();

    lex_string_strmake(thd->mem_root, out_fmt, c_fmt, c_fmt_len);
  } else {
    DBUG_ASSERT(0);
  }

  return false;
}

bool Item_func_oracle_to_date::val_datetime(MYSQL_TIME *ltime,
                   my_time_flags_t fuzzy_date MY_ATTRIBUTE((unused)))
{
  LEX_STRING val_string = {nullptr, 0}; /* null_date part may use this */
  LEX_STRING fmt_string;

  if (get_args(&val_string, &fmt_string)) {
    goto null_date;
  }

  set_zero_time(ltime, cached_timestamp_type);

  if (ora_extract_date_time(&val_string, &fmt_string, func_name(), ltime)) {
    goto null_date;
  }

  null_value = false; /* set to not null value */

  return false;

null_date:
  if (val_string.str) { /*warnings*/
    char buff[128];
    strmake(buff, val_string.str, min<size_t>(val_string.length, sizeof(buff) - 1));
    push_warning_printf(current_thd, Sql_condition::SL_WARNING,
                        ER_WRONG_VALUE_FOR_TYPE,
                        ER_THD(current_thd, ER_WRONG_VALUE_FOR_TYPE),
                        "datetime", buff, func_name());
  }

  null_value = true;

  return true;
}

bool Item_func_oracle_to_date::resolve_type(THD *thd) {
  maybe_null = true;

  cached_timestamp_type = MYSQL_TIMESTAMP_DATETIME;
  set_data_type_datetime(0);

  sql_mode = thd->variables.sql_mode &
             (MODE_NO_ZERO_DATE | MODE_NO_ZERO_IN_DATE | MODE_INVALID_DATES);

  return false;
}

bool Item_func_oracle_to_timestamp::resolve_type(THD *thd) {
  maybe_null = true;

  cached_timestamp_type = MYSQL_TIMESTAMP_DATETIME;
  set_data_type_datetime(DATETIME_MAX_DECIMALS);

  sql_mode = thd->variables.sql_mode &
             (MODE_NO_ZERO_DATE | MODE_NO_ZERO_IN_DATE | MODE_INVALID_DATES);

  return false;
}
