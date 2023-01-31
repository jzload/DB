/* Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd. 

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

#ifndef ZSQL_FEATURES_H
#define ZSQL_FEATURES_H
// zsql options
/**
  This part defines option flags for zsql_options in lex
*/
#define ZSQL_OPTION_USE_CURSOR_FETCH    (1ULL << 0)
#define ZSQL_OPTION_PUMP_EXPORT_JCSV    (1ULL << 1)
#define ZSQL_OPTION_END    (1ULL << 64) // ATTENTION ! This is END !!!

// oracle options
/**
 * This part defines option flags for oracle_options in lex
 */
// oracle_options: create or replace view_or_trigger_or_sp_or_event
#define ORA_CREATE_OR_REPLACE (1ULL << 0)
// oracle_options: merge option of merge into
// also used in merge_option of Sql_cmd_merge and Query_result_merge
#define ORA_MERGE_INTO_WITH_UPDATE  (1ULL << 1)
#define ORA_MERGE_INTO_WITH_INSERT  (1ULL << 2)
#define ORA_FLASHBACK_DROP_OR_TRUNCATE  (1ULL << 3)
#define ORA_OPTION_END    (1ULL << 64) // ATTENTION ! This is END !!!


#endif // ZSQL_FEATURES_H
