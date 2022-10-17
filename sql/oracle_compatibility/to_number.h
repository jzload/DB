#ifndef TO_NUMBER_H
#define TO_NUMBER_H

#include "sql/item_strfunc.h"
#include "sql/sql_list.h"

#define TO_NUM_DEF_LEN 65
#define TO_NUM_DEF_DEC 30
class Item_func_to_number final : public Item_func {
 public:
  Item_func_to_number(const POS &pos, Item *a) : Item_func(pos, a) {
    set_data_type_decimal(TO_NUM_DEF_LEN, TO_NUM_DEF_DEC);
    set_reset_decimal_flag(NEED_RESET_DEC);
    set_item_option(HAVE_TO_NUMBER_ITEM);
  }

  Item_func_to_number(const POS &pos, Item *a, Item *b) : Item_func(pos, a, b) {
    set_data_type_decimal(TO_NUM_DEF_LEN, TO_NUM_DEF_DEC);
    set_reset_decimal_flag(NEED_RESET_DEC);
    set_item_option(HAVE_TO_NUMBER_ITEM);
  }

  String *val_str(String *str) override;
  double val_real() override;
  longlong val_int() override;
  my_decimal *val_decimal(my_decimal *) override;

  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override {
    return get_date_from_decimal(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override {
    return get_time_from_decimal(ltime);
  }
  enum Item_result result_type() const override { return DECIMAL_RESULT; }
  bool resolve_type(THD *) override { return false; }
  const char *func_name() const override { return "to_number"; }

  bool is_decimal_value_null(my_decimal *tmp);
  bool is_str_value_null(String *pStr);
  bool check_two_params_null(String *fmt_str);
  my_decimal *get_decimal_value(my_decimal *tmp, my_decimal *dec);
  String *get_value_param_str(String *arg_str);
  String *get_format_param_str(String *arg_str);
  my_decimal *val_dec_one_param(my_decimal *);
  my_decimal *val_dec_two_params(my_decimal *);
  /**
   In this case:
   the input parameters is table fields, if the middle value in one
   field is null, then the null_value will not be reset and will come
   with output we not expected. To avoid this, we need reset the item.
   */
  void reset_item() { null_value = false; };
};

#endif  // TO_NUMBER_H
