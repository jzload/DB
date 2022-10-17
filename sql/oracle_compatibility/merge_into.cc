#include "sql/oracle_compatibility/merge_into.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <new>


#include "my_alloc.h"
#include "my_bitmap.h"
#include "my_dbug.h"
#include "sql/auth/auth_acls.h"
#include "sql/auth/auth_common.h"  // check_grant, check_access
#include "sql/binlog.h"  // mysql_bin_log
#include "sql/debug_sync.h"  // DEBUG_SYNC
#include "sql/derror.h"      // ER_THD
#include "sql/field.h"       // Field
#include "sql/item.h"            // Item
//#include "sql/locked_tables_list.h"
#include "sql/mem_root_array.h"
#include "sql/opt_explain.h"  // Modification_plan
#include "sql/opt_explain_format.h"
#include "sql/opt_trace.h"  // Opt_trace_object
#include "sql/opt_trace_context.h"
#include "sql/protocol.h"
#include "sql/query_options.h"
#include "sql/select_lex_visitor.h"
#include "sql/sql_base.h"  // check_record, fill_record
#include "sql/sql_bitmap.h"
#include "sql/sql_data_change.h" // COPY_INFO
#include "sql/sql_error.h"
#include "sql/sql_executor.h"
#include "sql/sql_lex.h"
#include "sql/sql_optimizer.h"  // build_equal_items, substitute_gc
#include "sql/sql_resolver.h"   // setup_order
#include "sql/sql_view.h"       // check_key_in_view
#include "sql/system_variables.h"
#include "sql/table.h"                     // TABLE
#include "sql/table_trigger_dispatcher.h"  // Table_trigger_dispatcher
#include "sql/sql_update.h"
#include "sql/sql_insert.h"
#include "sql/sql_resolver.h"
#include "sql/uniques.h"  // Unique_on_insert

#include "zsql_features.h"

bool setup_merge_where_cond(THD *thd, Item **where_cond, SELECT_LEX *select);
static Name_resolution_context *create_context(THD *thd, TABLE_LIST *table_list,
                                        bool resolve_in_single_table);

Sql_cmd *PT_merge::make_cmd(THD *thd) {
  DBUG_ASSERT(0 != (merge_option &
              (ORA_MERGE_INTO_WITH_INSERT | ORA_MERGE_INTO_WITH_UPDATE)));

  LEX *const lex = thd->lex;
  SELECT_LEX *const select = lex->current_select();
  Parse_context pc(thd, select);

  lex->duplicates = DUP_ERROR;
  lex->set_ignore(false);
  lex->sql_command = SQLCOM_MERGE_INTO;

  // MERGE INTO set the mdl lock MDL_SHARED_WRITE and table lock TL_WRITE_DEFAULT
  // for target table in contextualize of top join table.
  if (m_join_table->contextualize(&pc))
    return nullptr;

  // When there is only update clause,
  // convert "target_table right join source_table" to
  // "source_table straight join target_table".
  // The fixed order ensures the correct use of join cache.
  m_join_table->convert_to_straight_join();

  // Context is normally set after contextualize from_clause and
  // from_clause in MERGE INTO is the top right-join table.
  select->context.table_list =
    select->context.first_name_resolution_table =
    select->table_list.first;

  // contextualize "when not matched then insert ..." part
  if (contextualize_insert_body(thd, &pc, select))
    return nullptr;

  // contextualize "when matched then update set ..." part
  if (contextualize_update_body(thd, &pc, select))
    return nullptr;

  return new (thd->mem_root) Sql_cmd_merge(merge_option,
                                           m_insert_column_list, m_insert_value_list,
                                           opt_where_clause_insert,
                                           m_update_column_list, m_update_value_list,
                                           opt_where_clause_update);
}

/**
 * Contextualize "when matched then update set ..." part of MERGE INTO
 * 1. contextualize column and value list
 * 2. itemize where-clause if it exists
 */
bool PT_merge::contextualize_update_body(THD *, Parse_context *pc,
                                         SELECT_LEX *const select) {
  if (!(merge_option & ORA_MERGE_INTO_WITH_UPDATE))
    return false;

  if (m_update_column_list->contextualize(pc))
    return true;

  // parsing_place used in explain format=json to mark subqueries
  // If there are subqueries in value list, the title of subqueries will be
  // "update_value_subqueries"
  select->parsing_place = CTX_UPDATE_VALUE;
  if (m_update_value_list->contextualize(pc))
    return true;

  if (opt_where_clause_update) {
    // If only update clauses exists, update-where will be push down to select in
    // the future and subqueries of update-where will be processed as nomal where.
    // Otherwise, update-where will be processed after select (like having cond)
    // and the title of subqueries in update-where will be "update_clause_where_subqueries"
    if (merge_option & ORA_MERGE_INTO_WITH_INSERT)
      select->parsing_place = CTX_MERGE_INTO_UPDATE_WHERE;
    else
      select->parsing_place = CTX_WHERE;

    if (opt_where_clause_update->itemize(pc, &opt_where_clause_update))
      return true;
  }

  select->parsing_place = CTX_NONE;
  return false;
}

/**
 * Contextualize "when not matched then insert ..." part of MERGE INTO
 * 1. contextualize column and value list
 * 2. itemize where-clause if it exists
 * 3. insert-where should be resolved in source table only, so create a new
 *    context and change context of insert-where.
 *    e.g. merge into t using s on(...) when not matched then ... where (t.a>s.a)
 *    t.a in insert-where will nerver fetch a value, so change the context of
 *    insert-where and failure will happen in fix_fields.
 */
bool PT_merge::contextualize_insert_body(THD *thd, Parse_context *pc,
                                         SELECT_LEX *const select) {
  if (!(merge_option & ORA_MERGE_INTO_WITH_INSERT))
    return false;

  if (m_insert_column_list->contextualize(pc))
    return true;

  // If there are subqueries in value list, the title of subqueries will be
  // "insert_values_subqueries"
  select->parsing_place = CTX_INSERT_VALUES;
  if (m_insert_value_list->contextualize(pc))
    return true;

  if (opt_where_clause_insert) {
    // If only insert clauses exists, insert-where will be push down to select in
    // the future and subqueries of insert-where will be processed as nomal where.
    // Otherwise, insert-where will be processed after select (like having cond)
    // and the title of subqueries in insert-where will be "insert_clause_where_subqueries"
    if (merge_option & ORA_MERGE_INTO_WITH_UPDATE)
      select->parsing_place = CTX_MERGE_INTO_INSERT_WHERE;
    else
      select->parsing_place = CTX_WHERE;

    if (opt_where_clause_insert->itemize(pc, &opt_where_clause_insert))
      return true;

    // Create a new context only have source table, excluding the target table.
    // Because fields in insert-where clause cannot come from target table,
    // as Oracle performace.
    TABLE_LIST *source_table = select->table_list.first->next_local;
    Name_resolution_context *context_source = create_context(thd, source_table,
                                                             false);
    if (nullptr == context_source) return true;

    // Item::change_context_processor will never return true
    opt_where_clause_insert->walk(&Item::change_context_processor,
                                  enum_walk::POSTFIX, (uchar *)context_source);
  }

  select->parsing_place = CTX_NONE;
  return false;
}



