/**
  @file sql/oracle_compatibility/decode.h

  @brief
  This file defines decode function.
*/
#ifndef ITEM_FUNC_DECODE_INCLUDED
#define ITEM_FUNC_DECODE_INCLUDED

#include <stddef.h>
#include "m_ctype.h"
#include "sql/field.h"
#include "sql/item.h"
#include "sql/item_cmpfunc.h"
#include "sql_string.h"

class Item_func_decode final : public Item_func_case {

 public:
  Item_func_decode(const POS &pos, List<Item> &args_list)
    : Item_func_case(pos, args_list) {}

  const char *func_name() const override { return "decode"; }
};

#endif /* ITEM_FUNC_DECODE_INCLUDED */
