#include "sql/item.h"
#include "sql/zsql_features/bug_fixes/bug_item_hex_string.h"

void Item_hex_string::clone_inner(const Item_hex_string *item) {
  max_length = item->max_length;
  const String *str = &item->str_value;
  str_value.set(str->ptr(),str->length(),str->charset());
  collation.set(item->collation);
  fixed = 1;
}

Item_hex_string::Item_hex_string(const Item_hex_string *item) {
  clone_inner(item);
  set_data_type(MYSQL_TYPE_VARCHAR);
  unsigned_flag = true;
}

Item_hex_string::Item_hex_string(const POS &pos,
                                 const Item_hex_string *item)
  : super(pos) {
  clone_inner(item);
  set_data_type(MYSQL_TYPE_VARCHAR);
  unsigned_flag = true;
}

Item_bin_string::Item_bin_string(const Item_bin_string *item) {
  clone_inner(item);
}

Item_bin_string::Item_bin_string(const POS &pos,
                                 const Item_bin_string *item)
  : super(pos) {
  clone_inner(item);
}