bool Sql_cmd_merge::precheck(THD *thd) {
  /*
    The first table is the target table which should have the
    needed privilege corresponding to optional insert or update ops.
  */
  TABLE_LIST *target_table = lex->query_tables;
  ulong privilege = SELECT_ACL |
      ((merge_option & ORA_MERGE_INTO_WITH_INSERT) ? INSERT_ACL : 0 ) |
      ((merge_option & ORA_MERGE_INTO_WITH_UPDATE) ? /*DELETE_ACL |*/ UPDATE_ACL : 0 );

  // The first table will be checked for the specific privilege,
  // and the next_global table will be checked for the SELECT privilege.
  if (check_one_table_access(thd, privilege, target_table)) return true;

  // The special privilege of base table derived table does not checked above.
  // Due to insert/update character (cannot insert into a derived table or update
  // a derived table), it is ok to use check_one_table_access for insert/update.
  // But for MERGE INTO, When target table is derived table, the privilege of
  // base table in derived table need to check in addition.
  // e.g. merge into (select * from t1) dt using ...
  // check_one_table_access only check specially privilege of dt and SELECT_ACL of t1.
  // specially privilege of t1 is checked below.
  while (target_table && target_table->is_derived()) {
    target_table = target_table->derived_unit()->first_select()->get_table_list();
    if (check_single_table_access(thd, privilege, target_table, false))
      return true;
  }

  return false;
}

/**
 * check whether target table of MERGE INTO is a view with check option
 * (including where cond from underlying view works like check option)
 * If target table has check option, return false.
 * View with check option cannot be merge-into target table in MySQL and Oracle.
 */
static bool check_view_with_check_option(THD *thd, TABLE_LIST *table_list) {
  if (!table_list->is_view())
    return false;

  if (table_list->prepare_check_option_for_merge_into(thd, false)) {
    my_error(ER_MERGE_INTO, MYF(0), "view with check option cannot be modified");
    return true;
  }

  return false;
}

/**
 * Push down insert/update where to select if only one of insert or update clause
 * exists.
 */
void Sql_cmd_merge::push_down_where_to_select(SELECT_LEX *select) {
  if (opt_where_clause_update &&
      //(merge_option & ORA_MERGE_INTO_WITH_UPDATE) &&
      !(merge_option & ORA_MERGE_INTO_WITH_INSERT)) {
    // if only update clause exists, push down update-where to select
    select->set_where_cond(opt_where_clause_update);
    // clear insert-where
    opt_where_clause_update = nullptr;
  } else if (opt_where_clause_insert &&
             //(merge_option & ORA_MERGE_INTO_WITH_INSERT) &&
             !(merge_option & ORA_MERGE_INTO_WITH_UPDATE)) {
    // if only insert clause exists, push down insert-where to select
    select->set_where_cond(opt_where_clause_insert);
    // clear update-where
    opt_where_clause_insert = nullptr;
  }
}

bool Sql_cmd_merge::prepare_inner(THD *thd) {
  DBUG_TRACE;

  Prepare_error_tracker tracker(thd);

  SELECT_LEX_UNIT *const unit = lex->unit;
  SELECT_LEX *const select = lex->select_lex;

  Opt_trace_context *const trace = &thd->opt_trace;
  Opt_trace_object trace_wrapper(trace);
  Opt_trace_object trace_prepare(trace, "merge_preparation");
  trace_prepare.add_select_number(select->select_number);

  // Push down where to select if there is only update or insert before create
  // Query_result
  push_down_where_to_select(select);

  TABLE_LIST *const target_top_table = lex->query_tables;

  target_top_table->set_merge_into_target_table();

  result = new (thd->mem_root)
      Query_result_merge(target_top_table,
                        insert_column_list, insert_value_list, opt_where_clause_insert,
                        update_column_list, update_value_list, opt_where_clause_update,
                        merge_option);
  if (result == nullptr) return true; /* purecov: inspected */

    // The former is for the pre-iterator executor; the latter is for the
    // iterator executor.
    // TODO(sgunders): Get rid of this when we remove Query_result.
  select->set_query_result(result);
  select->master_unit()->set_query_result(result);

  // delay to call apply_local_transforms()
  // which may cause join_cond is pushed to where
  select->skip_local_transforms = true;

  DBUG_ASSERT(!unit->is_union());
  DBUG_ASSERT(unit->is_simple());


  // add option: SELECT_NO_JOIN_CACHE: set no_jbuf_after=0 and turn off BNL/BKA
  //             to avoid store rows of target table in join buffer.
  // (no_jbuf_after: Do not use join buffering after the table with this number)
  // remove option: OPTION_BUFFER_RESULT: buffer the result of select, e.g.,
  //                insert into t select * from t;
  //                1. buffer the result of select. 2. do insert
  if (unit->prepare(thd, result, SELECT_NO_JOIN_CACHE, OPTION_BUFFER_RESULT))
    return true;

  /*
  // 这部分代码没有实际功能,leaf table 第一个表是目标表基表，check_view_privileges
  // 检查该表和其属于的view的权限，检查到第二层view，顶层view不检查（已在precheck中检查）
  // 但是check_grant检查权限时，want_access &= ~sctx->master_access(db_name);
  // 这里用到的sctx为检查的table下的security_ctx（设置时间：open_tables_for_query后，
  // parse_view_definition中， view下table的security_ctx被设置与view相同），
  // 认为权限已有（即view权限），不再执行table_hash_search查找table权限，
  // 所以check_view_privileges没有实际作用
  // 目前mysql中：有v1权限，无t1权限，可以对v1进行增改，所以这个函数的意义是什么？
  TABLE_LIST *target_base_table = select->leaf_tables;
  if (select->derived_table_count > 1) {
    ulong privilege = SELECT_ACL |
      ((merge_option & ORA_MERGE_INTO_WITH_INSERT) ? INSERT_ACL : 0 ) |
      ((merge_option & ORA_MERGE_INTO_WITH_UPDATE) ?  UPDATE_ACL : 0 );
    if (select->check_view_privileges(thd, privilege, SELECT_ACL)) return true;
  } */

  // Leaf tables is set in SELECT_LEX::setup_tables (make_leaf_tables).
  // Because leaf tables is set in order of local list and target table is first
  // local table of top select, base table of target table is first table in leaf
  // tables.
  TABLE_LIST *target_base_table = select->leaf_tables;

  if ((merge_option & ORA_MERGE_INTO_WITH_UPDATE) &&
      !target_base_table->is_updatable()) {
    //my_error(ER_MERGE_INTO, MYF(0), "The target table of MERGE INTO is not updatable");
    my_error(ER_NON_UPDATABLE_TABLE, MYF(0), target_base_table->alias, "MERGE INTO");
    return true;
  }
  if ((merge_option & ORA_MERGE_INTO_WITH_INSERT) &&
      !target_base_table->is_insertable()) {
    //my_error(ER_MERGE_INTO, MYF(0), "The target table of MERGE INTO is not insertable");
    my_error(ER_NON_INSERTABLE_TABLE, MYF(0), target_base_table->alias, "MERGE INTO");
    return true;
  }

  // ORA-38106: MERGE not supported on join view or view with INSTEAD OF trigger.
  // Target table not supported 1.join view 2.derived table with multi base tables
  if (target_top_table->is_multiple_tables()) {
    my_error(ER_VIEW_MULTIUPDATE, MYF(0), target_top_table->view_db.str,
              target_top_table->view_name.str);
    return true;
  }

  // Target base table cannot be found twice in global list,
  // that means target base table is unique table in global list.
  // Except for target table in a materialized source table what does not matter,
  // target table cannot be used in any subqueries, because target table will be
  // modified in the future and associate subqueries cannot has a stable result.
  TABLE_LIST * duplicate = nullptr;
  duplicate = unique_table(target_base_table, target_top_table->next_global, false);
  if (duplicate != NULL) {
      my_error(ER_MERGE_INTO, MYF(0), "target table is misused in subqueries "\
                                      "or non-materialized source table");
      return true;
  }

  // target table cannot be view with check option
  if (check_view_with_check_option(thd, target_top_table))
    return true;

  thd->want_privilege = SELECT_ACL;

  TABLE *table = target_base_table->table;

  // prepare insert/update subclauses
  if (prepare_update_insert_clause(thd, select, target_top_table, table))
    return true;

  // write_set is set all because all fields should be recorded in the binlog
  // for master-slave replication, even if it is a update-only command.
  // Especially when the table has no key, full recorded fields help to
  // find the right record to update in a slave.
  //bitmap_set_all(table->write_set);

  if (select->apply_local_transforms(thd, false))
    return true; /* purecov: inspected */

  return false;
}

