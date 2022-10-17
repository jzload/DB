//#include "sql_priv.h"
/*
  It is necessary to include set_var.h instead of item.h because there
  are dependencies on include order for set_var.h and item.h. This
  will be resolved later.
*/
#include "sql/sql_class.h"                          // set_var.h: THD
#include "sql/set_var.h"
#include "sql/sql_locale.h"          // MY_LOCALE my_locale_en_US
#include "sql/strfunc.h"             // check_word
#include "sql/sql_time.h"

#include "sql/tztime.h"              // struct Time_zone
#include "sql/sql_class.h"           // THD
#include <m_ctype.h>
#include <time.h>
#include "sql/oracle_compatibility/oracle_func.h"
#include "sql/oracle_compatibility/to_char.h"

using std::min;
using std::max;



/**
  Create a formated date/time value in a string.
*/

uint to_char_fm_full_length(bool is_fm, uint length, uint full_length)
{
    return is_fm ? length : full_length;
}

void to_char_make_datetime_ud(MYSQL_TIME *l_time,
                                      String *str, char *pbuff)
{
    uint length = (uint)(int10_to_str(l_time->day, pbuff, 10) - pbuff);

    str->append_with_prefill(pbuff, length, 1, '0');

    if (l_time->day >= 10 &&  l_time->day <= 19)
      str->append(STRING_WITH_LEN("th"));
    else
    {
      switch (l_time->day %10)
      {
        case 1:
          str->append(STRING_WITH_LEN("st"));
          break;
        case 2:
          str->append(STRING_WITH_LEN("nd"));
          break;
        case 3:
          str->append(STRING_WITH_LEN("rd"));
          break;
        default:
          str->append(STRING_WITH_LEN("th"));
          break;
      }
    }

    return;
}

