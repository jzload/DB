/*
 * FILE:    full_join.cc
 * USAGE:   Implementation of full join.
 * 
 * DESCRIPTION:
 * 
 * Currently, we implement full join based on the server version 8.0.18.
 * At this time, the executor has not been fully refactored, implementations based
 * on optimizer and executor modifications are not possible. This is because that
 * we can't avoid pre-iterator execution in current state, which is based on nested
 * loop recursion. So we implement through modifications of the parse tree just
 * after the tree building, but before contexualization.
 * 
 * The implementation is discribed briefly here:
 * 
 * 1. PARSER PHASE:
 * 
 * If we have an income query
 *                    SELECT * FROM t1 FULL JOIN t2 ON t1.c1 = t2.c2,
 * we are to transform it to
 *             SELECT * FROM (SELECT * FROM t1 ANTI JOIN t2 ON t1.c1 = t2.c2
 *                            UNION ALL
 *                            SELECT * FROM t1 RIGHT JOIN t2 ON t1.c1 = t2.c2
 *                           ) full_join_k
 * To do this, we have to create a derived table instead of a joined table for
 * a full join nest. This part of work is done in make_derived_table.
 * 
 * This transformation need to replicate the two joined tables and the on clause.
 * We need to clone the whole parse tree rooted at these Parse_tree_nodes. This
 * is done in the parse_treee_node_clone.cc, collaborating with YXX who works on
 * start with ... connect by ...
 * 
 * 2. RESOLVER PHASE
 * 
 * 2.1 TABLE RESOLVE
 * Now we have the full join transformed into a derived table, we have to let
 * the resolver know about it, and resolve the idents correctly to it. For
 * example, the income query is
 *                      SELECT *
 *                      FROM t1 FULL JOIN t2 ON t1.c1 = t2.c2
 *                      WHERE t1.c1 < 5
 * After the transform, the query becomes
 *                      SELECT * 
 *                      FROM (SELECT *
 *                            FROM t1 ANTI JOIN t2 ON t1.c1 = t2.c2
 *                            UNION ALL
 *                            SELECT *
 *                            FROM t1 RIGHT JOIN t2 ON t1.c1 = t2.c2
 *                           ) full_join_k
 *                      WHERE t1.c1 < 5
 * In the former resolver, the top level context has no table t1, so it will rise
 * a bad table error. To solve this problem, we need to let the resolver know
 * that it should search the internal tables of full_join_k instead of itself.
 * We tell the resolver by adding a is_full_join_derived flag in the TABLE_LIST,
 * and set it to true for full join derived tables. The inner tables are in
 * TABLE_LIST::derived->first_select()->context->table_list. If we meet another
 * full join derived table in the list, we should search into it recursively.
 * 
 * This part is implement mostly in find_fields_in_table_ref(), which is used by
 * Item_field::fix_fields and Item_ref::fix_fields. 
 * 
 * 2.2 FIELD GENERATION
 * For the derived table, we use SELECT * to select all the fields from both sides
 * of the join operation. Normally, the setup_wild() in SELECT_LEX::prepare() will
 * do the job. However, when we have two tables with columns having the same name,
 * the derived table's setup will cause a duplicate column name error. So we have
 * to convert the columns from inner tables to columns of temp table with distinct
 * generating column names, while at the same time keep the table and field names
 * from inner tables.
 * 
 * We do this by adding a list of Full_join_field to the derived table's TABLE_LIST.
 * To generate this list, we use setup_wild() in the prepare() of the first_select()
 * in the full join's TABLE_LIST::derived unit. When calling insert_fields(), we
 * let it know whether we expect it to generate a full_join_fields_list. The
 * Full_join_field object contains the db, table and field name of the original field,
 * and an Item_ident point to the original inner table fields. 
 * 
 * Then, this list is used to create the derived unit's item_list. At this time, the
 * Item_type_holders are created and put into the item_list. We change the item_name
 * of the Item_type_holder to distinct inner tmp names which acts like alias for
 * original fields. We put the Item_type_holders into the Full_join_field as well,
 * and pass the full_join_fields_list to SELECT_LEX_UNIT. This part is done in 
 * SELECT_LEX_UNIT::prepare().
 * 
 * At last, the item_list of the derived unit is used to create tmp table columns.
 * We connect fields of this table to the Full_join_field objects, both direction.
 * We pass the full_join_fields_list to the derived table for furthur use.
 * 
 * 2.3 FIELD RESOLVE
 * We want to let the outer item's fix_fields() to resolve correctly to fields inside
 * the full join derived table. In fix_fields(), they use Field_iterator to iterate
 * through a table's fields, try to find a match for names. We create a new 
 * Field_iterator for full join derived tables: Field_iterator_full_join to iterate
 * over the full_join_fields_list, with the original names and tmp table fields.
 * This part is mostly done in sql_base.cc.
 * 
 * 2.4 ADDITIONAL CONSIDERATIONS
 * Natural join fields:
 * t1(c1,c2) natural full join t2(c1,c3) with the output fields (c1,c2,c3). In oracle,
 * c1 is not referable through t1.c1 or t2.c1 any more. In mysql, natural left join
 * and right join fields could still be refered by qualified form. Here, we choose to
 * follow the oracle's behavious. Thus, for natural join fields, we don't keep its
 * table name and db name in Full_join_field object. This also applied to join ...
 * using () fields.
 * Additionally, the field order of t1 natural left join t2 and t1 natural right
 * join t2 is different.
 * The output field order of 
 *                           t1(c1,c2) NATURAL LEFT JOIN t2(c1,c3)
 * is (c1,c2,c3), while that of 
 *                           t1(c1,c2) NATURAL RIGHT JOIN t2(c1,c3)
 * is (c1,c3,c2).
 * We have to take care of it in setup_wild(), more specifically, in
 * store_top_level_join_columns()
 * 
 * Anti join:
 * Anti join is not supported directly. It is recognized by the optimizer as a left
 * join nests marked by is_aj_nest(), which can be set by set_sj_or_aj_nest() to a
 * nest join table_list with on_cond. Moreover, a table's first_inner is not set to
 * itself if it is a single table nest. So, when we clone the PT_table_reference of
 * right table, if it is not a joined table, we add an additional nest level. 
 * 
 * Prepared statement:
 * For prepared statement, it is worth noting the statement arena and thd arena. The
 * thd arena lasts for a query, while the statement arena lasts for prepared statement.
 * For now, items are reused in multiple runs of ps & sp's, but materialized tmp tables
 * are not. Thus, the item_field's in item_list are reused, but the fields in them are
 * not correct. So we need to re-fix_fields.
 * 
 * View:
 * We want to let view know about anti join. However, anti-join is not supported in
 * current view processing. So we use a hint to pass this info. Unfortunately, hint
 * is not supported in view processing, either. We have to modify this so that the
 * specific hint we use to pass anti-join info will be correctly processed.
 */
