/**
  @file sql/oracle_compatibility/nvl.cc

  @brief
  This file defines nvl and nvl2 function.
*/
#include <stdio.h>

#include "sql/strfunc.h"
#include "sql/item_json_func.h"  // json_value
#include "sql/oracle_compatibility/nvl.h"


String *Item_func_nvl::str_op(String *str)
{
  DBUG_ASSERT(fixed == 1);

  String *args0 = args[0]->val_str(str);
  if (!args[0]->null_value && 0 != args0->length())
  {
      null_value = 0;
      args0->set_charset(collation.collation);
      return args0;
  }

  String *args1 = args[1]->val_str(str);
  if (args[1]->null_value || 0 == args1->length())
  {
    null_value = 1;
    return NULL;
  }
  else
  {
    null_value = 0;
    args1->set_charset(collation.collation);
    return args1;
  }
}

double Item_func_nvl2::val_real()
{
  DBUG_ASSERT(fixed == 1);

  String tem_value;
  args[0]->val_str(&tem_value); // update the value of 'args[0]->null_value'

  Item *item = (args[0]->null_value)? args[2] : args[1];
  double value = item->val_real();
  null_value = item->null_value;
  return value;
}

longlong Item_func_nvl2::val_int()
{
  DBUG_ASSERT(fixed == 1);

  String tem_value;
  args[0]->val_str(&tem_value); // update the value of 'args[0]->null_value'

  Item *item = (args[0]->null_value)? args[2] : args[1];
  longlong value = item->val_int();
  null_value = item->null_value;
  return value;
}

String *Item_func_nvl2::val_str(String *str)
{
  DBUG_ASSERT(fixed == 1);

  String tem_value;
  args[0]->val_str(&tem_value); // update the value of 'args[0]->null_value'

  Item *item = (args[0]->null_value) ? args[2] : args[1];
  String *res = item->val_str(str);
  if (item->null_value || res == NULL)
  {
    null_value = true;
    return (String *)NULL;
  }
  else
  {
    null_value = false;
    res->set_charset(collation.collation);
    return res;
  }
}

my_decimal *Item_func_nvl2::val_decimal(my_decimal *decimal_value)
{
  DBUG_ASSERT(fixed == 1);

  String tem_value;
  args[0]->val_str(&tem_value); // update the value of 'args[0]->null_value'

  Item *item = (args[0]->null_value)? args[2] : args[1];
  my_decimal *value = item->val_decimal(decimal_value);
  null_value = item->null_value;
  return value;
}

bool Item_func_nvl2::val_json(Json_wrapper *wr)
{
  DBUG_ASSERT(fixed == 1);

  String tem_value;
  args[0]->val_str(&tem_value); // update the value of 'args[0]->null_value'

  Item *arg = (args[0]->null_value)? args[2] : args[1];
  Item *args[] = {arg};
  bool ok = json_value(args, 0, wr);
  if (ok)
    return error_json();

  null_value = arg->null_value;
  return ok;
}

bool Item_func_nvl2::get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate)
{
  DBUG_ASSERT(fixed == 1);

  String tem_value;
  args[0]->val_str(&tem_value); // update the value of 'args[0]->null_value'

  Item *item = (args[0]->null_value)? args[2] : args[1];
  return (null_value = item->get_date(ltime, fuzzydate));
}

bool Item_func_nvl2::get_time(MYSQL_TIME *ltime)
{
  DBUG_ASSERT(fixed == 1);

  String tem_value;
  args[0]->val_str(&tem_value); // update the value of 'args[0]->null_value'

  Item *item = (args[0]->null_value)? args[2] : args[1];
  return (null_value = item->get_time(ltime));
}
