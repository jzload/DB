#include "sql/oracle_compatibility/insert_all.h"
#include "sql/parse_tree_nodes.h"
#include "sql/sql_insert.h"
#include "sql/sql_base.h"  // setup_fields
#include "sql/debug_sync.h"
#include "sql/rpl_rli.h"
#include "sql/protocol.h"
#include "sql/rpl_slave.h"
#include "sql/derror.h"
#include "sql/sql_optimizer.h"
#include "sql/auth/auth_acls.h"
#include "sql/sql_resolver.h"
#include "sql/sql_cmd_dml.h"
#include "sql/sql_lex.h"
#include "sql/sql_select.h"

/**
  print insert for INSERT ALL syntax

  @returns false if it is INSERT ALL, true if not
*/
bool SELECT_LEX::print_insert_all(const THD *thd, String *str,
                              enum_query_type query_type) {
  if (SQLCOM_INSERT == thd->lex->sql_command) {
    Sql_cmd_insert_values *sql_cmd_values = down_cast<Sql_cmd_insert_values*>(thd->lex->m_sql_cmd);
    if (sql_cmd_values->is_insert_all_global) {
      print_insert_all_values(thd, str, query_type, sql_cmd_values);
      return false;
    }
  } else if (SQLCOM_INSERT_SELECT == thd->lex->sql_command) {
    Sql_cmd_insert_select *sql_cmd_select = down_cast<Sql_cmd_insert_select*>(thd->lex->m_sql_cmd);
    if (sql_cmd_select->is_insert_all_global) {
      print_insert_all_select(thd, str, query_type, sql_cmd_select);
      return false;
    }
  }

  return true;
}

void SELECT_LEX::print_insert_all_values(const THD *thd, String *str,
                              enum_query_type query_type,
                              Sql_cmd_insert_values *tmp_sql_cmd) {
  str->append(STRING_WITH_LEN("insert all"));

  TABLE_LIST *const table_list = thd->lex->query_tables;
  TABLE_LIST *cur_table;
  table_fields_pair *tb_field;
  table_values_pair *tb_value;
  List_iterator_fast<table_fields_pair> it_field(*tmp_sql_cmd->insert_all_into_field_list);
  List_iterator_fast<table_values_pair> it_value(*tmp_sql_cmd->insert_all_into_many_values);

  for (cur_table = table_list;
       cur_table && ((tb_field = it_field++)) && ((tb_value = it_value++));
       cur_table = cur_table->next_global) {
    tmp_sql_cmd->insert_many_values  = tb_value->row_values_list->get_many_values();
    tmp_sql_cmd->insert_field_list   = tb_field->column_list->value;
    parent_lex->m_sql_cmd            = tmp_sql_cmd;

    str->append(STRING_WITH_LEN(" into "));
    cur_table->print(thd, str, query_type);  // table identifier

    print_insert_fields(thd, str, query_type); // fields
    str->append(' ');

    print_insert_values(thd, str, query_type); // values
  }

  str->append(STRING_WITH_LEN(" select * from dual"));
}

void SELECT_LEX::print_insert_all_select(const THD *thd, String *str,
                              enum_query_type query_type,
                              Sql_cmd_insert_select *tmp_sql_cmd) {
  str->append(STRING_WITH_LEN("insert all"));

  TABLE_LIST *const table_list = thd->lex->query_tables;
  TABLE_LIST *cur_table;
  table_fields_pair *tb_field;
  List_iterator_fast<table_fields_pair> it_field(*tmp_sql_cmd->insert_all_into_field_list);

  for (cur_table = table_list; cur_table && ((tb_field = it_field++));
       cur_table = cur_table->next_global) {
    tmp_sql_cmd->insert_field_list   = tb_field->column_list->value;
    parent_lex->m_sql_cmd            = tmp_sql_cmd;

    str->append(STRING_WITH_LEN(" into "));
    cur_table->print(thd, str, query_type);  // table identifier

    print_insert_fields(thd, str, query_type); // fields
  }
  str->append(' ');

  print_select(thd, str, enum_query_type(query_type | QT_ONLY_QB_NAME));
}

