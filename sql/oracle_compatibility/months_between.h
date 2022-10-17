#ifndef MONTHS_BETWEEN_H
#define MONTHS_BETWEEN_H

#include "sql/item_func.h"

class Item_func_months_between final : public Item_real_func {
public:
  Item_func_months_between(const POS &pos, Item *a, Item *b)
      : Item_real_func(pos, a, b) {}

  const char *func_name() const override { return "months_between"; }

  double val_real() override;
};

#endif // MONTHS_BETWEEN_H