#include "sql/zsql_features/full_join/full_join_field.h"
#include "sql/zsql_features/full_join/full_join.h"

#include "m_ctype.h"
#include "m_string.h"
#include "my_dbug.h"
#include "sql_string.h"
#include "sql/sql_class.h"
#include "sql/mem_root_array.h"
#include "sql/mysqld.h"
#include "sql/parse_tree_nodes.h"
#include "sql/parse_tree_items.h"
#include "sql/parse_tree_hints.h"
#include "sql/table.h"
#include "sql/nested_join.h"
#include "sql/item.h"
#include "sql/derror.h"

#include "sql/oracle_compatibility/parse_tree_transformer.h"
#include "sql/oracle_compatibility/parse_tree_node_clone.h"

static bool name_exists_and_not_empty(const char *name) {
  return name && name[0];
}

/* Full_join_field constructor used by insert_fields */
Full_join_field::Full_join_field(THD *thd,
                  const char *db_arg,
                  const char *table_arg,
                  const char *field_arg,
                  const Item_name_string &alias_arg,
                  bool is_natural_join_field_arg,
                  Item_ident *item_arg)
    : is_natural_join_field(is_natural_join_field_arg),
      item(item_arg)
{
  real_db_name = (!is_natural_join_field_arg && db_arg) 
                      ? thd->mem_strdup(db_arg) : nullptr;
  real_table_name = (!is_natural_join_field_arg && table_arg)
                      ? thd->mem_strdup(table_arg) : nullptr;
  if (field_arg)
    real_field_name = thd->mem_strdup(field_arg);
  else
    real_field_name = nullptr; // should not happen
  real_alias = thd->strmake(alias_arg.ptr(),alias_arg.length());
}

Full_join_field::Full_join_field(THD *thd,
                  const char *db_arg,
                  const char *table_arg,
                  const char *field_arg,
                  const char *alias_arg,
                  bool is_natural_join_field_arg,
                  Item_ident *item_arg)
    : is_natural_join_field(is_natural_join_field_arg),
      item(item_arg)
{
  real_db_name = (!is_natural_join_field_arg && db_arg) 
                      ? thd->mem_strdup(db_arg) : nullptr;
  real_table_name = (!is_natural_join_field_arg && table_arg)
                      ? thd->mem_strdup(table_arg) : nullptr;
  if (field_arg)
    real_field_name = thd->mem_strdup(field_arg);
  else
    real_field_name = nullptr;
  if (alias_arg)
    real_alias = thd->mem_strdup(alias_arg);
  else
    real_alias = nullptr;
}

/* 
 * Full_join_field constructor for other usage.
 * When call this, the Item itself should contain right names.
 */
Full_join_field::Full_join_field(THD *thd,
                                 bool is_natural_join_field_arg,
                                 Item_ident *item_arg)
    : Full_join_field(thd, item_arg->db_name, item_arg->table_name,
                      item_arg->field_name, item_arg->item_name,
                      is_natural_join_field_arg, item_arg)
{}

/*
 * Full_join_field::create_item
 * 
 * create an Item_field for a Full_join_field.
 * Its name is the original alias/name.
 */
Item *Full_join_field::create_item(THD *thd)
{
  SELECT_LEX *select = thd->lex->current_select();
  // create the Item_field
  Item_field *item = new Item_field(thd, &select->context,
                                    field,
                                    real_alias ? real_alias : real_field_name);
  if (!item) return nullptr;
  // set nullable properties
  if(is_null_on_empty_table(thd, item))
  {
    item->maybe_null = true;
    field->table->set_nullable();
  }
  return item;
}

void PT_table_factor_joined_table::set_full_join_anti() {
  m_joined_table->is_full_join_anti = true;
}

/*
 * Helper functions for make_derived_table.
 */
/* make a unique alias for full join derived table. */
static bool make_full_join_derived_table_alias(THD *thd,
                                          PT_table_reference *l,
                                          PT_table_reference *r,
                                          Item *on_cond,
                                          List<String> *using_fields,
                                          LEX_CSTRING &alias) {
  char name_buf[NAME_LEN + 1];
  const char *prefix = "#fj";
  uint p;
  uint moder = 1 << 24;
  p = ((ulonglong)l % moder +
       (ulonglong)r % moder +
       (ulonglong)on_cond % moder +
       (ulonglong)using_fields % moder) % moder;
  snprintf(name_buf, sizeof(name_buf), "%s%llu_%x",
           prefix, thd->lex->full_join_count++, p);
  name_buf[NAME_LEN] = '\0';
  size_t len = strlen(name_buf);
  alias.str = strmake_root(thd->mem_root, name_buf, len);
  if (!alias.str) {
    alias.length = 0;
    return true;
  }
  alias.length = len;
  return false;
}