/*
   multi table insert for insert all into selct statement

   @param thd   thread handler

   @returns false if success, true if error
*/
bool Sql_cmd_dml::execute_inner_for_insert_all(THD *thd) {
  DBUG_TRACE;

  SELECT_LEX_UNIT *unit = lex->unit;

  Sql_cmd_insert_select *tmp_sql_cmd = down_cast<Sql_cmd_insert_select*>(thd->lex->m_sql_cmd);
  List_iterator_fast<table_fields_pair> it_field(*(tmp_sql_cmd->insert_all_into_field_list));

  TABLE_LIST *const table_list = lex->query_tables;
  TABLE_LIST *cur_table;
  table_fields_pair *tb_field;

  bool is_first_table = false;

  TABLE_LIST *first_select_table = tmp_sql_cmd->first_select_table;
  TABLE_LIST *tmp_table_list;
  for (cur_table = table_list; cur_table && ((tb_field = it_field++)); cur_table = cur_table->next_global)
  {
    tmp_table_list         = cur_table->next_global;
    cur_table->next_global = first_select_table;
    cur_table->next_local  = first_select_table;
    lex->insert_table_leaf = cur_table;
    lex->insert_table      = cur_table;

    /* first need update table_list, table and field in query_result */
    Query_result_insert *tmp_query_result =
        down_cast<Query_result_insert*>(unit->query_result());
    tmp_query_result->update_parameters_for_insert_all(cur_table, cur_table->table,
        &(tb_field->column_list->value));

    /* copy from Sql_cmd_dml::executr_inner */
    if (!is_first_table) {
      if (unit->optimize(thd, /*materialize_destination=*/nullptr)) return true;

      // Calculate the current statement cost. It will be made available in
      // the Last_query_cost status variable.
      thd->m_current_query_cost = accumulate_statement_cost(lex);

      // Perform secondary engine optimizations, if needed.
      if (optimize_secondary_engine(thd)) return true;
    }

    if (lex->is_explain()) {
      cur_table->next_global = tmp_table_list;
      if (explain_query(thd, thd, unit)) {
        return true; /* purecov: inspected */
      } else {
        return false;
      }
    } else {
      if (unit->execute(thd)) return true;
    }

    if (!it_field.is_last()) {
      thd->get_stmt_da()->reset_ok_status_for_insert_all();
      tmp_query_result->reset_bulk_insert_started_for_insert_all();
      unit->reset_executed();
    }

    is_first_table = true;

    cur_table->next_global = tmp_table_list;
  }

  return false;
}


/**
  Contextualize fields and values for insert all statements,
  include add table to lex->query_tables

  @param thd      Thread handler
  @param tr

  @return false if success, true if error
*/
bool PT_insert::Contextualize_for_insert_all(THD *thd, Parse_context &pc)
{
  pc.select->parsing_place = CTX_INSERT_VALUES;

  Yacc_state *yyps = &pc.thd->m_parser_state->m_yacc;

  List_iterator<table_fields_pair> it_field(*table_columns_list);
  List_iterator<table_values_pair> it_value(*table_values_list);
  table_fields_pair *tb_field;
  table_values_pair *tb_value;
  TABLE_LIST *tmp;

  if (!has_select()) {
    while ((tb_field = it_field++) && (tb_value = it_value++))
    {
      /*need add all tables to table_list*/
      tmp = pc.select->add_table_to_list(thd, tb_field->table, NULL, TL_OPTION_UPDATING,
              TL_IGNORE, yyps->m_mdl_type, NULL, NULL);
      if (!tmp) return true;

      if (NULL == tb_field->table->db.str) {
        tb_field->table->db.str = tmp->db;
      }

      if (NULL == tb_value->table->db.str) {
        tb_value->table->db.str = tmp->db;
      }

      if (tb_field->column_list->contextualize(&pc)) return true;

      if (tb_value->row_values_list->contextualize(&pc)) return true;
    }

    // Ensure we're resetting parsing context of the right select
    DBUG_ASSERT(pc.select->parsing_place == CTX_INSERT_VALUES);
    pc.select->parsing_place = CTX_NONE;
  } else {
    while ((tb_field = it_field++))
    {
      /*need add all tables to table_list*/
      tmp = pc.select->add_table_to_list(thd, tb_field->table, NULL, TL_OPTION_UPDATING,
              TL_IGNORE, yyps->m_mdl_type, NULL, NULL);
      if (!tmp) return true;

      if (NULL == tb_field->table->db.str) {
        tb_field->table->db.str = tmp->db;
      }

      if (tb_field->column_list->contextualize(&pc)) return true;
    }
  }

  return false;
}


