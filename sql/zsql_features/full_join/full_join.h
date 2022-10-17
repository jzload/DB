#ifndef FULL_JOIN_H
#define FULL_JOIN_H

#include "sql/parse_tree_nodes.h"
#include "sql/parse_tree_items.h"

/*
 * PT_full_joined_table
 * Represent parse time full join table nodes.
 */
class PT_full_joined_table: public PT_joined_table
{
  typedef PT_joined_table super;
  Item *on;
  List<String> *using_fields;
  PT_derived_table *derived;

  /* In contextualize, call this to make the derived table. */
  bool make_derived_table(THD *thd);
  bool clone_members(THD *thd,
                     PT_table_reference **rep_tab1_node,
                     PT_table_reference **rep_tab2_node,
                     Item **rep_on,
                     List<String> **rep_using_fields);

public:
  /* PT_full_joined_table with on cond. */
  PT_full_joined_table(PT_table_reference *tab1_node_arg,
                       const POS &join_pos_arg,
                       PT_joined_table_type type,
                       PT_table_reference *tab2_node_arg,
                       Item *on_arg)
      : super(tab1_node_arg, join_pos_arg, type, tab2_node_arg),
        on(on_arg), using_fields(nullptr), derived(nullptr)
  {
    DBUG_ASSERT(on_arg || using_fields || (type | JTT_NATURAL));
  }
  /* PT_full_joined_table with using fields. */
  PT_full_joined_table(PT_table_reference *tab1_node_arg,
                       const POS &join_pos_arg,
                       PT_joined_table_type type,
                       PT_table_reference *tab2_node_arg,
                       List<String> *using_fields_arg)
      : super(tab1_node_arg, join_pos_arg, type, tab2_node_arg),
        on(nullptr), using_fields(using_fields_arg), derived(nullptr)
  {
    DBUG_ASSERT(on || using_fields || (type | JTT_NATURAL));
  }
  /* PT_full_joined_table with natural full join. */
  PT_full_joined_table(PT_table_reference *tab1_node_arg,
                       const POS &join_pos_arg,
                       PT_joined_table_type type,
                       PT_table_reference *tab2_node_arg)
      : super(tab1_node_arg, join_pos_arg, type, tab2_node_arg),
        on(nullptr), using_fields(nullptr), derived(nullptr)
  {
    DBUG_ASSERT(on || using_fields || (type | JTT_NATURAL));
  }
  /* contextualize is just contextualize the dirived table, and some settings. */
  bool contextualize(Parse_context *pc) override
  {
    if (make_derived_table(pc->thd) || derived->contextualize(pc))
      return true;
    value = derived->value;
    value->is_full_join_derived = true;
    pc->select->has_full_join = true;
    if (pc->select->check_duplicate_table_alias_for_full_join(pc->thd, value))
      return true;
    return false;
  }

  void *clone(Parse_tree_transformer *ptt) const override;
};

#endif /* FULL_JOIN_H*/