#ifndef DBUG_OFF
/*
 * An outer join nest must contains one and only one of 
 * on_cond, natural and using_fields.
 */
static bool only_one_join_cond(Item *on_cond,
                               PT_joined_table_type join_type,
                               List<String> *using_fields) {
  if (on_cond)
    return !(join_type & JTT_NATURAL) && !using_fields;
  else if (join_type & JTT_NATURAL)
    return !using_fields;
  else
    return using_fields != nullptr;
}
#endif

/* Make a select list with a '*' */
static PT_select_item_list *make_select_list(THD *thd) {
  Item *asterisk = nullptr;
  PT_select_item_list *select_list = nullptr;
  if (!(asterisk = new(thd->mem_root) Item_field(POS(), nullptr, nullptr, "*")))
    return nullptr;
  select_list = new(thd->mem_root) PT_select_item_list;
  if (select_list == nullptr || select_list->push_back(asterisk))
    return nullptr;
  return select_list;
}

/* Make from list with left_op join right_op on/using */
static Mem_root_array<PT_table_reference *>
              *make_from_list(THD *thd, bool antijoin,
                              const POS &join_pos,
                              PT_table_reference *left_op,
                              PT_joined_table_type join_type,
                              PT_table_reference *right_op,
                              Item *on_cond,
                              List<String> *using_fields) {
  PT_joined_table *joined_table = nullptr;
  Mem_root_array<PT_table_reference *> *from_clause = nullptr;
  // FROM lhs [join_type] JOIN rhs ON on_cond
  if (on_cond) {
    if (!(joined_table = new(thd->mem_root)
                  PT_joined_table_on(left_op,
                                      join_pos,
                                      join_type,
                                      right_op,
                                      on_cond)))
      return nullptr;
  }
  // FROM lhs [join_type] JOIN rhs USING (using_fields)
  // or FROM lhs NATURAL [join_type] JOIN rhs 
  else if (using_fields || (join_type & JTT_NATURAL)) {
    if (!(joined_table = new(thd->mem_root)
                  PT_joined_table_using(left_op,
                                        join_pos,
                                        join_type,
                                        right_op,
                                        using_fields)))
      return nullptr;
  }
  // We should have the [join_type] join PT_jonied_table now
  else return nullptr; // should not happen
  // Mark anti join, anti join will be set in prepare
  if (antijoin && (join_type & JTT_LEFT))
    joined_table->is_full_join_anti = true;
  if (!(from_clause = new(thd->mem_root)
          Mem_root_array<PT_table_reference *>(thd->mem_root))
      || from_clause->push_back(joined_table))
    return nullptr;
  return from_clause;
}

/* Make a hint list contains FULL_JOIN_ANTI hint. */
static PT_hint_list *make_hint_list(THD *thd) {
  PT_hint_list *hint_list = new(thd->mem_root) PT_hint_list(thd->mem_root);
  PT_qb_level_hint *fj_anti_hint = nullptr;
  fj_anti_hint = new(thd->mem_root) PT_qb_level_hint(NULL_CSTR,
                                                      true,
                                                      FULL_JOIN_ANTI_BLOCK_ENUM,
                                                      0);
  if (!hint_list || !fj_anti_hint || hint_list->push_back(fj_anti_hint))
    return nullptr;
  return hint_list;
}
/*
 * make_jonied_table_query
 * 
 * Make a PT_query_specification for use in make_derived_table.
 * Given A (NATURAL) [LEFT/RIGHT] JOIN B (ON .../ USING(...)), make a
 * PT_query_specification of the form
 * SELECT(possibly with anti join hint) *
 * FROM A (NATURAL) [LEFT/RIGHT] JOIN B (ON .../ USING(...))
 * 
 * Parameters:
 * thd          pointer to THD
 * antijoin     need an antijoin? Only valid when (join_type | JTT_LEFT)
 * join_pos     position in syntax
 * left_op      LEFT JOINED OPERANT
 * join_type    join type, should be (NATURAL) LEFT/RIGHT
 * right_op     RIGHT JOINED OPERANT
 * on_cond      the join on cond
 * using_field  the join using field
 * query[out]   the generated query
 * 
 * Return:
 * false for success, true for error
 */
static bool make_joined_table_query(THD *thd, bool antijoin,
                             const POS &join_pos,
                             PT_table_reference *left_op,
                             PT_joined_table_type join_type,
                             PT_table_reference *right_op,
                             Item *on_cond,
                             List<String> *using_fields,
                             PT_query_specification **query) {
  DBUG_ASSERT(join_type & (JTT_LEFT | JTT_RIGHT));
  DBUG_ASSERT(only_one_join_cond(on_cond, join_type, using_fields));
  Query_options *options = nullptr;
  PT_select_item_list *select_list = nullptr;
  Mem_root_array<PT_table_reference *> *from_clause = nullptr;
  PT_query_specification *query_tmp = nullptr;
  PT_hint_list *hint_list = nullptr; // could be null
  /* Construct the join query block */
  // Query_options for query_specification
  {
    if (!(options = new(thd->mem_root) Query_options))
      return true;
    // no options
    options->query_spec_options = 0;
  }
  // select list
  // SELECT *
  if (!(select_list = make_select_list(thd))) return true;

  // from clause
  if (!(from_clause = make_from_list(thd, antijoin, join_pos,
                                     left_op, join_type,
                                     right_op, on_cond,
                                     using_fields)))
    return true;

  // full join anti block hint
  if (antijoin && (join_type & JTT_LEFT)) {
    if (!(hint_list = make_hint_list(thd)))
      return true;
  }
  // joined query
  if (!(query_tmp = new(thd->mem_root) 
          PT_query_specification(hint_list,      // hints
                                  *options,       // options
                                  select_list,    // select
                                  nullptr,        // opt_into1
                                  *from_clause,   // from
                                  nullptr,        // where
                                  nullptr,        // hierarchical
                                  nullptr,        // group
                                  nullptr,        // having
                                  nullptr)))      // window
    return true;
  *query = query_tmp;
  return false;
}

