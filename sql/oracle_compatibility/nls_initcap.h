#ifndef NLS_INITCAP_H
#define NLS_INITCAP_H

#include <boost/algorithm/string/trim.hpp>
#include "sql/item_strfunc.h"

#ifdef HAVE_ZSQL_INITCAP
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

#endif //HAVE_ZSQL_INITCAP

#endif //NLS_INITCAP_H
