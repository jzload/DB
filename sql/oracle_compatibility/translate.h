/**
  @file sql/oracle_compatibility/translate.h

  @brief
  This file defines translate function.
*/
#ifndef ITEM_STRFUNC_TRANSLATE_INCLUDED
#define ITEM_STRFUNC_TRANSLATE_INCLUDED

#include <stddef.h>
#include "m_ctype.h"
#include "sql/field.h"
#include "sql/item.h"
#include "sql/item_strfunc.h"
#include "sql_string.h"

class THD;

class Item_func_translate final : public Item_str_func {
  // Holds result in case we need to allocate our own result buffer.
  String tmp_value_res{"", 0, &my_charset_bin};

 public:
  Item_func_translate(const POS &pos, Item *arg1, Item *arg2, Item *arg3)
      : Item_str_func(pos, arg1, arg2, arg3) {}
  String *val_str(String *) override;
  bool resolve_type(THD *) override;
  const char *func_name() const override { return "translate"; }

 private:
  uint byte_len_per_char(const String *str, const char *ptr, const char *ptr_end);
  int find_in_from(const char *source_ptr, uint byte_len, const String *from_str);
  bool args_contain_null_str(const String *source_str, const String *from_str,
                            const String *to_str);
  bool args_contain_null_value(void);
};

#endif /* ITEM_STRFUNC_TRANSLATE_INCLUDED */
