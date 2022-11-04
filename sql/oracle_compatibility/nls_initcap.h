#ifndef NLS_INITCAP_H
#define NLS_INITCAP_H

#include <boost/algorithm/string/trim.hpp>
#include "sql/item_strfunc.h"

#ifdef HAVE_ZSQL_NLS_INITCAP
class Item_func_oracle_nls_initcap : public Item_str_func
{
public:
  String m_temp_value;

public:
  Item_func_oracle_nls_initcap(const POS &pos, Item *a)
      : Item_str_func(pos, a) {}
  Item_func_oracle_nls_initcap(const POS &pos, Item *a, Item *b)
      : Item_str_func(pos, a, b) {}

  bool down_and_append(std::string *);
  bool format(String *);
  bool prepare_param(String *);
  String *initcap(String *&);
  bool match_nls(String *);

  String *val_str(String *) override;

  const char *func_name() const override { return "nls_initcap"; }
  bool resolve_type(THD *) override;
};
#endif

class Item_func_oracle_initcap final : public Item_str_func
{
  String *val_str(String *) override;

public:
  Item_func_oracle_initcap(const POS &pos, Item *a)
      : Item_str_func(pos, a) {}

  String *initcap(String *&);
  const char *func_name() const override { return "initcap"; }
  bool resolve_type(THD *) override;
};


#endif
