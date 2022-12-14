/* Copyright (c) 2006, 2018, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef SQL_UPDATE_INCLUDED
#define SQL_UPDATE_INCLUDED

#include <stddef.h>
#include <sys/types.h>

#include "my_base.h"
#include "my_sqlcommand.h"
#include "sql/query_result.h"  // Query_result_interceptor
#include "sql/sql_cmd_dml.h"   // Sql_cmd_dml
#include "sql/sql_list.h"

class COPY_INFO;
class Copy_field;
class Item;
class SELECT_LEX_UNIT;
class THD;
class Temp_table_param;
class Opt_trace_context;
struct TABLE;
struct TABLE_LIST;

bool records_are_comparable(const TABLE *table);
bool compare_records(const TABLE *table);
bool check_fields(THD *thd, List<Item> &items);
bool prepare_partial_update(Opt_trace_context *, List<Item> *, List<Item> *);

class Query_result_update final : public Query_result_interceptor {
  /// Number of tables being updated
  uint update_table_count;
  /// Pointer to list of updated tables, linked via 'next_local'
  TABLE_LIST *update_tables;
  /// Array of references to temporary tables used to store cached updates
  TABLE **tmp_tables;
  /// Array of parameter structs for creation of temporary tables
  Temp_table_param *tmp_table_param;
  /// The first table in the join operation
  TABLE *main_table;
  /**
    In a multi-table update, this is equal to the first table in the join
    operation (#main_table) if that table can be updated on the fly while
    scanning it. It is `nullptr` otherwise.

    @see safe_update_on_fly
  */
  TABLE *table_to_update;
  /// Number of rows found that matches join and WHERE conditions
  ha_rows found_rows;
  /// Number of rows actually updated, in all affected tables
  ha_rows updated_rows;
  /// List of pointers to fields to update, in order from statement
  List<Item> *fields;
  /// List of pointers to values to update with, in order from statement
  List<Item> *values;
  /// The fields list decomposed into separate lists per table
  List<Item> **fields_for_table;
  /// The values list decomposed into separate lists per table
  List<Item> **values_for_table;
  /**
   List of tables referenced in the CHECK OPTION condition of
   the updated view excluding the updated table.
  */
  List<TABLE> unupdated_check_opt_tables;
  /// ???
  Copy_field *copy_field;
  /// Length of the copy_field array.
  size_t max_fields{0};
  /// True if the full update operation is complete
  bool update_completed;
  /// True if all tables to be updated are transactional.
  bool trans_safe;
  /// True if the update operation has made a change in a transactional table
  bool transactional_tables;
  /**
     error handling (rollback and binlogging) can happen in send_eof()
     so that afterward send_error() needs to find out that.
  */
  bool error_handled;

  /**
     Array of update operations, arranged per _updated_ table. For each
     _updated_ table in the multiple table update statement, a COPY_INFO
     pointer is present at the table's position in this array.

     The array is allocated and populated during Query_result_update::prepare().
     The position that each table is assigned is also given here and is stored
     in the member TABLE::pos_in_table_list::shared. However, this is a publicly
     available field, so nothing can be trusted about its integrity.

     This member is NULL when the Query_result_update is created.

     @see Query_result_update::prepare
  */
  COPY_INFO **update_operations;
#ifdef HAVE_ZSQL_TEMPTABLE
  uint64_t m_rows_in_table; // rows in tmp table
  bool tmp_table_on_disk;
#endif /* HAVE_ZSQL_TEMPTABLE */

 public:
  Query_result_update(List<Item> *field_list, List<Item> *value_list)
      : Query_result_interceptor(),
        update_table_count(0),
        update_tables(NULL),
        tmp_tables(NULL),
        main_table(NULL),
        table_to_update(NULL),
        found_rows(0),
        updated_rows(0),
        fields(field_list),
        values(value_list),
        copy_field(NULL),
        update_completed(false),
        trans_safe(true),
        transactional_tables(false),
        error_handled(false),
        update_operations(NULL)
#ifdef HAVE_ZSQL_TEMPTABLE
        ,m_rows_in_table(0),
        tmp_table_on_disk(false)
#endif /* HAVE_ZSQL_TEMPTABLE */
        {}
  bool need_explain_interceptor() const override { return true; }
  bool prepare(THD *thd, List<Item> &list, SELECT_LEX_UNIT *u) override;
  bool optimize() override;
  bool start_execution(THD *) override {
    update_completed = false;
    return false;
  }
  bool send_data(THD *thd, List<Item> &items) override;
  void send_error(THD *thd, uint errcode, const char *err) override;
  bool do_updates(THD *thd);
  bool send_eof(THD *thd) override;
  void abort_result_set(THD *thd) override;
  void cleanup(THD *thd) override;
};

class Sql_cmd_update final : public Sql_cmd_dml {
 public:
  Sql_cmd_update(bool multitable_arg, List<Item> *update_values)
      : multitable(multitable_arg), update_value_list(update_values) {}

  enum_sql_command sql_command_code() const override {
    return multitable ? SQLCOM_UPDATE_MULTI : SQLCOM_UPDATE;
  }

  bool is_single_table_plan() const override { return !multitable; }

 protected:
  bool precheck(THD *thd) override;

  bool prepare_inner(THD *thd) override;

  bool execute_inner(THD *thd) override;

 private:
  bool update_single_table(THD *thd);

  bool multitable;

  bool accept(THD *thd, Select_lex_visitor *visitor) override;

 public:
  List<Item> *update_value_list;
};

#endif /* SQL_UPDATE_INCLUDED */