/**
 * change context of list of Item_field.
 */
inline bool change_context_in_modified_fields(List<Item> *item_list,
                                       Name_resolution_context *ctx) {
  Item *item = nullptr;
  List_iterator_fast<Item> it_modify(*item_list);
  while ((item = it_modify++)) {
    if (Item::FIELD_ITEM != item->type())
    {
      my_error(ER_MERGE_INTO, MYF(0), "this field cannot be modified");
      return true;
    }
    (down_cast<Item_field *>(item))->context = ctx;
  }
  return false;
}

inline bool check_modified_field_specified_twice(THD *thd,
                                                 const bool check_unique,
                                                 List<Item> *column_list,
                                                 TABLE *table) {
  if (check_unique &&
      column_list->elements > bitmap_bits_set(table->write_set)) {
    for (auto i = column_list->cbegin();
          i != column_list->cend(); ++i) {
      // Skipping views means that we only have FIELD_ITEM.
      const Item &item1 = *i;
      for (auto j = std::next(i); j != column_list->cend(); ++j) {
        const Item &item2 = *j;
        if (item1.eq(&item2, true)) {
          my_error(ER_FIELD_SPECIFIED_TWICE, MYF(0), item1.item_name.ptr());
          break;
        }
      }
    }
    DBUG_ASSERT(thd->is_error());
    return true;
  }

  return false;
}

inline bool check_and_validate_gc_assignment(List<Item> *fields, List<Item> *values,
                                             TABLE *table) {
  if ((table->has_gcol() || table->gen_def_fields_ptr) &&
      validate_gc_assignment(fields, values, table))
    return true;

  return false;
}

bool Sql_cmd_merge::setup_update_clause(THD *thd, SELECT_LEX *select,
                                        TABLE_LIST *table_list) {
  table_map map = table_list->map();
  if (setup_fields(thd, Ref_item_array(), *update_column_list,
                    UPDATE_ACL /*& DELETE_ACL*/, nullptr, false, true) ||
      check_fields(thd, *update_column_list) ||
      check_valid_table_refs(table_list, *update_column_list, map) ||
      setup_fields(thd, Ref_item_array(), *update_value_list,
                    SELECT_ACL, nullptr, true, false) ||
      setup_merge_where_cond(thd, &opt_where_clause_update, select))
    return true;
  return false;
}

/**
 * prepare insert/update subclauses of merge into
 * input: thd
 *        select: TOP SELECT_LEX of merge into (right join)
 *        table_list: top target table
 *        table : base target table
 */
bool Sql_cmd_merge::prepare_update_insert_clause(THD *thd, SELECT_LEX *select,
                                                 TABLE_LIST *table_list,
                                                 TABLE *table) {
    // Only fix the insert colums and update columns in the target table context
  Name_resolution_context *context_target = create_context(thd, table_list, true);
    if (nullptr == context_target) return true;

  // nothing will be done if there is no update clause
  if ((merge_option & ORA_MERGE_INTO_WITH_UPDATE) &&
      prepare_update_clause(thd, select, context_target, table_list, table))
    return true;

  // nothing will be done if there is no insert clause
  if ((merge_option & ORA_MERGE_INTO_WITH_INSERT) &&
      prepare_insert_clause(thd, select, context_target, table_list, table))
    return true;

  return false;
}

bool Sql_cmd_merge::prepare_update_clause(THD *thd, SELECT_LEX *select,
                                          Name_resolution_context *ctx,
                                          TABLE_LIST *table_list,
                                          TABLE *table) {
  //if (!(merge_option & ORA_MERGE_INTO_WITH_UPDATE))
    //return false;

  DBUG_ASSERT(table->write_set != &table->tmp_set);
  // Verify it's not used
  DBUG_ASSERT(bitmap_is_clear_all(&table->tmp_set));

  // mark fields of join cond in tmp_set
  Item *join_cond = table_list->join_cond();
  if (join_cond) {
    join_cond->walk(&Item::add_field_to_set_processor_for_merge_into,
                    enum_walk::SUBQUERY_POSTFIX,
                    reinterpret_cast<uchar *>(table));
  }

  if (change_context_in_modified_fields(update_column_list, ctx))
    return true;

  // mark fields of update colums in write_set
  if (setup_update_clause(thd, select, table_list))
    return true;

  // compare tmp_set and write_set to avoid update the fields in join cond
  if (bitmap_is_overlapping(&table->tmp_set, table->write_set)) {
    my_error(ER_MERGE_INTO, MYF(0), "fields in on condition cannot be updated");
    return true;
  }

  bitmap_clear_all(&table->tmp_set);

  // We currently don't check for unique columns when inserting via a view.
  if (check_modified_field_specified_twice(thd, !table_list->is_view(),
                                           update_column_list, table))
    return false;

  if (check_and_validate_gc_assignment(update_column_list, update_value_list, table))
    return true;

  Opt_trace_context *const trace = &thd->opt_trace;
  if (prepare_partial_update(trace, update_column_list, update_value_list))
    return true; /* purecov: inspected */

  return false;
}

bool Sql_cmd_merge::check_insert_values_count(TABLE *table) {
  uint field_count = 0;

  // If the SQL command was an merge into ... when not matched then INSERT ...
  // without column list (like "INSERT VALUES (1, 2);",
  // we ignore any columns that is hidden from the user.
  if (insert_column_list->elements == 0) {
    field_count = table->s->fields;
    for (uint i = 0; i < table->s->fields; ++i) {
      if (table->s->field[i]->is_hidden_from_user()) field_count--;
    }
  }
  else {
    field_count = insert_column_list->elements;
  }

  /*
    Values for all fields in table must be specified, unless there is
    no field list and no value list is supplied (means all default values).
  */
  if (insert_value_list->elements != field_count &&
    !(insert_value_list->elements == 0 && insert_column_list->elements == 0)) {
    my_error(ER_WRONG_VALUE_COUNT, MYF(0));
    return true;
  }

  return false;
}

inline bool check_insert_view_fields(List<Item> *list, TABLE_LIST *table_list) {
  if (table_list->is_view()) {
    DBUG_ASSERT(list);
    if (list->elements == 0 &&
        insert_view_fields(list, table_list))
      return true;
  }
  return false;
}

