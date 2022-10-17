#ifndef TO_CHAR_INCLUDED
#define TO_CHAR_INCLUDED

#include "sql/sql_list.h"
#include "sql/item_strfunc.h"


class Item_func_oracle_date_format final : public Item_str_func {
  MY_LOCALE *locale;
  int fixed_length;
  const bool is_time_format;
  bool is_fm;
  String value;
  uint m_ms_len;   // values of ffx's x
  String *pformat; // Pointer to a dynamically added format string

 public:

    Item_func_oracle_date_format(const POS &pos, Item *a, Item *b,
                               bool is_time_format_arg, uint ms_len, String *format_str) :
    Item_str_func(pos, a, b), is_time_format(is_time_format_arg), 
        m_ms_len(ms_len?ms_len:6), pformat(format_str)
    {}

  String *val_str(String *str);

  const char *func_name() const override {
    return is_time_format ? "time_format" : "date_format";
  }
  bool resolve_type(THD *) override;
  uint format_length(const String *format);
  bool eq(const Item *item, bool binary_cmp) const;
  bool to_char_make_date_time(Date_time_format *format, 
                 MYSQL_TIME *l_time, enum_mysql_timestamp_type type, String *str); 

  ~Item_func_oracle_date_format()
  {
     if (NULL != pformat)
        pformat->mem_free();
  }
  
};


class Item_func_to_char final : public Item_str_func
{
 public:
  Item_func_to_char(const POS &pos, Item *a, Item *b)
      : Item_str_func(pos, a, b) {
    collation.set(&my_charset_bin);
    max_length = MAX_FIELD_WIDTH;
  }

  Item_func_to_char(Item *a, Item *b)
      : Item_str_func(a,b) {
    collation.set(&my_charset_bin);
    max_length = MAX_FIELD_WIDTH;
  }

  Item_func_to_char(const POS &pos, Item *a)
      : Item_str_func(pos, a) {
    collation.set(&my_charset_bin);
    max_length = MAX_FIELD_WIDTH;
  }

  Item_func_to_char(Item *a)
      : Item_str_func(a) {
    collation.set(&my_charset_bin);
    max_length = MAX_FIELD_WIDTH;
  }

  String *val_str(String *str) override;
  bool resolve_type(THD *) override;

  const char *func_name() const override { return "to_char"; }
};


#endif
