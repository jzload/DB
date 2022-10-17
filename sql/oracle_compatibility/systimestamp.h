#ifndef ORACLE_COMPATIBILITY_SYSTIMESTAMP_H_52X36GDGSG2D3BG3B6S6SD5H3SD2G2SG366XXXX
#define ORACLE_COMPATIBILITY_SYSTIMESTAMP_H_52X36GDGSG2D3BG3B6S6SD5H3SD2G2SG366XXXX

#include "sql/item_timefunc.h"

class Item_func_now_systimestamp final : public Item_func_now
{
  typedef Item_func_now super;

protected:
  Time_zone *time_zone() override;

public:
  Item_func_now_systimestamp(const POS &pos, uint8 dec_arg);

  bool itemize(Parse_context *pc, Item **res) override;
  const char *func_name() const override { return "systimestamp"; }

  /*
   * The purpose of the following methods is to fully
   * implement the SYSTIMESTAMP function.
   * Not currently needed, for furture use.
   */
  // String *val_str(String *str) override;
  // void set_data_type_datetime(uint8 fsp) override;
  // enum Functype functype() const override { return NOW_FUNC; }

  /* Parse tree clone */
  void *clone(Parse_tree_transformer *ptt) const override;
};

#endif