/**
  make sql cmd for insert all statement

  @param thd      Thread handler

  @return false if success, true if error
*/
Sql_cmd *PT_insert::make_cmd_for_insert_all(THD *thd) {

  LEX *const lex = thd->lex;

  Parse_context pc(thd, lex->current_select());

  lex->sql_command = has_select() ? SQLCOM_INSERT_SELECT : SQLCOM_INSERT;
  lex->duplicates  = DUP_ERROR;
  lex->set_ignore(ignore);

  /*add all tables to table_list, and contextualize column_list of all tables */
  if (Contextualize_for_insert_all(thd, pc)) return NULL;

  pc.select->set_lock_for_tables(lock_option);

  DBUG_ASSERT(lex->current_select() == lex->select_lex);

  if (has_select()) {
    if (insert_query_expression->is_insert_all_select_from_dual()) {
      my_error(ER_INSERT_ALL_ERROR, MYF(0), "You cannot use INSERT ALL SELECT FROM DUAL without VALUES");
      return NULL;
    }

    SQL_I_List<TABLE_LIST> save_list;
    SELECT_LEX *const save_select = pc.select;
    save_select->table_list.save_and_clear(&save_list);
    if (insert_query_expression->contextualize(&pc)) return NULL;

    save_select->table_list.push_front(&save_list);
    lex->bulk_insert_row_cnt = 0;
  }

  Sql_cmd_insert_base *sql_cmd;
  if (has_select()) {
    sql_cmd = new (thd->mem_root) Sql_cmd_insert_select(is_replace, lex->duplicates);
  } else {
    sql_cmd = new (thd->mem_root) Sql_cmd_insert_values(is_replace, lex->duplicates);
  }
  if (sql_cmd == NULL) return NULL;

  sql_cmd->insert_all_into_many_values = table_values_list;
  sql_cmd->insert_all_into_field_list  = table_columns_list;

  sql_cmd->is_insert_all_global = true;
  sql_cmd->is_insert_all_local = true;

  return sql_cmd;
}


/**
  multi table insert for insert all into statement

  @param thd   thread handler

  @returns false if success, true if error
*/

bool Sql_cmd_insert_values::execute_inner_for_insert_all(THD* thd)
{
  DBUG_TRACE;

  TABLE_LIST *const table_list = lex->query_tables;

  List_iterator_fast<table_fields_pair> it_field(*insert_all_into_field_list);
  List_iterator_fast<table_values_pair> it_value(*insert_all_into_many_values);
  table_fields_pair *tb_field;
  table_values_pair *tb_value;
  TABLE_LIST *cur_table;
  for (cur_table = table_list;
       cur_table && ((tb_field = it_field++)) && ((tb_value = it_value++));
       cur_table = cur_table->next_global)
  {

    /*first need to modify member, because it is used in execute_inner*/
    is_insert_all_local = false;
    lex->insert_table                = cur_table;
    lex->insert_table_leaf           = cur_table;
    insert_many_values               = tb_value->row_values_list->get_many_values();
    insert_field_list                = tb_field->column_list->value;
    value_count                      = insert_many_values.head()->elements;

    if (NULL == cur_table->next_global) {
      is_insert_all_last_table = true;
    }

    if (thd->lex->is_explain()) {
      Modification_plan plan(
        thd, (thd->lex->sql_command == SQLCOM_INSERT) ? MT_INSERT : MT_REPLACE,
        cur_table->table, NULL, false, 0);
      DEBUG_SYNC(thd, "planned_single_insert");

      bool err = explain_single_table_modification(thd, thd, &plan, thd->lex->select_lex);
      return err;
    }

    if (execute_inner(thd)) {
      is_insert_all_local = true;
      return true;
    }
  }

  is_insert_all_local = true;
  return false;
}

