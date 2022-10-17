#ifndef TO_DATE_H
#define TO_DATE_H

#include "sql/sql_list.h"
#include "sql/item_strfunc.h"
#include "sql/item_timefunc.h"
#include "lex_string.h"
#include "my_time.h"

class Item_func_oracle_to_date : public Item_temporal_hybrid_func
{
public:
  enum_mysql_timestamp_type cached_timestamp_type;

private:
  bool get_args(LEX_STRING *out_val, LEX_STRING *out_fmt);

protected:
  bool val_datetime(MYSQL_TIME *ltime,
        my_time_flags_t fuzzy_date MY_ATTRIBUTE((unused))) override;

public:
  Item_func_oracle_to_date(const POS &pos, Item *a)
      : Item_temporal_hybrid_func(pos, a) {}
  Item_func_oracle_to_date(const POS &pos, Item *a, Item *b)
      : Item_temporal_hybrid_func(pos, a, b) {}

  virtual char *get_default_format() {
    return current_thd->variables.default_date_format;
  }

  const char *func_name() const override { return "to_date"; }
  bool resolve_type(THD *) override;
  virtual enum Item_result numeric_context_result_type() const override {
    return DATETIME_RESULT;
  }
};

class Item_func_oracle_to_timestamp final : public Item_func_oracle_to_date
{
public:
  Item_func_oracle_to_timestamp(const POS &pos, Item *a)
      : Item_func_oracle_to_date(pos, a) {}
  Item_func_oracle_to_timestamp(const POS &pos, Item *a, Item *b)
      : Item_func_oracle_to_date(pos, a, b) {}

  char *get_default_format() override {
    return current_thd->variables.default_timestamp_format;
  }

  const char *func_name() const override { return "to_timestamp"; }
  bool resolve_type(THD *) override;
};

#endif // TO_DATE_H
