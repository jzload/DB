/* Copyright (c) 2016, 2017, Oracle and/or its affiliates. All rights reserved.
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

/* Execute CALL statement */

#include "sql/sql_call.h"

#include <limits.h>
#include <stddef.h>
#include <sys/types.h>
#include <algorithm>
#include <atomic>

#include "lex_string.h"
#include "my_base.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_sys.h"
#include "mysql/plugin_audit.h"
#include "mysql_com.h"
#include "mysqld_error.h"
#include "sql/auth/auth_acls.h"
#include "sql/auth/auth_common.h"  // check_routine_access, check_table_access
#include "sql/item.h"              // class Item
#include "sql/protocol.h"
#include "sql/sp.h"  // sp_find_routine
#include "sql/sp_head.h"
#include "sql/sp_pcontext.h"  // class sp_variable
#include "sql/sql_audit.h"    // AUDIT_EVENT
#include "sql/sql_class.h"    // class THD
#include "sql/sql_lex.h"
#include "sql/sql_list.h"
#include "sql/system_variables.h"
#include "template_utils.h"
#include "sql/transaction.h"  // trans_commit_stmt

using std::max;


bool Sql_cmd_call::precheck(THD *thd) {
  // Check execute privilege on stored procedure
  if (check_routine_access(thd, EXECUTE_ACL, proc_name->m_db.str,
                           proc_name->m_name.str, true, false))
    return true;

  // Check SELECT privileges for any subqueries
  if (check_table_access(thd, SELECT_ACL, lex->query_tables, false, UINT_MAX,
                         false))
    return true;
  return false;
}

bool Sql_cmd_call::prepare_inner(THD *thd) {
  // All required SPs should be in cache so no need to look into DB.

  sp_head *sp = sp_find_routine(thd, enum_sp_type::PROCEDURE, proc_name,
                                &thd->sp_proc_cache, true);
  if (sp == NULL) {
    my_error(ER_SP_DOES_NOT_EXIST, MYF(0), "PROCEDURE", proc_name->m_qname.str);
    return true;
  }

  sp_pcontext *root_parsing_context = sp->get_root_parsing_context();

#ifndef HAVE_ZSQL_ORACLE_COMPATIBILITY
  uint arg_count = proc_args != NULL ? proc_args->elements : 0;

  if (root_parsing_context->context_var_count() != arg_count) {
    my_error(ER_SP_WRONG_NO_OF_ARGS, MYF(0), "PROCEDURE",
             proc_name->m_qname.str, root_parsing_context->context_var_count(),
             arg_count);
    return true;
  }
#else
  if (sp->check_params(thd, proc_args)) {
    sp->reset_notation_resource(thd);
    return true;
  }
#endif /* HAVE_ZSQL_ORACLE_COMPATIBILITY */

  if (proc_args == NULL) return false;

  List_iterator<Item> it(*proc_args);
  Item *item;
  int arg_no = 0;
  while ((item = it++)) {
    if (item->type() == Item::TRIGGER_FIELD_ITEM) {
      Item_trigger_field *itf = down_cast<Item_trigger_field *>(item);
      sp_variable *spvar = root_parsing_context->find_variable(arg_no);
      if (spvar->mode != sp_variable::MODE_IN)
        itf->set_required_privilege(spvar->mode == sp_variable::MODE_INOUT);
    }
    if ((!item->fixed && item->fix_fields(thd, it.ref())) ||
        item->check_cols(1)) {
#ifdef HAVE_ZSQL_ORACLE_COMPATIBILITY
      sp->reset_notation_resource(thd);
#endif
      return true; /* purecov: inspected */
    }
    arg_no++;
  }

  return false;
}

bool Sql_cmd_call::execute_inner(THD *thd) {
  // All required SPs should be in cache so no need to look into DB.

  sp_head *sp = sp_setup_routine(thd, enum_sp_type::PROCEDURE, proc_name,
                                 &thd->sp_proc_cache);
  if (sp == NULL) {
    my_error(ER_SP_DOES_NOT_EXIST, MYF(0), "PROCEDURE", proc_name->m_qname.str);
    return true;
  }

  // bits to be cleared in thd->server_status
  uint bits_to_be_cleared = 0;
  /*
    Check that the stored procedure doesn't contain Dynamic SQL and doesn't
    return result sets: such stored procedures can't be called from
    a function or trigger.
  */
  if (thd->in_sub_stmt) {
    const char *where =
        (thd->in_sub_stmt & SUB_STMT_TRIGGER ? "trigger" : "function");
    if (sp->is_not_allowed_in_function(where)) return true;
  }

  if (mysql_audit_notify(thd, AUDIT_EVENT(MYSQL_AUDIT_STORED_PROGRAM_EXECUTE),
                         proc_name->m_db.str, proc_name->m_name.str, NULL))
    return true;

  if (sp->m_flags & sp_head::MULTI_RESULTS) {
    if (!thd->get_protocol()->has_client_capability(CLIENT_MULTI_RESULTS)) {
      // Client does not support multiple result sets
      my_error(ER_SP_BADSELECT, MYF(0), sp->m_qname.str);
      return true;
    }
    /*
      If SERVER_MORE_RESULTS_EXISTS is not set,
      then remember that it should be cleared
    */
    bits_to_be_cleared = (~thd->server_status & SERVER_MORE_RESULTS_EXISTS);
    thd->server_status |= SERVER_MORE_RESULTS_EXISTS;
  }

  ha_rows select_limit = thd->variables.select_limit;
  thd->variables.select_limit = HA_POS_ERROR;

  /*
    Never write CALL statements into binlog:
    - If the mode is non-prelocked, each statement will be logged separately.
    - If the mode is prelocked, the invoking statement will care about writing
      into binlog.
    So just execute the statement.
  */
  bool result = sp->execute_procedure(thd, proc_args);

  thd->variables.select_limit = select_limit;

  thd->server_status &= ~bits_to_be_cleared;

  if (result) {
    DBUG_ASSERT(thd->is_error() || thd->killed);
    return true;  // Substatement should already have sent error
  }

  my_ok(thd, max(thd->get_row_count_func(), 0LL));

  return false;
}