bool Item_func_oracle_date_format::to_char_make_date_time(
                 Date_time_format *format, MYSQL_TIME *l_time,
                 enum_mysql_timestamp_type type, String *str)
{
  char intbuff[15];
  uint hours_i;
  uint weekday;
  uint length;
  size_t full_length;
  const char *ptr, *end;
  THD *thd = current_thd;
  MY_LOCALE *locale = thd->variables.lc_time_names;

  str->length(0);
  is_fm = false;

  if (l_time->neg)
    str->append('-');

  end = (ptr= format->format.str) + format->format.length;
  for (; ptr != end; ptr++) {
    if (*ptr != '%' || (ptr + 1) == end) {
      str->append(*ptr);
    } else {
      switch (*++ptr) {
        case 'M':
          if (!l_time->month)
            return 1;
          str->append(locale->month_names->type_names[l_time->month - 1],
                      my_strnlen(locale->month_names->type_names[l_time->month - 1],
                                 SIZE_MAX - 1),
                      system_charset_info);
          break;
        case 'b':
          if (!l_time->month)
            return 1;
          str->append(locale->ab_month_names->type_names[l_time->month - 1],
                   my_strnlen(locale->ab_month_names->type_names[l_time->month - 1],
                              SIZE_MAX - 1),
                   system_charset_info);
          break;
        case 'W':
          if (type == MYSQL_TIMESTAMP_TIME || !(l_time->month || l_time->year))
            return 1;
          weekday = calc_weekday(calc_daynr(l_time->year, l_time->month,
                                            l_time->day), 0);
          str->append(locale->day_names->type_names[weekday],
                      my_strnlen(locale->day_names->type_names[weekday],
                                 SIZE_MAX - 1),
                      system_charset_info);
          break;
        case 'a':
          if (type == MYSQL_TIMESTAMP_TIME || !(l_time->month || l_time->year))
            return 1;
          weekday = calc_weekday(calc_daynr(l_time->year, l_time->month,
                                            l_time->day), 0);
          str->append(locale->ab_day_names->type_names[weekday],
                      my_strnlen(locale->ab_day_names->type_names[weekday],
                                 SIZE_MAX - 1),
                      system_charset_info);
          break;
        case 'D':
          if (type == MYSQL_TIMESTAMP_TIME)
            return 1;
          to_char_make_datetime_ud(l_time, str, intbuff);
          break;
        case 'Y':
          length = (uint)(int10_to_str(l_time->year, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 4, '0');
          break;
        case 'y':
          length = (uint)(int10_to_str(l_time->year%100, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 2, '0');
          break;
        case 'm':
          length = (uint)(int10_to_str(l_time->month, intbuff, 10) - intbuff);
          full_length = to_char_fm_full_length(is_fm, length, 2);
          str->append_with_prefill(intbuff, length, full_length, '0');
          break;
        case 'c':
          length = (uint)(int10_to_str(l_time->month, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 1, '0');
          break;
        case 'd':
          length = (uint)(int10_to_str(l_time->day, intbuff, 10) - intbuff);
          full_length = to_char_fm_full_length(is_fm, length, 2);
          str->append_with_prefill(intbuff, length, full_length, '0');
          break;
        case 'e':
          length = (uint)(int10_to_str(l_time->day, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 1, '0');
          break;
        case 'f':
          length = (uint) (int10_to_str_ff6(l_time->second_part, intbuff) - intbuff);
          str->append_with_prefill_to_char_ff(intbuff, length, m_ms_len, '0');
          break;
        case 'F':
          is_fm = true;
          break;
        case 'H':
          length = (uint) (int10_to_str(l_time->hour, intbuff, 10) - intbuff);
          full_length = to_char_fm_full_length(is_fm, length, 2);
          str->append_with_prefill(intbuff, length, full_length, '0');
          break;
        case 'h':
        case 'I':
          hours_i = (l_time->hour % 24 + 11) % 12 + 1;
          length = (uint)(int10_to_str(hours_i, intbuff, 10) - intbuff);
          full_length = to_char_fm_full_length(is_fm, length, 2);
          str->append_with_prefill(intbuff, length, full_length, '0');
          break;
        case 'i':                 /* minutes */
          length = (uint)(int10_to_str(l_time->minute, intbuff, 10) - intbuff);
          full_length = to_char_fm_full_length(is_fm, length, 2);
          str->append_with_prefill(intbuff, length, full_length, '0');
          break;
        case 'j': {
          if (type == MYSQL_TIMESTAMP_TIME)
            return 1;

          int radix = 10;
          int diff = calc_daynr(l_time->year, l_time->month, l_time->day) -
                     calc_daynr(l_time->year, 1, 1) + 1;
          if (diff < 0)
            radix = -10;

          length = (uint)(int10_to_str(diff, intbuff, radix) - intbuff);
          str->append_with_prefill(intbuff, length, 3, '0');
          break;
        }
        case 'k':
          length = (uint)(int10_to_str(l_time->hour, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 1, '0');
          break;
        case 'l':
          hours_i = (l_time->hour % 24 + 11) % 12 + 1;
          length = (uint)(int10_to_str(hours_i, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 1, '0');
          break;
        case 'p':
          hours_i = l_time->hour % 24;
          str->append(hours_i < 12 ? "AM" : "PM", 2);
          break;
        case 'r':
          length = snprintf(intbuff, sizeof(intbuff),
                            ((l_time->hour % 24) < 12) ?
                                  "%02d:%02d:%02d AM" : "%02d:%02d:%02d PM",
                           (l_time->hour + 11) % 12 + 1,
                           l_time->minute,
                           l_time->second);
          str->append(intbuff, length);
          break;
        case 'S':
        case 's':
          length = (uint)(int10_to_str(l_time->second, intbuff, 10) - intbuff);
          full_length = to_char_fm_full_length(is_fm, length, 2);
          str->append_with_prefill(intbuff, length, full_length, '0');
          break;
        case 'T':
          length = snprintf(intbuff, sizeof(intbuff), "%02d:%02d:%02d",
                            l_time->hour, l_time->minute, l_time->second);
          str->append(intbuff, length);
          break;
        case 'U':
        case 'u': {
          uint year;
          if (type == MYSQL_TIMESTAMP_TIME)
            return 1;
          length =
              (uint)(int10_to_str(calc_week(*l_time,
                                      (*ptr) == 'U' ?
                                        WEEK_FIRST_WEEKDAY : WEEK_MONDAY_FIRST,
                                      &year),
                                  intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 2, '0');
          break;
        }
        case 'v':
        case 'V': {
          uint year;
          if (type == MYSQL_TIMESTAMP_TIME)
            return 1;
          length = (uint)(int10_to_str(calc_week(*l_time,
                                           ((*ptr) == 'V' ?
                                               (WEEK_YEAR | WEEK_FIRST_WEEKDAY) :
                                               (WEEK_YEAR | WEEK_MONDAY_FIRST)),
                                           &year),
                                       intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 2, '0');
          break;
        }
        case 'x':
        case 'X': {
          uint year;
          if (type == MYSQL_TIMESTAMP_TIME)
            return 1;
          (void) calc_week(*l_time,
                           ((*ptr) == 'X' ?
                                WEEK_YEAR | WEEK_FIRST_WEEKDAY :
                                WEEK_YEAR | WEEK_MONDAY_FIRST),
                           &year);
          length = (uint)(int10_to_str(year, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 4, '0');
          break;
        }
        case 'w':
          if (type == MYSQL_TIMESTAMP_TIME || !(l_time->month || l_time->year))
            return 1;
          weekday = calc_weekday(calc_daynr(l_time->year, l_time->month,
                                            l_time->day), 1);
          length = (uint)(int10_to_str(weekday + 1, intbuff, 10) - intbuff);
          str->append_with_prefill(intbuff, length, 1, '0');
          break;
        case 'q':
          length = (uint)(int10_to_str((l_time->month + 2) / 3, intbuff, 10) -
                          intbuff);
        default:
          str->append(*ptr);
          break;
      }
    }
  }
  return 0;
}

String *Item_func_to_char::val_str(String *str) {
  DBUG_ASSERT(nullptr != str);

  if (arg_count == 1) {
    str = args[0]->val_str(str);
    if ((null_value = args[0]->null_value)) {
      return NULL;
    }
    return str;
  }

  char buff0[MAX_FIELD_WIDTH] = {0};
  char buff1[MAX_FIELD_WIDTH] = {0};
  String tmp0(buff0, sizeof(buff0), &my_charset_bin);
  String tmp1(buff1, sizeof(buff1), &my_charset_bin);
  String *str_arg0, *str_arg1;
  str_arg0 = args[0]->val_str(&tmp0);
  str_arg1 = args[1]->val_str(&tmp1);

  /**
   Item_xxx::val_str(str) does not guarantee to return a pointer to the buffer
   of str (Some such as Item_string::val_str(), Item_field::val_str() invoking
   Field_string::val_str() or Field_varstring::val_str() return a pointer to
   their own internal buffer). args[0]->val_str() & args[1]->val_str() may have
   returned a string that we shouldn't modify (e.g. table field in queries).
   Hence, we need a local copy of the string, with its own buffer.
  */
  if (nullptr != str_arg0 && str_arg0 != &tmp0) {
    (void)tmp0.copy(*str_arg0);
    str_arg0 = &tmp0;
  }

  if (nullptr != str_arg1 && str_arg1 != &tmp1) {
    (void)tmp1.copy(*str_arg1);
    str_arg1 = &tmp1;
  }

  str->set_charset(collation.collation);
  str->length(0);

  bool ret = format_str_by_oracle_rule(str_arg0, str_arg1, str);
  if (ret) {
    null_value = 0;
    return str;
  } else {
    null_value = 1;
    return NULL;
  }
}

bool Item_func_to_char::resolve_type(THD *thd) {

  const CHARSET_INFO *cs = thd->variables.collation_connection;
  collation.set(cs, collation.derivation, collation.repertoire);

  maybe_null = true;
  return false;
}

bool Item_func_oracle_date_format::resolve_type(THD *thd) {
  /*
    Must use this_item() in case it's a local SP variable
    (for ->max_length and ->str_value)
  */
  Item *arg1 = args[1]->this_item();

  decimals = 0;
  const CHARSET_INFO *cs = thd->variables.collation_connection;
  uint32 repertoire = arg1->collation.repertoire;
  if (!thd->variables.lc_time_names->is_ascii)
    repertoire |= MY_REPERTOIRE_EXTENDED;
  collation.set(cs, arg1->collation.derivation, repertoire);
  if (arg1->type() == STRING_ITEM) {  // Optimize the normal case
    fixed_length = 1;
    max_length = format_length(arg1->get_str_value_addr()) *
                 collation.collation->mbmaxlen;
  } else {
    fixed_length = 0;
    max_length = min<uint32>(arg1->max_length, MAX_BLOB_WIDTH) * 10 *
                 collation.collation->mbmaxlen;
    set_if_smaller(max_length, MAX_BLOB_WIDTH);
  }
  maybe_null = 1;                 // If wrong date
  return false;
}

bool Item_func_oracle_date_format::eq(const Item *item, bool binary_cmp) const {
  if (item->type() != FUNC_ITEM)
    return 0;
  if (func_name() != down_cast<const Item_func *>(item)->func_name())
    return 0;
  if (this == item)
    return 1;

  const Item_func_oracle_date_format *item_func =
      down_cast<const Item_func_oracle_date_format *>(item);
  if (!args[0]->eq(item_func->args[0], binary_cmp))
    return 0;
  /*
    We must compare format string case sensitive.
    This needed because format modifiers with different case,
    for example %m and %M, have different meaning.
  */
  if (!args[1]->eq(item_func->args[1], 1))
    return 0;
  return 1;
}

uint Item_func_oracle_date_format::format_length(const String *format) {
  uint size = 0;
  const char *ptr=format->ptr();
  const char *end=ptr+format->length();

  for (; ptr != end ; ptr++) {
    if (*ptr != '%' || ptr == end-1)
      size++;
    else {
      switch(*++ptr) {
        case 'M': /* month, textual */
        case 'W': /* day (of the week), textual */
          size += 64; /* large for UTF8 locale data */
          break;
        case 'D': /* day (of the month), numeric plus english suffix */
        case 'Y': /* year, numeric, 4 digits */
        case 'x': /* Year, used with 'v' */
        case 'X': /* Year, used with 'v, where week starts with Monday' */
          size += 4;
          break;
        case 'a': /* locale's abbreviated weekday name (Sun..Sat) */
        case 'b': /* locale's abbreviated month name (Jan.Dec) */
          size += 32; /* large for UTF8 locale data */
          break;
        case 'j': /* day of year (001..366) */
          size += 3;
          break;
        case 'U': /* week (00..52) */
        case 'u': /* week (00..52), where week starts with Monday */
        case 'V': /* week 1..53 used with 'x' */
        case 'v': /* week 1..53 used with 'x', where week starts with Monday */
        case 'y': /* year, numeric, 2 digits */
        case 'm': /* month, numeric */
        case 'd': /* day (of the month), numeric */
        case 'h': /* hour (01..12) */
        case 'I': /* --||-- */
        case 'i': /* minutes, numeric */
        case 'l': /* hour ( 1..12) */
        case 'p': /* locale's AM or PM */
        case 'S': /* second (00..61) */
        case 's': /* seconds, numeric */
        case 'c': /* month (0..12) */
        case 'e': /* day (0..31) */
          size += 2;
          break;
        case 'k': /* hour ( 0..23) */
        case 'H': /* hour (00..23; value > 23 OK, padding always 2-digit) */
          size += 7; /* docs allow > 23, range depends on sizeof(unsigned int) */
          break;
        case 'r': /* time, 12-hour (hh:mm:ss [AP]M) */
          size += 11;
          break;
        case 'T': /* time, 24-hour (hh:mm:ss) */
          size += 8;
          break;
        case 'f': /* microseconds */
          size += 6;
          break;
        case 'w': /* day (of the week), numeric */
        case '%':
        default:
          size++;
          break;
      }
    }
  }
  return size;
}

String *Item_func_oracle_date_format::val_str(String *str) {
  String *format;
  MYSQL_TIME l_time;
  uint size;
  DBUG_ASSERT(fixed == 1);

  if (!is_time_format) {
    if (get_arg0_date(&l_time, TIME_FUZZY_DATE))
      return 0;
  } else {
    if (get_arg0_time(&l_time))
      return 0;
    l_time.year = l_time.month = l_time.day = 0;
  }

  if (!(format = args[1]->val_str(str)) || !format->length())
    goto null_date;

  if (fixed_length)
    size = max_length;
  else
    size = format_length(format);

  if (size < MAX_DATE_STRING_REP_LENGTH)
    size= MAX_DATE_STRING_REP_LENGTH;

  // If format uses the buffer provided by 'str' then store result locally.
  if (format == str || format->uses_buffer_owned_by(str))
    str = &value;
  if (str->alloc(size))
    goto null_date;

  Date_time_format date_time_format;
  date_time_format.format.str = (char*) format->ptr();
  date_time_format.format.length = format->length();

  /* Create the result string */
  str->set_charset(collation.collation);
  if (!to_char_make_date_time(&date_time_format, &l_time,
                      is_time_format ? MYSQL_TIMESTAMP_TIME :
                                       MYSQL_TIMESTAMP_DATE,
                      str))
    return str;

null_date:
  null_value = 1;
  return 0;
}