/*
 * Given nesessary elements, make a full join derived table.
 * 
 * The inputs is supposed to be correct and not null.
 */
static PT_derived_table *make_union_query(THD *thd,
                      LEX_CSTRING &alias,
                      PT_query_expression_body *left,
                      PT_query_primary *right,
                      POS &join_pos,
                      Create_col_name_list *col_name) {
  PT_union *full_join_query = nullptr;
  PT_query_expression *qe_left, *qe_union = nullptr;
  PT_subquery *subq = nullptr;
  PT_derived_table *ret = nullptr;
        
  // build the derived table
  if (!(qe_left = new(thd->mem_root) // left query
            PT_query_expression(left))
      || !(full_join_query = new(thd->mem_root) // left query union all right query
            PT_union(qe_left, join_pos, false, right))
      || !(qe_union = new(thd->mem_root) // wrap in a query expression
            PT_query_expression(full_join_query))
      || !(subq = new(thd->mem_root) // wrap in a subquery
            PT_subquery(join_pos, qe_union))
      || !(ret = new(thd->mem_root) // the derived table
            PT_derived_table(false, subq, alias, col_name)))
    return nullptr;

  return ret;
}

/* Clone needed members for building full join anti block. */
bool PT_full_joined_table::clone_members(THD *thd,
                                         PT_table_reference **rep_tab1_node,
                                         PT_table_reference **rep_tab2_node,
                                         Item **rep_on,
                                         List<String> **rep_using_fields) {
  // clone left operant
  *rep_tab1_node = 
                static_cast<PT_table_reference *>(tab1_node->clone(nullptr));
  // clone right operant
  *rep_tab2_node = 
                static_cast<PT_table_reference *>(tab2_node->clone(nullptr));
  if (!(*rep_tab1_node) || !(*rep_tab2_node)) return true;
  // clone on conditions
  if (on && !(*rep_on = static_cast<Item *>(on->clone(nullptr))))
    return true;
  // clone using fields
  if (using_fields) {
    if (!(*rep_using_fields = clone_String_list(thd->mem_root, using_fields)))
      return true;
  }
  return false;
}

/*
 * make_derived_table:
 * Make full join derived table. Called in contextualize().
 */
bool PT_full_joined_table::make_derived_table(THD *thd) {
  Create_col_name_list *col_name;
  PT_query_expression_body_primary *left_join_body;
  PT_query_specification *right_join_query;
  LEX_CSTRING alias;
  // Make anti query block
  // Use cloned objects to construct anti block
  {
    PT_query_specification *left_query = nullptr;
    PT_table_reference *rep_tab1_node = nullptr;
    PT_table_reference *rep_tab2_node = nullptr;
    Item *rep_on = nullptr;
    List<String> *rep_using_fields = nullptr;
    if (clone_members(thd, &rep_tab1_node, &rep_tab2_node,
                      &rep_on, &rep_using_fields))
      return true;
    PT_joined_table_type aj_type =
              (PT_joined_table_type)(JTT_LEFT | (m_type & JTT_NATURAL));
    if (make_joined_table_query(thd, true, join_pos,
                                rep_tab1_node,
                                aj_type,
                                rep_tab2_node,
                                rep_on,
                                rep_using_fields,
                                &left_query))
      return true;
    if (!(left_join_body = new(thd->mem_root)
              PT_query_expression_body_primary(left_query)))
      return true;
  }
  // Make right query block
  // use original objects
  {
    PT_joined_table_type rj_type =
              (PT_joined_table_type)(JTT_RIGHT | (m_type & JTT_NATURAL));
    if (make_joined_table_query(thd, false, join_pos,
                                tab1_node,
                                rj_type,
                                tab2_node,
                                on,
                                using_fields,
                                &right_join_query))
      return true;
  }
  // Column names for derived table
  // We cannot decide yet, so we pass an empty list
  if (!(col_name = new(thd->mem_root)Create_col_name_list))
    return true;
  col_name->init(thd->mem_root);

  // make full join table alias
  if (make_full_join_derived_table_alias(thd, tab1_node,
                                          tab2_node, on,
                                          using_fields, alias))
    return true;
  /* Construct full join query expression */
  // left_query UNION ALL right_query
  if (!(derived = make_union_query(thd, alias,
                                    left_join_body,
                                    right_join_query,
                                    join_pos, col_name)))
    return true;
  return false;
}

/* clone() a PT_full_joined_table */
void *PT_full_joined_table::clone(Parse_tree_transformer *ptt) const {
  // clone members of object
  PT_full_joined_table *node = nullptr;
  PT_table_reference *tab1_node_clone = static_cast<PT_table_reference*>(tab1_node->clone(ptt));
  PT_table_reference *tab2_node_clone = static_cast<PT_table_reference*>(tab2_node->clone(ptt));
  Item *on_clone = nullptr;
  List<String> *using_fields_clone = nullptr;

  if(!tab1_node_clone || !tab2_node_clone) {
    return nullptr;
  }

  if (on && !(on_clone = static_cast<Item*>(on->clone(ptt)))) return nullptr;

  if(using_fields) {
    if (!(using_fields_clone = clone_String_list(current_thd->mem_root, using_fields)))
      return nullptr;
  }
  // build new PT_full_joined_table
  if (on_clone)
    node = new(current_thd->mem_root) PT_full_joined_table(tab1_node_clone,
                    join_pos, m_type, tab2_node_clone, on_clone);
  else if (using_fields_clone)
    node = new(current_thd->mem_root) PT_full_joined_table(tab1_node_clone,
                    join_pos, m_type, tab2_node_clone, using_fields_clone);
  else
    node = new(current_thd->mem_root) PT_full_joined_table(tab1_node_clone,
                    join_pos, m_type, tab2_node_clone);
  // make derived table
  if(!node) return nullptr;

  if(ptt) {
    ptt->transform(node);
  }
  return node;
}