#ifdef HAVE_ZSQL_ORACLE_COMPATIBILITY
/* Added for 'declare begin end' block, which is defined as SQLCOM_DECL_BLOCK */
bool Sql_cmd_call_decl_block::precheck(THD *thd) {
  // Check SELECT privileges for any subqueries
  return check_table_access(thd, SELECT_ACL, lex->query_tables, false, UINT_MAX, false);
}


bool Sql_cmd_call_decl_block::prepare_inner(THD *thd) {
  // All required SPs should be in cache so no need to look into DB.
  if (thd == NULL || sp == NULL) {
    my_error(ER_SP_DOES_NOT_EXIST, MYF(0), "PROCEDURE", "UNDEFINED");
    return true;
  }

  return sp_adjust_info_for_execute(thd, sp);
}


bool Sql_cmd_call_decl_block::execute_inner(THD *thd) {
  DBUG_ASSERT(thd);

  // compile a new sp basing on current sp
  sp_head *new_sp = sp_setup_routine_from_oldsp(thd, enum_sp_type::PROCEDURE, sp);
  if (new_sp == NULL) {
    my_error(ER_SP_DOES_NOT_EXIST, MYF(0), "PROCEDURE", "UNDEFINED");
    return true;
  }

  // bits to be cleared in thd->server_status
  uint bits_to_be_cleared = 0;

  if (mysql_audit_notify(thd, AUDIT_EVENT(MYSQL_AUDIT_STORED_PROGRAM_EXECUTE),
                         new_sp->m_db.str, new_sp->m_name.str, NULL))
    return true;

  if (new_sp->m_flags & sp_head::MULTI_RESULTS) {
    if (!thd->get_protocol()->has_client_capability(CLIENT_MULTI_RESULTS)) {
      // Client does not support multiple result sets
      my_error(ER_SP_BADSELECT, MYF(0), new_sp->m_qname.str);
      return true;
    }
    /*
      If SERVER_MORE_RESULTS_EXISTS is not set,
      then remember that it should be cleared
    */
    bits_to_be_cleared = (~thd->server_status & SERVER_MORE_RESULTS_EXISTS);
    thd->server_status |= SERVER_MORE_RESULTS_EXISTS;
  }

  ha_rows select_limit = thd->variables.select_limit;
  thd->variables.select_limit = HA_POS_ERROR;

  /* 
    Oracle consider the declare block as a trnasaction, so switch off the AUTOCOMMIT 
    before execute procedure, and restore after execuation.
  */
  bool prev_autocommit = (thd->variables.option_bits & OPTION_AUTOCOMMIT);
  if(prev_autocommit) {
    thd->variables.option_bits &= ~OPTION_AUTOCOMMIT;
    thd->variables.option_bits |= OPTION_NOT_AUTOCOMMIT;
  }

  /* 
    make a savepoint, to which transaction will rollback when declare block executes failed.
    No need to release, savepoints will be released at commit or rollback of transaction;
  */
  thd->lex->ident = new_sp->m_qname;
  if (thd->in_multi_stmt_transaction_mode() && trans_savepoint(thd, thd->lex->ident))
    return true;

  /*
    Never write CALL statements into binlog:
    - If the mode is non-prelocked, each statement will be logged separately.
    - If the mode is prelocked, the invoking statement will care about writing
      into binlog.
    So just execute the statement.
  */
  DBUG_ASSERT(new_sp->get_root_parsing_context()->context_var_count() == 0);
  bool result = new_sp->execute_procedure(thd, NULL);

  /*
    After execuation, restore the AUTOCOMMIT flag to previous value if it used to be ON.
  */
  if(prev_autocommit) {
    thd->variables.option_bits |= OPTION_AUTOCOMMIT; 
    thd->variables.option_bits &= ~OPTION_NOT_AUTOCOMMIT;
  }

  thd->variables.select_limit = select_limit;

  thd->server_status &= ~bits_to_be_cleared;

  if (result) {
    DBUG_ASSERT(thd->is_error() || thd->killed);

    /* execute declare block failed, Oracle will rollback the declare block */
    if(thd->in_multi_stmt_transaction_mode()) {
      //rollback the declare block only while multi_stmt_transaction mode
      trans_rollback_to_savepoint(thd, thd->lex->ident);
    } else {
      //directly rollback all while not multi_stmt_transaction mode
      trans_rollback(thd);
    }
    return true;  // Substatement should already have sent error
  }

  /* 
    if autocommit if ON or in a transaction under successful execuation, declare block will be 
    commited by Oracle at the end.
  */
  if(!thd->in_multi_stmt_transaction_mode() && 
       (trans_commit_stmt(thd) || trans_commit(thd)) ) {
    return true;
  }

  my_ok(thd, max(thd->get_row_count_func(), 0LL));

  return false;
}
#endif /* HAVE_ZSQL_ORACLE_COMPATIBILITY */