bool Sql_cmd_merge::setup_insert_clause(THD *thd, SELECT_LEX *select,
                                        TABLE_LIST *table_list) {
  table_map map = table_list->map();
  if (setup_fields(thd, Ref_item_array(), *insert_column_list,
                    INSERT_ACL, nullptr, false, true) ||
      check_valid_table_refs(table_list, *insert_column_list, map) ||
      setup_fields(thd, Ref_item_array(), *insert_value_list,
                    SELECT_ACL, nullptr, true, false) ||
      setup_merge_where_cond(thd, &opt_where_clause_insert, select))
    return true;
  return false;
}

bool Sql_cmd_merge::prepare_insert_clause(THD *thd, SELECT_LEX *select,
                                          Name_resolution_context *ctx,
                                          TABLE_LIST *table_list,
                                          TABLE *table) {
  //if (!(merge_option & ORA_MERGE_INTO_WITH_INSERT))
    //return false;

  DBUG_ASSERT(table->write_set != &table->tmp_set);
  // Verify it's not used
  //DBUG_ASSERT(bitmap_is_clear_all(&table->tmp_set));

  bitmap_copy(&table->tmp_set, table->write_set);
  bitmap_clear_all(table->write_set);

  if (check_insert_view_fields(insert_column_list, table_list))
    return true;

  // check whether thr count of insert values matches the count of insert columns
  if (check_insert_values_count(table))
    return true;

  if (change_context_in_modified_fields(insert_column_list, ctx))
    return true;

  if (setup_insert_clause(thd, select, table_list))
    return true;

  // We currently don't check for unique columns when inserting via a view.
  //const bool check_unique = !table_list->is_view();

  if (check_modified_field_specified_twice(thd, !table_list->is_view(),
                                           insert_column_list, table))
    return false;

  if (table->vfield) table->mark_generated_columns(false);

  if (check_and_validate_gc_assignment(insert_column_list, insert_value_list, table))
    return true;

  // Check All not used columns in table have default values in advance
  // although all the values will be checked after fill record.
  if (table_list->is_view() &&
      check_view_insertability(thd, table_list, select->leaf_tables)) {
    my_error(ER_NON_INSERTABLE_TABLE, MYF(0), table_list->alias, "MERGE INTO");
    return true;
  }

  if ((table_list->is_view() || insert_column_list->elements == 0) &&
      insert_value_list->elements > 0)
    bitmap_set_all(table->write_set);

  // save table->write_set
  bitmap_copy(&table->def_fields_set_insert_saved, table->write_set);
  bitmap_union(table->write_set, &table->tmp_set);
  bitmap_clear_all(&table->tmp_set);

  return false;
}

/**
 * Check whether view has check option.
 * If upper view is cascaded, where condition of lower view will be treated as
 * check option even if lower view does not have with check option.
 * example of cascaded:
 * create v1 as select * from t1 where (cond1);
 * create v2 as select * from t1 with cascaded option;
 * cond1 will be checked when modified v2
 *
 * Reference function: TABLE_LIST::prepare_check_option
 */
bool TABLE_LIST::prepare_check_option_for_merge_into(THD *thd, bool is_cascaded) {
  DBUG_ASSERT(is_view());

  if ((is_cascaded || with_check) && derived_where_cond)
    return true;

  is_cascaded |= (with_check == VIEW_CHECK_CASCADED);

  // target table of merge into cannot be a joined view, so next_local must be null
  if (merge_underlying_list->is_view() &&
      merge_underlying_list->prepare_check_option_for_merge_into(thd, is_cascaded))
    return true; /* purecov: inspected */
  DBUG_ASSERT(!merge_underlying_list->next_local);

  return false;
}

bool setup_merge_where_cond(THD *thd, Item **where_cond, SELECT_LEX *select) {
  if (!(where_cond && *where_cond))
    return false;

  DBUG_ASSERT((*where_cond)->is_bool_func());
  select->resolve_place = SELECT_LEX::RESOLVE_CONDITION;
  //thd->where = "where clause";
  if ((!(*where_cond)->fixed &&
      (*where_cond)->fix_fields(thd, where_cond)) ||
      (*where_cond)->check_cols(1))
    return true;

  // Simplify the where condition if it's a const item
  if ((*where_cond)->const_item() && !thd->lex->is_view_context_analysis() &&
      !(*where_cond)->walk(&Item::is_non_const_over_literals,
                           enum_walk::POSTFIX, NULL) &&
      simplify_const_condition(thd, where_cond))
    return true;

  select->resolve_place = SELECT_LEX::RESOLVE_NONE;
  return false;
}

bool Query_result_merge::initial_unique_table(){
  if (!m_unique &&
      (!(m_unique = new (*THR_MALLOC) Unique_on_insert(table->file->ref_length)) ||
      m_unique->init())) {
    /* purecov: begin inspected */
    destroy(m_unique);
    my_error(ER_OOM, MYF(0));
    return true;
    /* purecov: end */
  }
  return false;
}

bool Query_result_merge::prepare_copy_info(TABLE *table) {
  if (merge_option & ORA_MERGE_INTO_WITH_INSERT) {
    if (info.add_function_default_columns(table, table->write_set))
      return true;
    if (info.add_function_default_columns(table, &table->def_fields_set_insert_saved))
      return true;
  }

  if (merge_option & ORA_MERGE_INTO_WITH_UPDATE) {
    if (update.add_function_default_columns(table, table->write_set))
      return true;

    if ((table->file->ha_table_flags() & HA_PARTIAL_COLUMN_READ) != 0 &&
        update.function_defaults_apply(table)) {
      /*
        A column is to be set to its ON UPDATE function default only if other
        columns of the row are changing. To know this, we must be able to
        compare the "before" and "after" value of those columns
        (i.e. records_are_comparable() must be true below). Thus, we must read
        those columns:
      */
      bitmap_union(table->read_set, table->write_set);
    }
  }
  return false;
}

bool Query_result_merge::prepare(THD *, List<Item> &, SELECT_LEX_UNIT *u) {
// 1、COPY_INFO info、update调用add_function_default_columns，增加从函数获得默认值的列
  unit = u;

  table = u->first_select()->leaf_tables->table;

  table->next_number_field = table->found_next_number_field;

  //if (prepare_copy_info(table)) return true;

// 2、被更新的表不能使用覆盖索引
  // UPDATE/DELETE operations requires full row from base table, disable covering key
  // INSERT operations can use covering keys.
  if (merge_option & ORA_MERGE_INTO_WITH_UPDATE) {
    table->no_keyread = 1;
    table->covering_keys.clear_all();
  }

// 3、table->triggers->mark_fields TRG_EVENT_UPDATE/TRG_EVENT_INSERT
  if (table->triggers &&
      table->triggers->mark_fields(TRG_EVENT_UPDATE) &&
      table->triggers->mark_fields(TRG_EVENT_INSERT))
    return true;
      //是这里做还是后面做？是需要对应到update或insert操作吗？

// 4、设置table->file->ha_extra
  /*
    These operation is for NDB:
    HA_EXTRA_DELETE_CANNOT_BATCH
    HA_EXTRA_UPDATE_CANNOT_BATCH
    Because zsql do not support NDB yet,
    merge into operation do not set NDB operation which is set in update and insert.
  */

// 5、Table field reset_warnings、reset_tmp_null
  for (Field **next_field = table->field; *next_field; ++next_field) {
    (*next_field)->reset_warnings();
    (*next_field)->reset_tmp_null();
  }

  // 6、创建bitmap用来存储分区read_partitions
/*  if (table->part_info && !parts_map_has_initialized) {
    if (table->part_info->init_partition_bitmap(&saved_parts_map, thd->mem_root))
      return false;
    parts_map_has_initialized = true;
  }*/
  return false;
}