/*
 * TABLE_LIST::is_full_join_child_table
 * 
 * test for table in full join nest.
 * If no name is given, which means field name not qualified by a table name,
 * return true.
 * 
 * @param db_name     name of db, we assume the db_name is correctly setup
 *                  if exists.
 * @param table_name  name of table to search
 * @return    true for table found, false for not. if no table name.
 */
bool TABLE_LIST::full_join_contains_table(const char *db_name, const char *table_name)
{
  // We get here only for full join converted derived table
  DBUG_ASSERT(is_full_join_derived && (derived != NULL));
  if (!name_exists_and_not_empty(table_name))
    return true;
  // For full join converted derived table, we get two SELECT_LEX with the same tables.
  SELECT_LEX *select = derived->first_select();
  DBUG_ASSERT(select != NULL);
  /*
   * TODO: FULL JOIN.We should do sth. to make sure that the context is prepared.
   */
  TABLE_LIST *inner_tables = select->context.table_list;
  for (TABLE_LIST *tables = inner_tables; tables; tables = tables->next_local)
  {
    // meet a full join derived table in search, recursive into it
    if (tables->derived && tables->is_full_join_derived)
    {
      if (tables->full_join_contains_table(db_name, table_name))
        return true;
    }
    // don't search full join derived table itself
    else
    {
      if (!my_strcasecmp(table_alias_charset, table_name, tables->alias) &&
            (!name_exists_and_not_empty(db_name) || !strcmp(tables->db, db_name)))
        return true;
    }
  }
  return false;
}

/*
 * in_full_join_nest
 * If this table_list is in a full join nest.
 * Recursively check outer select for full join.
 * 
 * Return:
 * True for yes, false for no.
 */
bool TABLE_LIST::in_full_join_nest() {
  SELECT_LEX *sel_curr = select_lex;
  if ((outer_join & JOIN_TYPE_RIGHT)) {
    while (sel_curr) {
      if (sel_curr->has_full_join)
        return true;
      else
        sel_curr = sel_curr->outer_select();
    }
  }
  return false;
}

void TABLE_LIST::setup_full_join_fields_list() {
  if (!is_full_join_derived)
    return;
  DBUG_ASSERT(derived && derived->full_join_fields_list);
  DBUG_ASSERT(table->visible_field_count()
              == derived->full_join_fields_list->elements);
#ifndef DBUG_OFF
  bool first_execution = full_join_fields_list == nullptr;
#endif
  full_join_fields_list = derived->full_join_fields_list;
  List_iterator_fast<Full_join_field> it_fj(*full_join_fields_list);
  Field **ptr = table->visible_field_ptr();
  Full_join_field *fj_field;
  while((fj_field=it_fj++))
  {
    DBUG_ASSERT(*ptr);
    DBUG_ASSERT(fj_field->item);
    DBUG_ASSERT(fj_field->type_holder);
    DBUG_ASSERT(!fj_field->field || !first_execution);
    fj_field->field = *ptr;
    (*ptr)->full_join_field = fj_field;
    ptr++;
  }
}

bool TABLE_LIST::make_full_join_table_column_names(THD *thd) {
  // if 'this' is not a full join derived table, return
  if (!is_full_join_derived || m_derived_column_names)
    return false;
  // new item are created on statement arena
  Prepared_stmt_arena_holder ps_arena_holder(thd);
  Create_col_name_list *tmp_list = new(thd->mem_root) 
                            Mem_root_array<LEX_CSTRING>(thd->mem_root);
  if (!tmp_list)
    return true; // OOM
  List_iterator_fast<Item> li(derived->types);
  Item *item;
  ulonglong field_count = 0;
  while ((item = li++)) {
    // Change item_name for derived columns inner name of full join
    char name_buf[NAME_LEN + 1];
    const char *prefix = "#fjc";
    snprintf(name_buf, sizeof(name_buf), "%s%llu", prefix, field_count++);
    name_buf[NAME_LEN] = '\0';
    size_t len = strlen(name_buf);
    DBUG_ASSERT(len <= NAME_LEN);
    LEX_CSTRING *name_str =  (LEX_CSTRING *) thd->alloc(sizeof(LEX_CSTRING));
    if (!name_str)
      return true; // OOM
    if (!(name_str->str = thd->strmake(name_buf, len)))
      return true;
    name_str->length = len;
    if (tmp_list->push_back(*name_str))
      return true; // OOM
  }
  set_derived_column_names(tmp_list);
  return false;
}

/* create an Item_field for a full join derived table field */
Item *Field_iterator_full_join::create_item(THD *thd)
{
  return cur_field_ref->create_item(thd);
}

/* Init a Field_iterator_full_join */
void Field_iterator_full_join::set(TABLE_LIST *table)
{
  DBUG_ASSERT(table->is_full_join_derived &&
              table->full_join_fields_list);
  fj_fields_it.init(*table->full_join_fields_list);
  cur_field_ref = fj_fields_it++;
  // field should have been set when we call this
  DBUG_ASSERT(!cur_field_ref || cur_field_ref->field);
}

