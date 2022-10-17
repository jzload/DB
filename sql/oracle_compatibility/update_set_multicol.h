/**
  @file sql/oracle_compatibility/update_set_multicol.h

  @brief
  This file for update set (c1,c2) = subquery.
*/
#ifndef ORA_UPDATE_SET_MULTICOL_H
#define ORA_UPDATE_SET_MULTICOL_H
#include <unordered_map>
#include "sql/item.h"
class Item;
/** RowItemInfos record the row item and position like this:
   update t set
          (c1, c2) = (select 1, 2 from dual),
          c3 = 1,
          (c4, c5) = (select 3, 4 from dual),
          c6 = (select 1 from dual)
   where c1 = 1;
  then the RowItemInfos has map like this:
  (c1, c2) position is 0
  (c4, c5) postion is 2
 */
using RowItemInfos =  std::unordered_map<uint, Item*>;
using LIST_ITER = List_iterator_fast<Item>;
bool check_row_item (Item *item);
bool put_value_to_field(THD *thd, TABLE *table, Item *value, Item *fld, MY_BITMAP *bitmap,
                        MY_BITMAP *insert_into_fields_bitmap, bool raise_autoinc_has_expl_non_null_val);
bool fill_fields(THD *thd, TABLE *table, List<Item> &fields, List<Item> &values, MY_BITMAP *bitmap,
                 MY_BITMAP *insert_into_fields_bitmap, bool raise_autoinc_has_expl_non_null_val);
#endif