bool Query_result_merge::filter_solved_record() {
  if (m_unique) {
    uint32 new_part_id = 0;
    bool need_set_read_partitions = false;
    if (table->part_info) {
      int error;
      longlong new_func_value;
      if (unlikely((error = table->part_info->get_partition_id(
                                                        table->part_info,
                                                        &new_part_id,
                                                        &new_func_value)))) {
        table->part_info->err_value = new_func_value;
        return error;
      }
      if (!bitmap_is_set(&table->part_info->read_partitions, new_part_id)) {
        need_set_read_partitions = true;
        bitmap_set_bit(&table->part_info->read_partitions, new_part_id);
      }

/*      bitmap_copy(&saved_parts_map, &table->part_info->read_partitions);
      bitmap_set_all(&table->part_info->read_partitions); */
    }
    table->file->position(table->record[0]);
    if (table->part_info) {
//      bitmap_copy(&table->part_info->read_partitions, &saved_parts_map);

      int2store(table->file->ref, new_part_id);
      if (need_set_read_partitions)
        bitmap_clear_bit(&table->part_info->read_partitions, new_part_id);
    }
    return m_unique->unique_add(table->file->ref);
  }
  return false;
}

bool Query_result_merge::check_solved_record() {
  if (m_unique) {
    table->file->position(table->record[0]);
    return m_unique->unique_check(table->file->ref);
  }
  return false;
}

bool Unique_on_insert::unique_check(void *ptr) {
  int error;

  Field *key = *m_table->visible_field_ptr();
  if (key->store((const char *)ptr, m_size, &my_charset_bin) != TYPE_OK)
    return true; /* purecov: inspected */

  if (!check_unique_constraint(m_table)) return true;

    /* Initialize the index used for finding the deleting recored. */
  error = m_table->file->ha_index_init(0, 0);
  if (error != 0) {
    return error;
  }

  /* below delete record by index */
  uchar used_key[MAX_KEY_LENGTH];
  key_copy(used_key, m_table->record[0], m_table->key_info,
         m_table->key_info->key_part->store_length);
  error = m_table->file->ha_index_read_map(m_table->record[0], used_key,
                                           (key_part_map)1,//only one key
                                           HA_READ_KEY_EXACT);

  switch (error) {
    case HA_ERR_END_OF_FILE:
    case HA_ERR_KEY_NOT_FOUND:
      error = false;
      break;
    default:
      error = true;
  }

  m_table->file->ha_index_end();
  return error;
}

bool Query_result_merge::start_execution(THD *thd) {
  if (!bulk_insert_started && thd->locked_tables_mode <= LTM_LOCK_TABLES &&
      !thd->lex->is_explain()) {
    DBUG_ASSERT(!bulk_insert_started);
    // TODO: Is there no better estimation than 0 == Unknown number of rows?
    table->file->ha_start_bulk_insert((ha_rows)0);
    bulk_insert_started = true;
  }

  DBUG_ASSERT(table->reginfo.lock_type == TL_WRITE);
  table->mark_columns_needed_for_merge(thd, merge_option);

  if (table->setup_partial_update()) return true;

  return false;
}

void Query_result_merge::cleanup(THD *thd) {

  if (table) {
    table->next_number_field = 0;
    //table->file->ha_reset();
    table->no_cache = 0;//updategg
  }

  if (m_unique) {
    m_unique->cleanup();
    destroy(m_unique);
    m_unique = nullptr;
  }

  thd->check_for_truncated_fields = CHECK_FIELD_IGNORE;  // Restore this setting
  //DBUG_ASSERT(
     // trans_safe || updated_rows == 0 ||
      //thd->get_transaction()->cannot_safely_rollback(Transaction_ctx::STMT)); // update
}

static bool optimize_where_clause(THD *thd, Item **where, Item::cond_result &cond_value) {
  if (thd->stmt_arena->is_regular())
    *where = (*where)->copy_andor_structure(thd);

  COND_EQUAL *cond_equal = NULL;

  if (optimize_cond(thd, where, &cond_equal, nullptr, &cond_value)) {
    DBUG_PRINT("error", ("Error from optimize_cond"));
    return true;
  }
  if (*where &&
      (*where)->walk(&Item::cast_incompatible_args, enum_walk::POSTFIX, nullptr))
    return true; // failure of cast_incompatible_args caused by oom

  return false;
}

bool Query_result_merge::optimize() {
  DBUG_TRACE;

  // 1、创建封装了临时表的对象 Unique_on_insert
  if (initial_unique_table())
    return false;
  // every m_unique is a new table, no need to reset it

  // 2、optimize where cond in update/insert clause
  SELECT_LEX *const select = unit->first_select();
  JOIN *const join = select->join;
  THD *thd = join->thd;

  if (opt_where_clause_insert) {
    Item::cond_result insert_cond_value;
    if (optimize_where_clause(thd, &opt_where_clause_insert, insert_cond_value))
      return true;

    if (insert_cond_value == Item::COND_FALSE)  // Impossible where
    {
      merge_option &= (~ORA_MERGE_INTO_WITH_INSERT);
    }
  }

  if (opt_where_clause_update) {
    Item::cond_result update_cond_value;
    if (optimize_where_clause(thd, &opt_where_clause_update, update_cond_value))
      return true;
    if (update_cond_value == Item::COND_FALSE)  // Impossible where
    {
      merge_option &= (~ORA_MERGE_INTO_WITH_UPDATE);
    }
  }

  if ((table->file->ha_table_flags() & HA_STATS_RECORDS_IS_EXACT) &&
      0.0 == table->file->stats.records)
    all_ops_insert = true;

  if (prepare_copy_info(table)) return true;

  return false;
}

bool Query_result_merge::no_merge_result_attached() {
  return merge_option == 0;
}

bool Query_result_merge::store_insert_values(THD *thd) {
  if ((insert_columns && insert_columns->elements) ||
      !insert_values->elements)  {
    restore_record(table, s->default_values);
    // In fill_record_n_invoke_before_triggers(),
    // while optype_info is not UPDATE_OPERATION, is_row_changed must be nullptr,
    // so KW error(NPD.CONST.CALL) is a misreport and ignore it.
    /* KW_SUPPRESS_START:*:ignore NPD.CONST.CALL */
    if (validate_default_values_of_unset_fields(thd, table) ||
        fill_record_n_invoke_before_triggers(
                              thd, &info, *insert_columns, *insert_values, table,
                              TRG_EVENT_INSERT, table->s->fields, true, nullptr))
      return true;
    /* KW_SUPPRESS_END */
  } else {
    if (fill_record_n_invoke_before_triggers(
                            thd, table->field, *insert_values, table,
                            TRG_EVENT_INSERT, table->s->fields))
      return true;
  }

  if (check_that_all_fields_are_given_values(thd, table, table_list))
    return true;

  return false;
}

