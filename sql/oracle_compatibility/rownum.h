#ifndef ROWNUM_H
#define ROWNUM_H

#include "sql/composite_iterators.h"
#include "sql/item.h"
#include "sql/item_func.h"
#include "sql/sql_optimizer.h"
#include "sql/oracle_compatibility/parse_tree_transformer.h"

class RownumIterator final : public RowIterator {
 public:
  RownumIterator(THD *thd,
                 unique_ptr_destroy_only<RowIterator> source,
                 Item *condition,
                 SELECT_LEX *select_lex);

  bool Init() override;

  int Read() override;

  std::vector<std::string> DebugString() const override;

  void SetNullRowFlag(bool is_null_row) override {
    m_source->SetNullRowFlag(is_null_row);
  }

  void StartPSIBatchMode() override { m_source->StartPSIBatchMode(); }
  void EndPSIBatchModeIfStarted() override {
    m_source->EndPSIBatchModeIfStarted();
  }

  void UnlockRow() override { m_source->UnlockRow(); }

  std::vector<Child> children() const override {
    return std::vector<Child>{{m_source.get(), ""}};
  }

 private:
  unique_ptr_destroy_only<RowIterator> m_source;
  Item *m_condition;
  SELECT_LEX *m_select;
};

class Item_func_rownum final : public Item_int_func {
  typedef Item_int_func super;
  SELECT_LEX *m_select;
public:
  Item_func_rownum(const POS &pos);

  Item_func_rownum(SELECT_LEX *select);

  bool itemize(Parse_context *pc, Item **res) override;

  const char *func_name() const override { return "rownum"; }

  longlong val_int() override;

  enum Functype functype() const override { return ROWNUM_FUNC; }

  /**
     Returns the pseudo tables depended upon in order to evaluate this
     function expression. The default implementation returns the empty
     set.

    We add RAND_TABLE_BIT to prevent moving this item from HAVING to WHERE.

    This function is non-deterministic and hence depends on the
    'RAND' pseudo-table.

    We use RAND_TABLE_BIT to prevent Item_func_rownum from
    being treated as a constant and precalculated before execution

    @retval Always RAND_TABLE_BIT
  */
  table_map get_initial_pseudo_tables() const override {
    return RAND_TABLE_BIT;
  }

  bool find_rownum_item(THD *, SELECT_LEX *, uint, bool &change_row_to_ref) override {
    change_row_to_ref = true;
    return false;
  }

  void print(const THD *thd, String *str, enum_query_type query_type) const override;
  void *clone(Parse_tree_transformer *ptt) const override;
};

bool extract_rownum_from_where_cond(THD *thd, Item **where_cond, Item **rownum_cond);

inline void increase_rownum_controller(SELECT_LEX *select)
{
  if (select->with_rownum)
  {
    select->m_Rownum_controller.increase_rownum();
  }
}

inline void reset_rownum_controller_flag(SELECT_LEX *select)
{
  if (select->with_rownum)
  {
    select->m_Rownum_controller.set_increased(false);
  }
}

int deal_with_rownum_in_single_table_dml(THD *thd, SELECT_LEX *const select_lex,
                                      QEP_TAB &qep_tab);
#endif // ROWNUM_H
