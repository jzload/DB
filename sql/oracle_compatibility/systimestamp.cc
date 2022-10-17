#include "systimestamp.h"
#include "sql/sql_lex.h"
#include "sql/sql_class.h"
#include "sql/tztime.h"

Item_func_now_systimestamp::Item_func_now_systimestamp(const POS &pos, uint8 dec_arg)
      : super(pos, dec_arg)
{
}

bool Item_func_now_systimestamp::itemize(Parse_context *pc, Item **res)
{
  if (skip_itemize(res)) return false;
  if (super::itemize(pc, res)) return true;

  pc->thd->lex->safe_to_cache_query = false;

  return false;
}

Time_zone *Item_func_now_systimestamp::time_zone()
{
  return current_thd->time_zone();
}

/*
 * Set item return type.
 * It should be string for Item::send to invoke val_str
 * @fsp : decimals, default 6.
 */
// void Item_func_now_systimestamp::set_data_type_datetime(uint8 fsp)
// {
//   set_data_type(MYSQL_TYPE_STRING);
//   collation.set_numeric();
//   decimals = fsp;
//   max_length = MAX_DATETIME_WIDTH + fsp + (fsp > 0 ? 1 : 0);
// }

// String *Item_func_now_systimestamp::val_str(String *str)
// {
//   DBUG_ASSERT(fixed);

//   MYSQL_TIME tm;

//   //TIME_FUZZY_DATE unused refer to "Item::send" function.
//   super::get_date(&tm, TIME_FUZZY_DATE);
//   super::val_str(str);

//   str_value.append(tm.hour % 24 < 12 ? " AM " : " PM ", 4);
//   str_value.append(*(this->time_zone()->get_name()));  //parameter: the string of timezone. Use current timezone "+08:00" now.

//   return &str_value;
// }