bool Query_result_merge::store_update_values(THD *thd, bool &is_row_changed) {
  store_record(table, record[1]);
  is_row_changed = false;
  if (fill_record_n_invoke_before_triggers(
                            thd, &update, *update_fields, *update_values, table,
                            TRG_EVENT_UPDATE, 0, false, &is_row_changed))
    return true;
  return false;
}

bool Query_result_merge::send_data(THD *thd, List<Item> &) {
  bool error = 0;
  // 1. The target table has null row after the join means that there is no
  //    rows in the target table matches the row from the source table.
  //    In this condition, we do the insertion operation.
  //    Otherwise, we do the update operation.
  // 2. When engine of target table is InnoDB, statistics records is not exact
  //    and flag of null row is set during query.
  // 3. When engine of target table is MyIsam, statistics records is exact,
  //    zero-row table can be detected and the join type is JT_SYSTEM.
  //    Only source table is scaned and null row flag will not be set every time
  //    before send_data. This occasion is marked by all_ops_insert.
  bool insert_op = table->has_null_row() || all_ops_insert;

  if (insert_op) {
    if ((merge_option & ORA_MERGE_INTO_WITH_INSERT) &&
        (!opt_where_clause_insert || opt_where_clause_insert->val_int()))
      error = insert_data(thd);
  }
  else {
    if ((merge_option & ORA_MERGE_INTO_WITH_UPDATE) &&
        (!opt_where_clause_update || opt_where_clause_update->val_int())) {
      error = update_data(thd);
    }
    else {
      table->file->unlock_row();
    }
  }

  return error != 0 || thd->is_error();
}

bool Query_result_merge::insert_data(THD *thd) {
  DBUG_TRACE;

  Autoinc_field_has_explicit_non_null_value_reset_guard after_each_row(table);
  //thd->check_for_truncated_fields = CHECK_FIELD_WARN;
  thd->check_for_truncated_fields = CHECK_FIELD_ERROR_FOR_NULL;
  if (store_insert_values(thd))
    return true;
  //thd->check_for_truncated_fields = CHECK_FIELD_ERROR_FOR_NULL;
  /*if (thd->is_error()) {
    return true;
  }*/

  /*
   TODO: This method is supposed to be called only once per-statement. But
         currently it is called for each row inserted by INSERT SELECT. Which
         looks like unnessary and results in resource wastage.
  */
  //prepare_triggers_for_insert_stmt(thd, table);

  if (invoke_table_check_constraints(thd, table)) {
    // return false when IGNORE clause is used.
    return thd->is_error();
  }

  table->reset_null_row();

  // When info.handle_duplicates = DUP_ERROR, the 4th para of write_record
  // will nerver be used.
  // Use info as 4th para instead of NULL to avoid KW error(NPD.CONST.CALL)
  bool error = write_record(thd, table, &info, &info/* NULL */);

  DEBUG_SYNC(thd, "create_select_after_write_rows_event");

  // restore the ref (row id or unique key) of inserted record
  if (filter_solved_record())
    return false; //todo: It is impossible that a inserted record has a old record

  if (!error && table->triggers) {
    /*
      Restore fields of the record.
      If triggers exist then whey can modify some fields which were not
      originally touched by INSERT ... SELECT, so we have to restore
      their original values for the next row.
    */
    restore_record(table, s->default_values);
  }

  if (!error && table->next_number_field) {
    /*
      If no value has been autogenerated so far, we need to remember the
      value we just saw, we may need to send it to client in the end.
    */
    if (thd->first_successful_insert_id_in_cur_stmt == 0)  // optimization
      autoinc_value_of_last_inserted_row = table->next_number_field->val_int();
    /*
      Clear auto-increment field for the next record, if triggers are used
      we will clear it twice, but this should be cheap.
    */
    table->next_number_field->reset();
  }

  return error;
}

bool Query_result_merge::update_data(THD *thd) {
  if (table->has_updated_row()) {
    //my_error(ER_MERGE_INTO, MYF(0), "The same row cannot be updated twice");
    return false;
  }

  table->clear_partial_update_diffs();
  table->set_updated_row();//每次update时设定不知道能否阻止BNL下重复update？

  // check the ref (row id or unique key) of updated record
  // this record updated or inserted in this merge operation,
  // will not be updated again
  if (check_solved_record())
    return false;

  thd->check_for_truncated_fields = CHECK_FIELD_WARN;
  bool is_row_changed = false;
  if (store_update_values(thd, is_row_changed))
    return true;

  found_rows++;
  int error = 0;
  if (is_row_changed) {
    /*
      Existing rows in table should normally satisfy CHECK constraints. So
      it should be safe to check constraints only for rows that has really
      changed (i.e. after compare_records()).

      In future, once addition/enabling of CHECK constraints without their
      validation is supported, we might encounter old rows which do not
      satisfy CHECK constraints currently enabled. However, rejecting
      no-op updates to such invalid pre-existing rows won't make them
      valid and is probably going to be confusing for users. So it makes
      sense to stick to current behavior.
    */
    if (invoke_table_check_constraints(thd, table)) {
      return thd->is_error();
    }

    if (!updated_rows++) {
      /*
        Inform the table that we are going to update the table even
        while we may be scanning it.  This will flush the read cache
        if it's used.
      */
      table->file->ha_extra(HA_EXTRA_PREPARE_FOR_UPDATE);
    }
    if ((error = table->file->ha_update_row(table->record[1],
                                            table->record[0])) &&
        error != HA_ERR_RECORD_IS_THE_SAME) {
      updated_rows--;
      myf error_flags = MYF(0);
      if (table->file->is_fatal_error(error)) error_flags |= ME_FATALERROR;

      table->file->print_error(error, error_flags);

      /* Errors could be downgraded to warning by IGNORE */
      if (thd->is_error()) return true;
    } else {
      if (error == HA_ERR_RECORD_IS_THE_SAME) {
        error = 0;
        updated_rows--;
      }
      else { // 主键被update时，存储最新的rowid，如此时rowid有重复的，需报错
        if (filter_solved_record())
          return true;
      }

      if (!table->file->has_transactions()) {
        thd->get_transaction()->mark_modified_non_trans_table(
            Transaction_ctx::STMT);
      }
    }
  }
  if (!error && table->triggers &&
      table->triggers->process_triggers(thd, TRG_EVENT_UPDATE,
                                        TRG_ACTION_AFTER, true))
    return true;

  return false;
}