/**
  multi-table prepare for insert all

  @param thd          Thread handler

  @return false if success, true if error
*/
bool Sql_cmd_insert_base::prepare_inner_for_insert_all(THD *thd)
{
  DBUG_TRACE;

  Prepare_error_tracker tracker(thd);

  TABLE_LIST *const table_list = lex->query_tables;
  SQL_I_List<TABLE_LIST> tmp_save_list = lex->select_lex->table_list;
  TABLE_LIST *cur_table;
  TABLE_LIST *tmp_table;

  List_iterator<table_fields_pair> its_fields(*insert_all_into_field_list);
  List_iterator<table_values_pair> its_values(*insert_all_into_many_values);
  table_fields_pair *table_field;
  table_values_pair *table_value;

  if (NULL != insert_all_into_many_values)
  {
    for (cur_table = table_list;
         cur_table && ((table_field = its_fields++)) && ((table_value = its_values++));
         cur_table = cur_table->next_global)
    {
      if (cur_table->is_view()) {
        my_error(ER_INSERT_ALL_ERROR, MYF(0), "Target table cannot be insert, because it is a VIEW.");
        return true;
      }

      /* first modify member,because it is used in prepare_inner */
      is_insert_all_local    = false;
      cur_table->next_local  = NULL;
      cur_table->next_name_resolution_table = NULL;
      lex->select_lex->context.last_name_resolution_table = cur_table;
      lex->query_tables      = cur_table;
      lex->insert_table_leaf = NULL;
      insert_many_values     = table_value->row_values_list->get_many_values();
      insert_field_list      = table_field->column_list->value;
      tmp_table              = cur_table->next_global;
      cur_table->next_global = NULL;

      if (prepare_inner(thd)) {
        lex->query_tables      = table_list;
        is_insert_all_local    = true;
        cur_table->next_global = tmp_table;
        return true;
      }

      cur_table->next_global = tmp_table;
    }
  } else {
    Sql_cmd_insert_select *tmp_sql_cmd = down_cast<Sql_cmd_insert_select*>(thd->lex->m_sql_cmd);

    /* skip the table inserted into, find the first select table */
    List_iterator<table_fields_pair> it(*insert_all_into_field_list);
    for (cur_table = table_list; cur_table; cur_table = cur_table->next_local)
    {
      if ((table_field = it++)) {
        continue;
      } else {
        tmp_sql_cmd->first_select_table = cur_table;
        break;
      }
    }

    for (cur_table = table_list;
         cur_table && ((table_field = its_fields++));
         cur_table = cur_table->next_global)
    {
      tmp_table = cur_table->next_global;
      cur_table->next_global = tmp_sql_cmd->first_select_table;

      if (cur_table->is_view()) {
        my_error(ER_INSERT_ALL_ERROR, MYF(0), "Target table cannot be insert, because it is a VIEW.");
        return true;
      }

      /* first modify member,because it is used in prepare_inner */
      is_insert_all_local    = false;
      cur_table->next_local  = tmp_sql_cmd->first_select_table;
      cur_table->next_name_resolution_table = NULL;
      lex->query_tables      = cur_table;
      lex->insert_table_leaf = NULL;
      lex->insert_table      = cur_table;
      insert_field_list      = table_field->column_list->value;

      if (prepare_inner(thd)) {
        lex->query_tables      = table_list;
        is_insert_all_local    = true;
        cur_table->next_global = tmp_table;
        return true;
      }

      cur_table->next_global = tmp_table;
    }
  }

  lex->query_tables      = table_list;
  lex->select_lex->table_list = tmp_save_list;
  is_insert_all_local    = true;
  return false;
}


/**
  statistics insert all affected rows

*/
void Sql_cmd_insert_values::statistic_insert_all_affected_rows(THD *thd,
                                                               ha_rows affected_rows, ulonglong id)
{
  insert_all_affected_rows = insert_all_affected_rows + affected_rows;
  if (is_insert_all_last_table) {
    my_ok(thd, insert_all_affected_rows, id);
  }
}
