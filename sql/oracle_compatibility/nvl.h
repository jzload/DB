/**
  @file sql/oracle_compatibility/nvl.h

  @brief
  This file defines nvl/nvl2 function.
*/
#ifndef ITEM_CMPFUNC_NVL_INCLUDED
#define ITEM_CMPFUNC_NVL_INCLUDED

#include <stddef.h>
#include "m_ctype.h"
#include "sql/field.h"
#include "sql/item.h"
#include "sql/item_cmpfunc.h"
#include "sql_string.h"

class Item_func_nvl final : public Item_func_ifnull {

 public:
  Item_func_nvl(const POS &pos, Item *a, Item *b)
      : Item_func_ifnull(pos, a, b) {}

  String *str_op(String *str) override;
  const char *func_name() const override { return "nvl"; }
};


class Item_func_nvl2 final : public Item_func_if {
 public:
  Item_func_nvl2(const POS &pos, Item *a, Item *b, Item *c)
      : Item_func_if(pos, a, b, c) {
    null_on_null = false;
  }

  double val_real() override;
  longlong val_int() override;
  String *val_str(String *str) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool val_json(Json_wrapper *wr) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
  const char *func_name() const override { return "nvl2"; }
};

#endif /* ITEM_CMPFUNC_NVL_INCLUDED */