bool Query_result_merge::send_eof(THD *thd) {
  int error;
  ulonglong id, row_count;
  bool changed MY_ATTRIBUTE((unused));
  THD::killed_state killed_status = thd->killed;
  DBUG_TRACE;
  DBUG_PRINT("enter",
             ("trans_table=%d, table_type='%s'",
              table->file->has_transactions(), table->file->table_type()));

  error = (bulk_insert_started ? table->file->ha_end_bulk_insert() : 0);
  if (!error && thd->is_error()) error = thd->get_stmt_da()->mysql_errno();

  changed = (info.stats.copied || updated_rows);

  /*
    MERGE INTO on non-transactional table which changes any rows
    must be marked as unsafe to rollback.
  */
  DBUG_ASSERT(
      table->file->has_transactions() || !changed ||
      thd->get_transaction()->cannot_safely_rollback(Transaction_ctx::STMT));

  /*
    Write to binlog before commiting transaction.  No statement will
    be written by the binlog_query() below in RBR mode.  All the
    events are in the transaction cache and will be written when
    ha_autocommit_or_rollback() is issued below.
  */
  if (mysql_bin_log.is_open() &&
      (!error ||
       thd->get_transaction()->cannot_safely_rollback(Transaction_ctx::STMT))) {
    int errcode = 0;
    if (!error)
      thd->clear_error();
    else
      errcode = query_error_code(thd, killed_status == THD::NOT_KILLED);
    if (thd->binlog_query(THD::ROW_QUERY_TYPE, thd->query().str,
                          thd->query().length, stmt_binlog_is_trans(), false,
                          false, errcode)) {
      table->file->ha_release_auto_increment();
      return 1;
    }
  }
  table->file->ha_release_auto_increment();

  if (error) {
    myf error_flags = MYF(0);
    if (table->file->is_fatal_error(my_errno())) error_flags |= ME_FATALERROR;

    table->file->print_error(my_errno(), error_flags);
    return 1;
  }

  /*
    For the strict_mode call of push_warning above results to set
    error in Diagnostic_area. Therefore it is necessary to check whether
    the error was set and leave method if it is true. If we didn't do
    so we would failed later when my_ok is called.
  */
  if (thd->get_stmt_da()->is_error()) return true;

  //输出统计数据
  //Query OK, %ld rows affected (xx.xx sec)
  //Rows merged: %ld  Rows matched: %ld  Changed: %ld  Insert records: %ld  Duplicates: %ld  Warnings: %ld
  //

  char buff[260];

  snprintf(buff, sizeof(buff), /* ER_THD(thd, ER_MERGE_INTO_INFO) */
           "Rows merged: %ld  Rows matched: %ld  Rows changed: %ld  Rows inserted: %ld  Warnings: %ld",
           (long)(info.stats.records + found_rows),
           (long)(found_rows),
           (long)(updated_rows),
           (long)(info.stats.records),
           (long)thd->get_stmt_da()->current_statement_cond_count());

  // set the value of the AUTOINCREMENT column for the last INSERT
  id = (thd->first_successful_insert_id_in_cur_stmt > 0)
           ? thd->first_successful_insert_id_in_cur_stmt
           : (thd->arg_of_last_insert_id_function
                  ? thd->first_successful_insert_id_in_prev_stmt
                  : (info.stats.copied ? autoinc_value_of_last_inserted_row
                                       : 0));

  //total affected rows
  row_count= info.stats.copied + //info.stats.deleted +
              (thd->get_protocol()->has_client_capability(CLIENT_FOUND_ROWS)
                   ? found_rows//(info.stats.touched + found_rows)
                   : updated_rows);//(info.stats.updated + updated_rows));

  my_ok(thd, row_count, id/* last insert id */, buff);

  /*
    If we have inserted into a VIEW, and the base table has
    AUTO_INCREMENT column, but this column is not accessible through
    a view, then we should restore LAST_INSERT_ID to the value it
    had before the statement.
  */
  if (table_list != NULL && table_list->is_view() &&
      !table_list->contain_auto_increment)
    thd->first_successful_insert_id_in_cur_stmt =
        thd->first_successful_insert_id_in_prev_stmt;

  return false;
}

void Query_result_merge::abort_result_set(THD *thd) {
  DBUG_TRACE;
  /*
    If the creation of the table failed (due to a syntax error, for
    example), no table will have been opened and therefore 'table'
    will be NULL. In that case, we still need to execute the rollback
    and the end of the function.
   */
  if (table) {
    bool changed MY_ATTRIBUTE((unused));
    bool transactional_table;
    /*
      Try to end the bulk insert which might have been started before.
      We don't need to do this if we are in prelocked mode (since we
      don't use bulk insert in this case). Also we should not do this
      if tables are not locked yet (bulk insert is not started yet
      in this case).
    */
    if (bulk_insert_started) table->file->ha_end_bulk_insert();

    /*
      If at least one row has been inserted/modified and will stay in
      the table (the table doesn't have transactions) we must write to
      the binlog (and the error code will make the slave stop).

      For many errors (example: we got a duplicate key error while
      inserting into a MyISAM table), no row will be added to the table,
      so passing the error to the slave will not help since there will
      be an error code mismatch (the inserts will succeed on the slave
      with no error).

      If table creation failed, the number of rows modified will also be
      zero, so no check for that is made.
    */
    changed = (info.stats.records || updated_rows);
    transactional_table = table->file->has_transactions();
    if (thd->get_transaction()->cannot_safely_rollback(Transaction_ctx::STMT)) {
      if (mysql_bin_log.is_open()) {
        int errcode = query_error_code(thd, thd->killed == THD::NOT_KILLED);
        /* error of writing binary log is ignored */
        (void)thd->binlog_query(THD::ROW_QUERY_TYPE, thd->query().str,
                                thd->query().length, transactional_table, false,
                                false, errcode);
      }
    }
    DBUG_ASSERT(
        transactional_table || !changed ||
        thd->get_transaction()->cannot_safely_rollback(Transaction_ctx::STMT));
    table->file->ha_release_auto_increment();
  }
}

void Query_result_merge::send_error(THD *, uint errcode, const char *err) {
  /* First send error what ever it is ... */
  my_error(errcode, MYF(0), err);
}

void TABLE::mark_columns_needed_for_merge(THD *thd, uint merge_option) {
  mark_columns_per_binlog_row_image(thd);

  if (merge_option & ORA_MERGE_INTO_WITH_UPDATE) {
    mark_columns_needed_for_update(thd, false);
    if ((merge_option & ORA_MERGE_INTO_WITH_INSERT) && found_next_number_field)
      mark_auto_increment_column();
    return;
  }

  if (found_next_number_field) mark_auto_increment_column();
  /* Mark dependent generated columns as writable */
  if (vfield) mark_generated_columns(true);
  /* Mark columns needed for check constraints evaluation */
  if (table_check_constraint_list != nullptr)
    mark_check_constraint_columns(true);
  return;
}

bool Sql_cmd_merge::accept(THD *thd, Select_lex_visitor *visitor) {
  // Columns
  if (insert_column_list) {
    List_iterator<Item> it_field(*insert_column_list);
    while (Item *field = it_field++)
      if (walk_item(field, visitor)) return true;
  }

  if (insert_value_list) {
    List_iterator<Item> it_field(*insert_value_list);
    while (Item *field = it_field++)
      if (walk_item(field, visitor)) return true;
  }

  List_iterator<Item> it_value(*update_column_list), it_column(*update_value_list);
  Item *value, *column;
  while ((column = it_column++) && (value = it_value++))
    if (walk_item(column, visitor) || walk_item(value, visitor)) return true;

  if (thd->lex->select_lex->accept(visitor)) return true;

  return visitor->visit(thd->lex->select_lex);
}