/* iterate over the field iterator */
void Field_iterator_full_join::next()
{
  cur_field_ref = fj_fields_it++;
  DBUG_ASSERT(!cur_field_ref || cur_field_ref->field);
}

/* create full join field in insert_fields. */
Full_join_field *Field_iterator_table_ref::create_full_join_field(THD *thd, Item_ident *item)
{
  Full_join_field *ret = nullptr;
  if (item == nullptr) {
    item = static_cast<Item_ident *>(create_item(thd));
  }
  // If the field is in a full join derived table, reuse the names
  // select * from t1 full join t2 on a=b full join t3 on b=c;
  if (field_it == &full_join_it) {
    Full_join_field *tmp = full_join_it.field_ref();
    ret = new(thd->mem_root)Full_join_field(thd,
                                            tmp->real_db_name,
                                            tmp->real_table_name,
                                            tmp->real_field_name,
                                            tmp->real_alias,
                                            tmp->is_natural_join_field,
                                            item);
  }
  // If it in a full join table contained in a natural join table
  // select * from ((t1 full join t2 on a=b) natural join t3) 
  //                        full join t4 on t4.a1=t1.a1;
  else if (field_it == &natural_join_it &&
            natural_join_it.column_ref()->full_join_field) {
    Full_join_field *tmp = natural_join_it.column_ref()->full_join_field;
    bool tmp_is_natural_join_field = natural_join_it.column_ref()->is_natural_join_field;
    ret = new(thd->mem_root)Full_join_field(thd,
                                            tmp->real_db_name,
                                            tmp->real_table_name,
                                            tmp->real_field_name,
                                            tmp->real_alias,
                                            tmp_is_natural_join_field,
                                            item);
  }
  else
    ret = new(thd->mem_root)Full_join_field(thd,
                                            is_natural_join_field(),
                                            item);
  return ret; // if nullptr, will deal outside.
}

Full_join_field *Field_iterator_table_ref::get_full_join_field() {
  if (field_it == &full_join_it)
    return full_join_it.field_ref();
  return nullptr;
}

/* Helper functions for find_fields_in_full_join */
#ifndef DBUG_OFF
/* There should be no db name unless table name is specified */
static bool no_db_name_unless_table_name_exists(const char *db_name,
                                                const char *table_name) {
  return !(db_name && db_name[0]) || (table_name && table_name[0]);
}
#endif

/*
 * Compare query_name to real_name.
 * 
 * false for equal, true for not equal.
 * 
 * If query_name not exists, return false, for all matched.
 * */
static bool name_cmp_safe(const char *real_name, const char *query_name) {
  return query_name && query_name[0] &&
          my_strcasecmp(table_alias_charset,
                        real_name, query_name);
}

/* Natural join using field refered with qualifed form? */
static bool natural_field_qualified_refered(Full_join_field *fj,
                                            const char *table_name) {
  DBUG_ASSERT(fj);
  return fj->is_natural_join_field &&
         table_name && table_name[0];
}
/* Derived table field with no db name qualified with a db? */
static bool derived_table_qualified_db(Full_join_field *fj,
                                       const char *db_name) {
  DBUG_ASSERT(fj);
  return db_name && db_name[0] &&
         !(fj->real_db_name && fj->real_db_name[0]);
}
/*
 * find_field_in_full_join
 * search an ident in a full_join_derived_table
 * Called in find_field_in_table_ref
 */
Field *find_field_in_full_join(TABLE_LIST *table_list,
                               const char *db_name,
                               const char *table_name,
                               const char *field_name,
                               Field **found) {
  DBUG_TRACE;
  DBUG_PRINT("enter", ("full join: '%s', db name: '%s', "
                       "table name: '%s', field name: '%s'",
                       table_list->alias, db_name, table_name,
                       field_name));
  DBUG_ASSERT(found != nullptr);
  DBUG_ASSERT(no_db_name_unless_table_name_exists(db_name, table_name));
  DBUG_ASSERT(table_list->full_join_contains_table(db_name, table_name));
  DBUG_ASSERT(name_exists_and_not_empty(field_name));
  bool found_natural_field_qualified = false;
  Full_join_field *fj_field, *curr_fj_field;
  Field_iterator_full_join field_it;
  field_it.set(table_list);

  for (fj_field = nullptr, curr_fj_field = field_it.field_ref();
       !field_it.end_of_fields();
       field_it.next(), curr_fj_field = field_it.field_ref()) {
    // Field name not matched
    if (my_strcasecmp(system_charset_info,
                      curr_fj_field->real_field_name, field_name))
      continue;
    // A natural join field, which has no table name
    if (natural_field_qualified_refered(curr_fj_field, table_name)){
      found_natural_field_qualified = true;
      continue;
    }
    // A derived table field, which has no db name
    if (derived_table_qualified_db(curr_fj_field, db_name))
      continue;
    // All matched
    if (!name_cmp_safe(curr_fj_field->real_table_name, table_name)
        && !name_cmp_safe(curr_fj_field->real_db_name, db_name)) {
      // found multiple match field, report ambiguous
      if (fj_field) {
        if (found)
          *found = fj_field->field;
        return fj_field->field; // for compatible with other find_field_in_xxx
      }
      // mark found
      fj_field = curr_fj_field;
    }
  }
  // Only qualified natural join field found
  if (!fj_field && found_natural_field_qualified) {
    // Natural join / using fields is refered in qualified form.
    my_error(ER_NATURAL_JOIN_COLUMN_QUALIFIED_REFERED, MYF(0), 
              field_name);
    return nullptr;
  }
  return (fj_field) ? fj_field->field : nullptr;
}

bool table_alias_duplicate(TABLE_LIST *a, TABLE_LIST *b) {
  if (my_strcasecmp(table_alias_charset,
                     a->alias,
                     b->alias)
      || strcmp(a->db, b->db))
    return false;
  return true;
}

bool check_duplicate_with_list(TABLE_LIST *inner_tbl,
                               List_iterator_fast<TABLE_LIST> &li,
                               SQL_I_List<TABLE_LIST> &table_list) {
  TABLE_LIST *former_tbl;
  li.rewind();
  while ((former_tbl = li++)) {
    if (table_alias_duplicate(inner_tbl, former_tbl))
      return true;
  }
  for (former_tbl = table_list.first;
        former_tbl; former_tbl = former_tbl->next_local) {
    if (table_alias_duplicate(inner_tbl, former_tbl))
      return true;
  }
  return false;
}

bool SELECT_LEX::alias_duplicate_with_full_join_inner_tables(TABLE_LIST *tbl) {
  DBUG_ASSERT(tbl);
  DBUG_ASSERT(tbl->alias);

  if (!table_list_full || table_list_full->elements == 0)
    return false;
  List_iterator<TABLE_LIST> li(*table_list_full);
  TABLE_LIST *former_tbl;
  while ((former_tbl = li++)) {
    if (table_alias_duplicate(tbl, former_tbl)) {
      return true;
    }
  }
  return false;
}

bool SELECT_LEX::check_duplicate_table_alias_for_full_join(THD *thd, TABLE_LIST *tbl) {
  // Just set in PT_full_joined_table::contextualize()
  // should not be called elsewhere.
  DBUG_ASSERT(tbl);
  DBUG_ASSERT(tbl->is_full_join_derived);
  DBUG_ASSERT(tbl->derived_unit());
  DBUG_ASSERT(has_full_join);
  // If tbl is the first full join derived table, table_list_full has not been created yet.
  if (!table_list_full && !(table_list_full = new (thd->mem_root)List<TABLE_LIST>))
    return true; // OOM

  List_iterator_fast<TABLE_LIST> li(*table_list_full);
  List_iterator_fast<TABLE_LIST> li_inner;
  List<TABLE_LIST> *inner_list = tbl->derived_unit()->first_select()->table_list_full;
  TABLE_LIST *inner_tbl;

  if (inner_list) {
    li_inner.init(*inner_list);
    while ((inner_tbl = li_inner++)) {
      if (check_duplicate_with_list(inner_tbl, li, table_list)) {
        my_error(ER_NONUNIQ_TABLE, MYF(0), inner_tbl->alias);
        return true;
      }
    }
    table_list_full->concat(inner_list);
  }
  
  for (inner_tbl = tbl->derived_unit()->first_select()->table_list.first;
         inner_tbl; inner_tbl = inner_tbl->next_local) {
    if (check_duplicate_with_list(inner_tbl, li, table_list)) {
      my_error(ER_NONUNIQ_TABLE, MYF(0), inner_tbl->alias);
      return true;
    }
    if (table_list_full->push_back(inner_tbl))
      return true;  // OOM
  }
  return false;
}

/*
 * Test if this query block has and needs apply the FULL_JOIN_ANTI hint.
 */
bool SELECT_LEX::need_apply_full_join_anti_hint(THD *thd) {
  PT_qb_level_hint *fj_hint = nullptr;
  return  opt_hints_qb && 
          (fj_hint = opt_hints_qb->get_full_join_anti_hint()) &&
          (fj_hint->type() == FULL_JOIN_ANTI_BLOCK_ENUM) &&
          (fj_hint->switch_on()) &&
          first_execution &&
          !thd->lex->is_view_context_analysis();
}
/*
 * Test if the FULL_JOIN_ANTI hint can be applied.
 */
bool SELECT_LEX::can_apply_full_join_anti_hint() {
  // Some nesessary check
  /*
    * 1. top_join_list has only one element;
    * 2. the element is a nested join with 2 elements;
    * 3. its first child is a left join nest;
    * 4. master unit is a union, this is the first child.
    * These conditions are nesessary but not sufficient.
    */
  NESTED_JOIN *nj_tmp = nullptr;
  if (top_join_list.elements == 1 && // 1
      (nj_tmp = top_join_list.head()->nested_join) && //2
      nj_tmp->join_list.elements == 2
      && (nj_tmp->join_list.head()->outer_join & JOIN_TYPE_LEFT) //3
      //&& master->is_union() && master->first_select() == this //4
      )
    return true;
  return false;
}
/*
 * Test appliability of FULL_JOIN_ANTI hint and apply it.
 */
void SELECT_LEX::apply_full_join_anti_hint(THD *thd) {
  // IF this block is marked as FULL JOIN ANTI BLOCK
  if (need_apply_full_join_anti_hint(thd)) {
    if (can_apply_full_join_anti_hint()) {
      NESTED_JOIN *nj_tmp = top_join_list.head()->nested_join;
      nj_tmp->join_list.head()->set_sj_or_aj_nest();
    }
    else {
      push_warning_printf(thd,Sql_condition::SL_WARNING,
                          ER_WARN_FULL_JOIN_ANTI_BLOCK_HINT_MISUSED,
                          ER_DEFAULT(ER_WARN_FULL_JOIN_ANTI_BLOCK_HINT_MISUSED),
                          "");
    }
  }
}

bool SELECT_LEX::check_and_create_full_join_fields_list(THD *thd,
                                    bool &is_fj_first_select) {
  is_fj_first_select = is_full_join_first_select();
  if (is_fj_first_select && !full_join_fields_list
      && !(full_join_fields_list = new(thd->mem_root)
                                         List<Full_join_field>))
    return true;
  return false;
}

/*
 * Setup the full_join_fields_list for a full join derived unit.
 */