void SELECT_LEX::print_merge(const THD *thd, String *str,
                             enum_query_type query_type) {

  /**
    USES: 'MERGE INTO target_table USING source_table on (conds) ...'
  */
  DBUG_ASSERT(parent_lex->sql_command == SQLCOM_MERGE_INTO);

  str->append(STRING_WITH_LEN("merge "));

  // Don't print QB name hints since it will be printed through print_select.
  print_hints(thd, str, enum_query_type(query_type | QT_IGNORE_QB_NAME));
  //print_insert_options(str);
  str->append(STRING_WITH_LEN("into "));

  const TABLE_LIST *target_table = parent_lex->query_tables;
  const TABLE_LIST *source_table = parent_lex->query_tables->next_local;

  target_table->print(thd, str, query_type);  // table identifier

  str->append(STRING_WITH_LEN(" using "));

  source_table->print(thd, str, query_type);

  str->append(' ');

  const bool is_optimized = join && join->is_optimized();

  // print ON (condition)
  print_on_condition(thd, str, query_type);

  Sql_cmd_merge *sql_cmd_merge = static_cast<Sql_cmd_merge *>(parent_lex->m_sql_cmd);
  Query_result_merge *merge_result = static_cast<Query_result_merge *>(m_query_result);
  DBUG_ASSERT(sql_cmd_merge);
  DBUG_ASSERT(merge_result);

  if (parent_lex->oracle_options & ORA_MERGE_INTO_WITH_UPDATE) {
    DBUG_ASSERT(sql_cmd_merge->update_column_list);
    DBUG_ASSERT(sql_cmd_merge->update_value_list);
    str->append(STRING_WITH_LEN("when matched then update set "));
    print_update_list(thd, str, query_type, *sql_cmd_merge->update_column_list,
                      *sql_cmd_merge->update_value_list);
    str->append(' ');

    Item *clause_where_cond = is_optimized ? merge_result->get_update_where()
                                           : sql_cmd_merge->opt_where_clause_update;
    if (clause_where_cond)
    {
      str->append(STRING_WITH_LEN("where("));
      clause_where_cond->print(thd, str, query_type);
      str->append(')');
      str->append(' ');
    } else if (!(merge_result->merge_option & ORA_MERGE_INTO_WITH_UPDATE))
      str->append(STRING_WITH_LEN("where false "));
  }

  if (parent_lex->oracle_options & ORA_MERGE_INTO_WITH_INSERT) {
    str->append(STRING_WITH_LEN("when not matched then insert "));
    if (sql_cmd_merge->insert_column_list &&
        sql_cmd_merge->insert_column_list->elements > 0) {
      print_item_list(thd, str, query_type, *sql_cmd_merge->insert_column_list);
      str->append(' ');
    }

    str->append(STRING_WITH_LEN("values "));
    if (sql_cmd_merge->insert_value_list) {
      print_item_list(thd, str, enum_query_type(query_type & ~QT_NO_DATA_EXPANSION),
                      *sql_cmd_merge->insert_value_list);
    } else {
      str->append(STRING_WITH_LEN("()"));
    }

    Item *clause_where_cond = is_optimized ? merge_result->get_insert_where()
                                           : sql_cmd_merge->opt_where_clause_insert;
    if (clause_where_cond)
    {
      str->append(STRING_WITH_LEN(" where("));
      clause_where_cond->print(thd, str, query_type);
      str->append(')');
    } else if (!(merge_result->merge_option & ORA_MERGE_INTO_WITH_INSERT))
      str->append(STRING_WITH_LEN(" where false"));
  }

  // If there is only update or insert clause in merge command,
  // the where cond will be push to the join
  if (m_where_cond)
    print_where_cond(thd, str, query_type);
}

void SELECT_LEX::print_on_condition(const THD *thd, String *str,
                                    enum_query_type query_type) {
  const TABLE_LIST *target_table = parent_lex->query_tables;
  const bool is_optimized = join && join->is_optimized();

  Item *const cond =
      is_optimized ? target_table->join_cond_optim() : target_table->join_cond();

  if (cond && cond != (Item *)1) {
    str->append(STRING_WITH_LEN("on ("));
    cond->print(thd, str, query_type);
    str->append(STRING_WITH_LEN(") "));
  }
}

void SELECT_LEX::print_item_list(const THD *thd, String *str,
                                 enum_query_type query_type,
                                 List<Item> fields) {
  List_iterator<Item> it_column(fields);
  str->append(STRING_WITH_LEN("("));
  bool first = true;
  while (Item *column = it_column++) {
    if (first)
      first = false;
    else
      str->append(',');

    column->print(thd, str, query_type);
  }
  str->append(STRING_WITH_LEN(")"));
}

bool SELECT_LEX::materialize_for_merge_into(THD *thd, TABLE_LIST * table) {
  if (thd->lex->sql_command != SQLCOM_MERGE_INTO)
    return false;

  if (table != thd->lex->query_tables->next_local)
    return false;

  TABLE_LIST *target_base_table = leaf_tables;
  if (!target_base_table->is_updatable()) {
    //my_error(ER_MERGE_INTO, MYF(0), "The target table of MERGE INTO is not updatable");
    my_error(ER_NON_UPDATABLE_TABLE, MYF(0), target_base_table->alias, "MERGE INTO");
    return true;
  }

  target_base_table = target_base_table->updatable_base_table();
  TABLE_LIST *duplicate = unique_table(target_base_table, leaf_tables/*thd->lex->query_tables*/, false);
  if (duplicate != NULL) {
    table->setup_materialized_derived(thd);
    table->algorithm = VIEW_ALGORITHM_TEMPTABLE;
  }
  return false;
}

/**
 * set current table and underlying table as target table of MERGE INTO
 * Only set the first table of select and the first select of derievd table,
 * because multiple table will meets error return in the future.
 */
void TABLE_LIST::set_merge_into_target_table() {
  is_merge_into_target = true;

  if (is_derived()) {
    SELECT_LEX *sl = derived->first_select();
    DBUG_ASSERT(sl);
    TABLE_LIST *tr = sl->get_table_list();
    if (tr) {
      tr->set_merge_into_target_table();
    }
  }
}

/**
 * Specially used for mark target table fields in join cond of MERGE INTO,
 * to avoid to be updated and cause a wrong result.
 * Generated column will affect the result no matter it is virtual or stored
 * when associated column is updated, so all associated columns are marked.
 *
 * Reference function: Item_field::add_field_to_set_processor
*/
bool Item_field::add_field_to_set_processor_for_merge_into(uchar *arg) {
  DBUG_TRACE;
  DBUG_PRINT("info", ("%s", field->field_name ? field->field_name : "noname"));
  TABLE *table = (TABLE *)arg;
  if (table_ref->table == table) {
    bitmap_set_bit(&table->tmp_set, field->field_index);
    if (field->gcol_info)
      bitmap_union(&table->tmp_set, &field->gcol_info->base_columns_map);
  }
  return false;
}

/**
 * Create a new context using table_list to specify tables to resolve items in.
 *
 * When resolve_in_single_table is true, first_name_resolution_table and
 * last_name_resolution_table is the same table_list.
*/
static Name_resolution_context *create_context(THD *thd, TABLE_LIST *table_list,
                                               bool resolve_in_single_table) {
  Name_resolution_context *new_context =
                        new (thd->mem_root) Name_resolution_context;
  if (nullptr == new_context) return nullptr;
  new_context->init();

  //new_context->first_name_resolution_table = new_context->table_list = table_list;
  //new_context->resolve_in_select_list = false;
  new_context->resolve_in_table_list_only(table_list);

  if (resolve_in_single_table)
    new_context->last_name_resolution_table = table_list;

  new_context->select_lex = table_list->select_lex;

  return new_context;
}