bool SELECT_LEX_UNIT::setup_full_join_fields_list(THD *thd, SELECT_LEX *sl) {
  // not full join first select, return
  if (!sl->is_full_join_first_select())
    return false;
  if (!sl->first_execution) {
    DBUG_ASSERT(full_join_fields_list);
    return false;
  }
  // if it's not the first execution, no table is marked as full join derived table.
  DBUG_ASSERT(sl->full_join_fields_list);
  DBUG_ASSERT(sl->item_list.elements ==
              sl->full_join_fields_list->elements);
  types.empty();
#ifndef DBUG_OFF
  List_iterator_fast<Item> it(sl->item_list);
  Item *item_list_tmp;
#endif
  List_iterator_fast<Full_join_field> it_fj(*sl->full_join_fields_list);
  Full_join_field *fj_field;
  Item_ident *item_tmp;
  // iterate over Full_join_fields, build Item_type_holder for each field.
  while ((fj_field = it_fj++))
  {
    item_tmp = fj_field->item;
#ifndef DBUG_OFF
    item_list_tmp = it++;
    DBUG_ASSERT(item_list_tmp == (Item *)item_tmp);
#endif
    /* This test maybe not neccesary for full join items, but still we
        can keep them here for safety. */
    /*
      If the outer query has a GROUP BY clause, an outer reference to this
      query block may have been wrapped in a Item_outer_ref, which has not
      been fixed yet. An Item_type_holder must be created based on a fixed
      Item, so use the inner Item instead.
    */
    DBUG_ASSERT(item_tmp->fixed ||
                (item_tmp->type() == Item::REF_ITEM &&
                  down_cast<Item_ref *>(item_tmp)->ref_type() ==
                      Item_ref::OUTER_REF));
    if (!item_tmp->fixed) item_tmp = down_cast<Item_ident*>(item_tmp->real_item());

    auto holder = new Item_type_holder(thd, item_tmp);
    if (!holder) return true;
    // type_holder is not set yet, not true for ps/sp
    DBUG_ASSERT(fj_field->item);
    DBUG_ASSERT(!fj_field->type_holder);
    fj_field->type_holder = holder;
    types.push_back(holder);
  }
  full_join_fields_list = sl->full_join_fields_list;
  return false;
}

/*
 * has_full_join_anti_block_hint
 * 
 * Test if hints list contains full join anti block hint
 * 
 * Return
 * true for yes; false for no
 */
bool PT_hint_list::has_full_join_anti_block_hint()
{
  for (PT_hint *hint : hints)
  {
    if (hint && hint->type() == FULL_JOIN_ANTI_BLOCK_ENUM)
      return true;
  }
  return false;
}

/*
 * Keep only full join anti block hint in the list.
 * 
 * Should be called 1.when we know there is a FULL_JOIN_ANTI hint; 2. in a
 * opt_qb_hints list.
 * 
 * Also, each select is supposed to have at most one of this kind of hint,
 * test this in DBUG mode.
 */

bool PT_hint_list::keep_only_full_join_anti_block_hint(bool &contains_other_hints)
{
  PT_hint *fj_hint = nullptr;
  for (PT_hint *hint : hints)
  {
    if (hint->type() == FULL_JOIN_ANTI_BLOCK_ENUM)
      fj_hint = hint;
    else
      contains_other_hints = true;
    // This short cut is not short at most time
    // if (fj_hint && *contains_other_hints) break;
  }
  DBUG_ASSERT((fj_hint));
  hints.clear();
  if (hints.push_back(fj_hint))
    return true;
  return false;
}

/*
 * Generate full join field for select_lex in insert fields
 * 
 * Param:
 * thd      current thd
 * item     the item to create full join field from
 * field_iterator   the field iterator to get full join field
 * fj_fields_list   output place of the generated full join field
 * 
 * Return
 * true for error, false for complete.
 */
bool generate_full_join_field(THD *thd, Item *item,
                              Field_iterator_table_ref &field_iterator,
                              List<Full_join_field> *fj_fields_list) {
  DBUG_ASSERT(fj_fields_list);
  DBUG_ASSERT(item->type() == Item::FIELD_ITEM 
              || item->type() == Item::REF_ITEM);
  Item_ident *ident = down_cast<Item_ident *>(item);
  Full_join_field *fj_field; 
  // thd->mem_root is set to stmt root in setup_wild.
  if (!(fj_field = field_iterator.create_full_join_field(thd, ident)))
    return true; // Should be OOM.
  DBUG_ASSERT(fj_field->item && !fj_field->type_holder &&
              !fj_field->field);
  if (fj_fields_list->push_back(fj_field))
    return true; // OOM
  return false;
}

bool is_table_name_match_full_join_field(const char *db_name,
                                         const char *table_name,
                                         TABLE_LIST *tables,
                                         Field_iterator_table_ref &field_iterator) {
  if (!tables->is_full_join_derived)
    return true;
  if (!name_exists_and_not_empty(table_name))
    return true;
  DBUG_ASSERT(field_iterator.field()->table->pos_in_table_list == tables);
  Full_join_field *fj_field = nullptr;
  if ((fj_field = field_iterator.get_full_join_field())) {
    if (!name_cmp_safe(fj_field->real_table_name, table_name) && 
        !name_cmp_safe(fj_field->real_db_name, db_name)) {
      return true;
    }
  }
  return false;
}

void get_full_join_field_name_from_item(Item_ident *item, const char **field_name,
                              const char **table_name, const char **db_name) {
  if (!field_name || !table_name || !db_name)
    return;
  if (item->type() == Item::FIELD_ITEM) {
    Item_field *item_field = static_cast<Item_field *>(item);
    Full_join_field *fj_field = nullptr;
    if (item_field->field && (fj_field = item_field->field->full_join_field)) {
      *field_name = fj_field->real_field_name;
      *table_name = fj_field->real_table_name;
      *db_name = fj_field->real_db_name;
    }
  }
}