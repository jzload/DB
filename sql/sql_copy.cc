/* Copyright (c) 2000, 2015, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

/*
  Atomic copy of table;  COPY TABLE t1 to t2, tmp to t1 [,...]
*/


#ifdef HAVE_ZSQL_COPY_TABLE

#include "sql_copy.h"
#include "table.h"                   //
#include "sql_table.h"               // build_table_filename

#include "mysqld.h"
#include "sql/dd/cache/dictionary_client.h"  // dd::cache::Dictionary_client
#include "sql/dd/types/abstract_table.h"
#include "sql/field.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"

#include "lock.h"                    // MYSQL_OPEN_SKIP_TEMPORARY
#include "sql_base.h"                // tdc_remove_table, lock_table_names,
#include "sql_handler.h"             // mysql_ha_rm_tables
#include "log.h"
#include "sql/auth/auth_common.h"
#include "sql/auth/auth_acls.h"
#include "sql_reload.h"
#include "transaction.h"
#include "partition_info.h"          // partition_info
#include "my_dir.h"
#include "mysqld_error.h"

#define MAX_SHELL_CMD_LEN 1024
#define BUFFERSIZE        (1024*8)

#define UNUSED(x) (void)x

/* To be backwards compatible we also fold partition separator on windows. */
#ifdef _WIN32
const char* mysql_part_sep = "#p#";
const char* mysql_sub_sep = "#sp#";
#else
const char* mysql_part_sep = "#P#";
const char* mysql_sub_sep = "#SP#";
#endif /* _WIN32 */

/* Partition separator for *nix platforms */
const char* mysql_part_sep_nix = "#P#";
const char* mysql_sub_sep_nix = "#SP#";

bool is_partion_table(TABLE_LIST *table_list);
bool does_part_table_include_subpartition(TABLE_LIST *table_list);
/*need recursive*/
bool does_table_include_subpartition(TABLE_LIST *table_list);
bool check_copy_table_legally(TABLE_LIST *src_table, TABLE_LIST *dest_table);
bool check_copy_part_table(TABLE_LIST *src_table, TABLE_LIST *dest_table);
bool check_copy_not_part_table(TABLE_LIST *src_table, TABLE_LIST *dest_table);
bool is_same_partition(TABLE_LIST *src_table, TABLE_LIST *dest_table);
bool copy_part_table_files(TABLE_LIST *src_table, TABLE_LIST *dst_table);
bool copy_not_part_table_files(TABLE_LIST *src_table, TABLE_LIST *dest_table);
void my_partition_name_casedn_str(char *s);

bool delete_part_table_files(TABLE_LIST *table_list);

bool delete_not_part_table_files(TABLE_LIST *dest_table);

bool set_table_partiton_name(char *partition_name_start,
                             partition_element *part_elem,
                             size_t table_name_len,
                             size_t left_length);

bool prepare_for_copy_table(THD *thd, TABLE_LIST *src_table,
                            TABLE_LIST *dest_table);

bool clear_up_after_copy_table(THD *thd, TABLE_LIST *dest_table);

bool rollback_for_copy_table(THD *thd);

bool load_source_table_data_to_dest(THD *thd, TABLE_LIST *src_table,
                                    TABLE_LIST *dest_table);

bool commit_copy_table_cmd(THD *thd);

static bool check_table_common_properties(TABLE_LIST *src_table,
                                          TABLE_LIST *dest_table);

static bool check_table_engine(TABLE_LIST *src_table, TABLE_LIST *dest_table);

static bool check_table_fields(TABLE_LIST *src_table, TABLE_LIST *dest_table);

static bool check_table_indexes(TABLE_LIST *src_table, TABLE_LIST *dest_table);

static bool check_table_format(TABLE_LIST *src_table, TABLE_LIST *dest_table);

static bool check_table_encrypt(TABLE_LIST *src_table, TABLE_LIST *dest_table);

static bool check_table_index_count(TABLE_SHARE *ts1, TABLE_SHARE * ts2);
static bool check_table_index_type(TABLE *ts1, TABLE * ts2);
static bool table_include_fulltext(TABLE_SHARE *ts);
static bool table_include_fk_constraint(TABLE *table);

static bool set_copy_table_error(const char *msg)
{
  char  errbuf[MYSYS_STRERROR_SIZE];

  my_error(ER_COPY_TABLE_FAILED, MYF(0), msg);

  sql_print_error("copy table file error (errno: %d - %s)", errno,
                  my_strerror(errbuf, sizeof(errbuf),errno));

  return false;
}

/*****************************************************
check file status:
true: file doesn't exist
false: file exists
******************************************************/
static bool check_file(char *path)
{
  struct stat st;

  if (!my_stat(path, &st, MYF(0)))
    return true;

  return false;
}

static bool prepare_for_copy_file(char *src_file, char *dst_file)
{
  if (!src_file || !dst_file)
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "internal error");
    return true;
  }

  if (check_file(src_file))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "source table file doesn't exist");
    return true;
  }

  if (!check_file(dst_file))
  {
    sql_print_warning("copy table: there is still a ibd file[%s]" \
                      " after discarding tablespace",
                      dst_file);
    if (my_delete(dst_file,0))
    {
      my_error(ER_COPY_TABLE_FAILED, MYF(0),
               "dest table file already exists and delete failed");
      return true;
    }
  }

  return false;
}

static bool close_table_files(File src_fd, File dst_fd)
{
  if (src_fd > 0)
    my_close(src_fd, myf(0));

  if (dst_fd > 0)
    my_close(dst_fd, myf(0));

  return false;
}

static bool open_table_files(char *src_file, char *dst_file,
                             File  &src_fd, File &dst_fd)
{
  src_fd = my_open(src_file, O_RDONLY, myf(0));
  dst_fd = my_open(dst_file, O_CREAT|O_RDWR, myf(0));

  if (src_fd < 0 || dst_fd < 0)
  {
    set_copy_table_error("open table file failed");
    close_table_files(src_fd, dst_fd);
    return true;
  }

  return false;
}

static bool copy_file(char *src_file, char *dst_file)
{
  size_t  n_chars;
  File    src_fd, dst_fd;
  char    buffer[BUFFERSIZE];

  if (prepare_for_copy_file(src_file, dst_file))
    return true;

  if (open_table_files(src_file, dst_file, src_fd, dst_fd))
    return true;

  do
  {
    n_chars = my_read(src_fd, (uchar*)buffer, BUFFERSIZE, myf(0));

    if (n_chars == MY_FILE_ERROR)
    {
      set_copy_table_error("read source table file failed");
      goto err;
    }

    if (my_write(dst_fd, (uchar*)buffer, n_chars,myf(0)) != n_chars)
    {
      set_copy_table_error("write dest table file failed");
      goto err;
    }

  } while (n_chars > 0);

  close_table_files(src_fd, dst_fd);

  return false;

err:
  close_table_files(src_fd, dst_fd);

  return true;
}

/** Translate and append partition name.
@param[out] to      String to write in filesystem charset
@param[in]  from    Name in system charset
@param[in]  sep     Separator
@param[in]  len     Max length of to buffer
@return     length of written string. */
size_t my_append_sep_and_name(char *to, const char *from, const char *sep,
                              size_t len)
{
  size_t    ret;
  size_t    sep_len = strlen(sep);

  /*
  ut_ad(len > sep_len + strlen(from));
  ut_ad(to != NULL);
  ut_ad(from != NULL);
  ut_ad(from[0] != '\0');
  */
  memcpy(to, sep, sep_len);

  ret = tablename_to_filename(from, to + sep_len, len - sep_len);

  /* Don't convert to lower case for nix style name. */
  if (strcmp(sep, mysql_part_sep_nix) != 0
      && strcmp(sep, mysql_sub_sep_nix) != 0)
  {
    my_partition_name_casedn_str(to);
  }

  return(ret + sep_len);
}

void my_partition_name_casedn_str(char *s)
{
#ifdef _WIN32
  my_casedn_str(system_charset_info, s);
#else
  UNUSED(s);
#endif

  return;
}


static bool prepare_for_src_table(THD *thd, TABLE_LIST *first_table)
{
  thd->lex->type|= REFRESH_TABLES;
  thd->lex->type|= REFRESH_FOR_EXPORT;
  for (; first_table; first_table= first_table->next_global)
  {
    first_table->mdl_request.set_type(MDL_SHARED_NO_WRITE);
    first_table->required_type= dd::enum_table_type::BASE_TABLE; /* Don't try to flush views. */
    first_table->open_type= OT_BASE_ONLY;      /* Ignore temporary tables. */
  }

  return false;
}

static bool prepare_for_dest_table(THD *thd, TABLE_LIST *first_table)
{
  UNUSED(thd);

  /*
    Adjust values of table-level and metadata which was set in parser
    for the case general ALTER TABLE.
  */
  first_table->mdl_request.set_type(MDL_EXCLUSIVE);
  first_table->set_lock({TL_WRITE, THR_DEFAULT});;
  /* Do not open views. */
  first_table->required_type= dd::enum_table_type::BASE_TABLE;

  return false;
}


static bool set_slow_and_binlog_info(THD *thd, TABLE_LIST *src_table,
                                     TABLE_LIST *dest_table)
{
  UNUSED(src_table);
  thd->enable_slow_log= opt_log_slow_admin_statements;

  /*
    Add current database to the list of accessed databases
    for this statement. Needed for MTS.
  */
  thd->add_to_binlog_accessed_dbs(dest_table->db);

  return false;
}


static bool check_access_for_copy_table(THD *thd, TABLE_LIST *src_table,
                                        TABLE_LIST *dest_table)
{
  if (check_global_access(thd, RELOAD_ACL))
    return true;

  // src and dest all need to flush ... for export
  if (thd->lex->type & REFRESH_FOR_EXPORT)
  {
    /* Check table-level privileges. */
    if (check_table_access(thd, LOCK_TABLES_ACL | SELECT_ACL, src_table,
                           false, UINT_MAX, false))
      return true;
  }

  if (check_access(thd, ALTER_ACL, dest_table->db,
                   &dest_table->grant.privilege,
                   &dest_table->grant.m_internal,
                   0, 0))
    return true;

  if (check_grant(thd, ALTER_ACL, dest_table, false, UINT_MAX, false))
    return true;


  /*
    Check if we attempt to alter mysql.slow_log or
    mysql.general_log table and return an error if
    it is the case.
    TODO: this design is obsolete and will be removed.
  */
  enum_log_table_type table_kind=
    query_logger.check_if_log_table(dest_table, false);

  if (table_kind != QUERY_LOG_NONE)
  {
    /* Disable alter of enabled query log tables */
    if (query_logger.is_log_table_enabled(table_kind))
    {
      my_error(ER_BAD_LOG_STATEMENT, MYF(0), "ALTER");
      return true;
    }
  }

  return false;
}

#if 0
static bool break_table_list(TABLE_LIST *src_table)
{
  src_table->next_local  = NULL;
  src_table->next_global = NULL;
  return true;
}
#endif 

static bool link_table_list(TABLE_LIST *src_table, TABLE_LIST *dst_table)
{
  src_table->next_local  = dst_table;
  src_table->next_global = dst_table;
  return true;
}
#if 0
static bool flush_source_table_for_export(THD *thd, TABLE_LIST *first_table)
{
  if (flush_tables_for_export(thd, first_table))
    return true;

  return false;
}
#endif

/* Refer to:
 * Sql_cmd_discard_import_tablespace::mysql_discard_or_import_tablespace */
static bool discard_dest_table_space(THD *thd, TABLE_LIST *dest_table)
{
  int error;
  TABLE *table = dest_table->table;

  if (table->part_info)
  {
#if 0
    /*
      If not ALL is mentioned and there is at least one specified
      [sub]partition name, use the specified [sub]partitions only.
    */
    if (thd->lex->alter_info->partition_names.elements > 0 &&
        !(thd->lex->alter_info->flags & Alter_info::ALTER_ALL_PARTITION))
    {
      dest_table->partition_names= &thd->lex->alter_info->partition_names;
      /* Set all [named] partitions as used. */
      if (dest_table->table->part_info->set_partition_bitmaps(dest_table))
        return true;
    }
#endif

    /*
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "doesn't support partition table");
    return true;
    */
  }

  bool is_non_tmp_table = (table->s->tmp_table == NO_TMP_TABLE);
  handlerton *hton = table->s->db_type();

  //dd::cache::Dictionary_client::Auto_releaser releaser(thd->dd_client());
  dd::Table *table_def = nullptr;

  if (is_non_tmp_table)
  {
    if (thd->dd_client()->acquire_for_modification(dest_table->db,
                                 dest_table->table_name, &table_def))
    {
      return true;
    }

    /* Table was successfully opened above. */
    DBUG_ASSERT(table_def != nullptr);
  }
  else
  {
    table_def = table->s->tmp_table_def;
  }

  error = table->file->ha_discard_or_import_tablespace(true, table_def);

  if (error)
  {
    table->file->print_error(error, MYF(0));
    return true;
  }

  /*
    Storage engine supporting atomic DDL can fully rollback discard/
    import if any problem occurs. This will happen during statement
    rollback.

    In case of success we need to save dd::Table object which might
    have been updated by SE. If this step fail,
    then statement rollback will also restore status quo ante.
  */
  if (is_non_tmp_table && (hton->flags & HTON_SUPPORTS_ATOMIC_DDL) &&
      thd->dd_client()->update(table_def))
  {
    return true;
  }

  return false;
}

/* deal with the system() return value and errno */
static bool copy_source_table_files(TABLE_LIST *src_table,
                                    TABLE_LIST *dest_table)
{
  if (is_partion_table(src_table))
  {
    return copy_part_table_files(src_table, dest_table);
  }
  return copy_not_part_table_files(src_table, dest_table);
}


bool copy_not_part_table_files(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  char src_ibd[MAX_SHELL_CMD_LEN+1] = {0};
  char src_cfg[MAX_SHELL_CMD_LEN+1] = {0};
  char dst_ibd[MAX_SHELL_CMD_LEN+1] = {0};
  char dst_cfg[MAX_SHELL_CMD_LEN+1] = {0};

  snprintf(src_ibd, MAX_SHELL_CMD_LEN, "%s%s/%s.ibd",
           mysql_real_data_home, src_table->db, src_table->table_name);
  snprintf(src_cfg, MAX_SHELL_CMD_LEN, "%s%s/%s.cfg",
           mysql_real_data_home, src_table->db, src_table->table_name);

  snprintf(dst_ibd, MAX_SHELL_CMD_LEN, "%s%s/%s.ibd",
           mysql_real_data_home, dest_table->db, dest_table->table_name);
  snprintf(dst_cfg, MAX_SHELL_CMD_LEN, "%s%s/%s.cfg",
           mysql_real_data_home, dest_table->db, dest_table->table_name);

  if (copy_file(src_ibd, dst_ibd ) || copy_file(src_cfg, dst_cfg))
    return true;

  return false;
}


bool copy_part_table_files(TABLE_LIST *src_table, TABLE_LIST *dst_table)
{
  size_t    src_table_name_len;
  size_t    dst_table_name_len;

  char      src_ibd[MAX_SHELL_CMD_LEN+1] = {0};
  char      src_cfg[MAX_SHELL_CMD_LEN+1] = {0};
  char      dst_ibd[MAX_SHELL_CMD_LEN+1] = {0};
  char      dst_cfg[MAX_SHELL_CMD_LEN+1] = {0};

  char      src_partition_name[FN_REFLEN]={0};
  char*     src_partition_name_start;
  char      dst_partition_name[FN_REFLEN]={0};
  char*     dst_partition_name_start;

  partition_element *src_part_elem;
  partition_element *dst_part_elem;

  src_table_name_len = snprintf(src_partition_name, FN_REFLEN,
                                "%s%s/%s",mysql_real_data_home,
                                src_table->db, src_table->table_name);
  src_partition_name_start = src_partition_name + src_table_name_len;

  dst_table_name_len = snprintf(dst_partition_name, FN_REFLEN,
                                "%s%s/%s", mysql_real_data_home,
                                dst_table->db, dst_table->table_name);
  dst_partition_name_start = dst_partition_name + dst_table_name_len;

  List_iterator_fast<partition_element> src_part_it(src_table->table->part_info->partitions);
  List_iterator_fast<partition_element> dst_part_it(dst_table->table->part_info->partitions);
  while ((src_part_elem = src_part_it++) && (dst_part_elem = dst_part_it++))
  {
    if (set_table_partiton_name(src_partition_name_start, src_part_elem,
                      src_table_name_len, FN_REFLEN - src_table_name_len) ||
        set_table_partiton_name(dst_partition_name_start, dst_part_elem,
                      dst_table_name_len, FN_REFLEN - dst_table_name_len))
    {
      return true;
    }

    snprintf(src_ibd, MAX_SHELL_CMD_LEN, "%s.ibd", src_partition_name);
    snprintf(src_cfg, MAX_SHELL_CMD_LEN, "%s.cfg", src_partition_name);
    snprintf(dst_ibd, MAX_SHELL_CMD_LEN, "%s.ibd", dst_partition_name);
    snprintf(dst_cfg, MAX_SHELL_CMD_LEN, "%s.cfg", dst_partition_name);

    if (copy_file(src_ibd, dst_ibd) || copy_file(src_cfg, dst_cfg))
    {
      return true;
    }
  }

  return false;
}


bool set_table_partiton_name(char *partition_name_start,
                             partition_element *part_elem,
                             size_t table_name_len,
                             size_t left_length)
{
  size_t   len;
  char *table_name = (char*)(partition_name_start - table_name_len);

  /* Append the partition name to the table name. */
  len = my_append_sep_and_name(partition_name_start,
                               part_elem->partition_name,
                               mysql_part_sep,
                               left_length);

  if ((table_name_len + len + sizeof("/")) >= FN_REFLEN)
  {
    my_error(ER_PATH_LENGTH, MYF(0), table_name);
    return true;
  }

  return false;
}


static bool import_dest_table_space(THD *thd, TABLE_LIST *first_table)
{
  int error;
  TABLE *table = first_table->table;

  bool is_non_tmp_table = (table->s->tmp_table == NO_TMP_TABLE);

  //dd::cache::Dictionary_client::Auto_releaser releaser(thd->dd_client());
  dd::Table *table_def = nullptr;

  if (is_non_tmp_table)
  {
    if (thd->dd_client()->acquire_for_modification(first_table->db,
                                   first_table->table_name, &table_def))
      return true;

    /* Table was successfully opened above. */
    DBUG_ASSERT(table_def != nullptr);
  }
  else
  {
    table_def = table->s->tmp_table_def;
  }

  error = table->file->ha_discard_or_import_tablespace(false, table_def);

  if (error)
  {
    table->file->print_error(error, MYF(0));
    return true;
  }

  handlerton *hton = table->s->db_type();

  /*
    Storage engine supporting atomic DDL can fully rollback discard/
    import if any problem occurs. This will happen during statement
    rollback.

    In case of success we need to save dd::Table object which might
    have been updated by SE. If this step fail,
    then statement rollback will also restore status quo ante.
  */
  if (is_non_tmp_table && (hton->flags & HTON_SUPPORTS_ATOMIC_DDL) &&
      thd->dd_client()->update(table_def))
  {
    return true;
  }

  return false;
}

static bool unlock_tables_for_copy_table(THD *thd)
{
  if (thd->variables.option_bits & OPTION_TABLE_LOCK)
  {
    thd->locked_tables_list.unlock_locked_tables(thd);
    thd->mdl_context.release_transactional_locks();
    thd->variables.option_bits&= ~(OPTION_TABLE_LOCK);
  }

  if (thd->global_read_lock.is_acquired())
    thd->global_read_lock.unlock_global_read_lock(thd);

  return false;
}

static bool delete_dest_cfg_file(TABLE_LIST *dest_table)
{
  if (is_partion_table(dest_table))
  {
    return delete_part_table_files(dest_table);
  }
  return delete_not_part_table_files(dest_table);
}


bool delete_not_part_table_files(TABLE_LIST *dest_table)
{
  char  errbuf[MYSYS_STRERROR_SIZE];
  char  cfg_file[MAX_SHELL_CMD_LEN+1] = {0};

  snprintf(cfg_file, MAX_SHELL_CMD_LEN, "%s%s/%s.cfg",
           mysql_real_data_home, dest_table->db, dest_table->table_name);
  if (-1 == my_delete(cfg_file, 0))
  {
    sql_print_warning("delete cfg file (%s) error after copy table (errno: %d - %s)",
                      cfg_file, errno, my_strerror(errbuf, sizeof(errbuf),errno));
  }

  return false;
}


bool delete_part_table_files(TABLE_LIST *table_list)
{
   char    errbuf[MYSYS_STRERROR_SIZE];
   char    cfg_file[MAX_SHELL_CMD_LEN+1] = {0};
   char    partition_name[FN_REFLEN]={0};
   char*   partition_name_start;
   size_t  table_name_len;

   table_name_len = snprintf(partition_name, FN_REFLEN, "%s%s/%s",
                             mysql_real_data_home,
                             table_list->db, table_list->table_name);
   partition_name_start = partition_name + table_name_len;

   List_iterator_fast<partition_element> part_it(table_list->table->part_info->partitions);
   partition_element* part_elem;
   while ((part_elem = part_it++))
   {
     if (set_table_partiton_name(partition_name_start, part_elem,
                                 table_name_len, FN_REFLEN - table_name_len))
       return true;

     snprintf(cfg_file, MAX_SHELL_CMD_LEN, "%s.cfg", partition_name);
     if (-1 == my_delete(cfg_file, 0))
     {
       sql_print_warning("delete cfg file (%s) error after copy table (errno: %d - %s)",
                         cfg_file,errno,my_strerror(errbuf, sizeof(errbuf),errno));
     }
   }

  return false;
}


bool is_partion_table(TABLE_LIST *table_list)
{
  if (table_list->table->part_info)
    return true;

  return false;
}


bool does_part_table_include_subpartition(TABLE_LIST *table_list)
{
  List_iterator_fast<partition_element> part_it(table_list->table->part_info->partitions);
  partition_element* part_elem;
  while ((part_elem = part_it++))
  {
    if (part_elem->subpartitions.elements > 0)
      return true;
  }

  return false;
}


/*need recursive*/
bool does_table_include_subpartition(TABLE_LIST *table_list)
{
  if (!is_partion_table(table_list))
    return false;

  return does_part_table_include_subpartition(table_list);
}


bool check_copy_table_legally(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  if (check_table_common_properties(src_table, dest_table))
    return true;

  if (is_partion_table(src_table))
  {
    return check_copy_part_table(src_table, dest_table);
  }

  return check_copy_not_part_table(src_table, dest_table);
}


static bool check_table_common_properties(TABLE_LIST *src_table,
                                          TABLE_LIST *dest_table)
{
  if (check_table_engine(src_table, dest_table))
    return true;

  if (check_table_fields(src_table, dest_table))
    return true;

  if (check_table_indexes(src_table, dest_table))
    return true;

  if (check_table_format(src_table, dest_table))
    return true;

  if (check_table_encrypt(src_table, dest_table))
    return true;

  return false;
}


static bool check_table_fields(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  Field *field1, *field2 = NULL;
  TABLE_SHARE *ts1 = src_table->table->s;
  TABLE_SHARE *ts2 = dest_table->table->s;

  if (ts1->fields != ts2->fields)
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "table field count is different");
    return true;
  }

  for (uint i=0; i < ts1->fields; i++)
  {
    field1 = ts1->field[i];
    field2 = ts2->field[i];

    if (!field1 || !field2)
    {
      my_error(ER_COPY_TABLE_FAILED, MYF(0), "internal error");
      return true;
    }

    if ((!field1->eq_def(field2)) ||
        (field1->real_maybe_null() != field2->real_maybe_null())) 
    {
        my_error(ER_COPY_TABLE_FAILED, MYF(0), "table field def is different");
        return true;
    }
  }

  return false;
}


static bool check_table_engine(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  TABLE_SHARE *ts1 = src_table->table->s;
  TABLE_SHARE *ts2 = dest_table->table->s;
  if (NULL == ts1->db_type() || NULL == ts2->db_type()) // fix for kw
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "internal error");
    return true;
  }

  if (ts1->db_type()->db_type != ts2->db_type()->db_type)
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "table engines are different");
    return true;
  }

  return false;
}


static bool check_table_indexes(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  if (check_table_index_count(src_table->table->s,
                              dest_table->table->s))
    return true;

  if (check_table_index_type(src_table->table, dest_table->table))
    return true;

  return false;
}

static uint calculate_unique_keys(TABLE_SHARE *ts)
{
  uint keys = ts->keys;
  uint i = 0;
  uint unique_keys = 0;

  for (KEY *keyinfo = ts->key_info; i < keys; ++i, ++keyinfo)
  {
    if (keyinfo->flags & HA_NOSAME)
      unique_keys++;
  }

  return unique_keys;
}

static bool check_table_index_count(TABLE_SHARE *ts1, TABLE_SHARE * ts2)
{
  if (ts1->keys != ts2->keys || ts1->key_parts != ts2->key_parts)
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "table keys are different");
    return true;
  }

  if (calculate_unique_keys(ts1) != calculate_unique_keys(ts2))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "unique indexes are different");
    return true;
  }

  if (ts1->primary_key != ts2->primary_key)
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "primary key is different");
    return true;
  }

  return false;
}


static bool check_table_index_type(TABLE *t1, TABLE *t2)
{
  if (table_include_fulltext(t1->s) || table_include_fulltext(t2->s))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "table has fulltext index");
    return true;
  }

  if (table_include_fk_constraint(t1) || table_include_fk_constraint(t2))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "table has foreign key constraint");
    return true;
  }

  return false;
}


static bool table_include_fulltext(TABLE_SHARE *ts)
{
  KEY *keys = ts->key_info;

  if (!keys)
    return false;

  for (uint i=0; i < ts->keys; i++)
  {
    if (keys[i].algorithm == HA_KEY_ALG_FULLTEXT)
      return true;
  }

  return false;
}


/* the func only for innodb:
1.table contains fk
2.table was referenced_by_foreign_key
*/
static bool table_include_fk_constraint(TABLE *table)
{
  if (table->file->has_foreign_key() ||
      table->file->referenced_by_foreign_key())
  {
    return true;
  }

  return false;

  /*
  THD *thd = current_thd;
  List<FOREIGN_KEY_INFO> f_key_list;
  table->file->get_foreign_key_list(thd, &f_key_list);

  if (f_key_list.elements > 0)
    return true;

  return false;
  */
}


static bool check_table_format(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  TABLE_SHARE *ts1 = src_table->table->s;
  TABLE_SHARE *ts2 = dest_table->table->s;

  if (ts1->row_type != ts2->row_type)
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "table row_format is different");
    return true;
  }

  return false;
}


static bool check_table_encrypt(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  TABLE_SHARE *ts1 = src_table->table->s;
  TABLE_SHARE *ts2 = dest_table->table->s;

  if (ts1->encrypt_type.str && ts1->encrypt_type.str[0] &&
      0 == strncasecmp(ts1->encrypt_type.str, "Y", ts1->encrypt_type.length))
    goto err;

  if (ts2->encrypt_type.str && ts2->encrypt_type.str[0] &&
      0 == strncasecmp(ts2->encrypt_type.str, "Y", ts2->encrypt_type.length))
    goto err;

  return false;

err:
  my_error(ER_COPY_TABLE_FAILED, MYF(0), "table encrypiton is not supported");
  return true;
}


bool check_copy_part_table(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  if (!is_partion_table(src_table) || !is_partion_table(dest_table))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "one table is partition, another is not");
    return true;
  }

  if (does_table_include_subpartition(src_table) ||
      does_table_include_subpartition(dest_table))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "partition table inlcudes subpartition");
    return true;
  }

  if (!is_same_partition(src_table, dest_table))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0),
             "partitons between two tables are not consistency");
    return true;
  }

  return false;
}


bool is_same_partition(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  partition_info *src_info  = src_table->table->part_info;
  partition_info *dest_info = dest_table->table->part_info;

  return src_info->is_same_partitioning(dest_info);
}


bool check_copy_not_part_table(TABLE_LIST *src_table, TABLE_LIST *dest_table)
{
  if (is_partion_table(src_table) || is_partion_table(dest_table))
  {
    my_error(ER_COPY_TABLE_FAILED, MYF(0), "one table is partition, another is not");
    return true;
  }

  return false;
}

/*
orignal process:
src:
    src_table->mdl_request.set_type(MDL_SHARED_NO_WRITE);
    src_table->required_type= FRMTYPE_TABLE;  // Don't try to flush views.
    src_table->open_type= OT_BASE_ONLY;       // Ignore temporary tables.
    src_table->lock_type = TL_READ_NO_INSERT;

    ACCESS:RELOAD_ACL && (LOCK_TABLES_ACL | SELECT_ACL)

dest：
  dest_table->mdl_request.set_type(MDL_EXCLUSIVE);
  dest_table->lock_type= TL_WRITE;
  dest_table->required_type= FRMTYPE_TABLE;
  dest_table->open_type = OT_TEMPORARY_OR_BASE;

  ACCESS:ALTER_ACL

new process:
  src and dest:
  src_table->mdl_request.set_type(MDL_SHARED_NO_WRITE);
  src_table->required_type= FRMTYPE_TABLE;  // Don't try to flush views.
  src_table->open_type= OT_BASE_ONLY;       // Ignore temporary tables.
  src_table->lock_type = TL_READ_NO_INSERT;

  ACCESS:RELOAD_ACL && (LOCK_TABLES_ACL | SELECT_ACL)  && ALTER_ACL

*/

bool prepare_for_copy_table(THD *thd, TABLE_LIST *src_table,
                            TABLE_LIST *dest_table)
{
  if (prepare_for_src_table(thd, src_table))
    return true;

  if (prepare_for_dest_table(thd, dest_table))  // need link table list
    return true;

  if (check_access_for_copy_table(thd, src_table, dest_table))
    return true;

  if (set_slow_and_binlog_info(thd, src_table, dest_table))
    return true;

  link_table_list(src_table,  dest_table);

  return false;
}


bool clear_up_after_copy_table(THD *thd, TABLE_LIST *dest_table)
{
  //UNUSED(thd);
  unlock_tables_for_copy_table(thd);

  delete_dest_cfg_file(dest_table);

  return false;
}


bool rollback_for_copy_table(THD *thd)
{
  trans_rollback_stmt(thd);
  trans_rollback_implicit(thd);
  unlock_tables_for_copy_table(thd);
  return false;
}

bool load_source_table_data_to_dest(THD *thd, TABLE_LIST *src_table,
                                    TABLE_LIST *dest_table)
{
  if (discard_dest_table_space(thd, dest_table))
    return true;

  if (copy_source_table_files(src_table, dest_table))
    return true;

  if (import_dest_table_space(thd, dest_table))
    return true;

  return false;
}


bool commit_copy_table_cmd(THD *thd)
{
  /* The ALTER TABLE is always in its own transaction */
  if(trans_commit_stmt(thd) || trans_commit_implicit(thd))
    return true;

  if(write_bin_log(thd, true, thd->query().str, thd->query().length))
    return true;

  return false;
}

static bool mdl_upgrade_shared_lock(THD *thd, TABLE *table)
{
  bool is_non_tmp_table = (table->s->tmp_table == NO_TMP_TABLE);

  /*
    Under LOCK TABLES we need to upgrade SNRW metadata lock to X lock
    before doing discard or import of tablespace.

    Skip this step for temporary tables as metadata locks are not
    applicable for them.
  */
  if (is_non_tmp_table &&
      (thd->locked_tables_mode == LTM_LOCK_TABLES ||
       thd->locked_tables_mode == LTM_PRELOCKED_UNDER_LOCK_TABLES) &&
      thd->mdl_context.upgrade_shared_lock(table->mdl_ticket, MDL_EXCLUSIVE,
                                           thd->variables.lock_wait_timeout))
  {
    return true;
  }

  return false;
}

static void mdl_downgrade_lock(THD *thd, TABLE *table)
{
  bool is_non_tmp_table = (table->s->tmp_table == NO_TMP_TABLE);

  if (is_non_tmp_table &&
      (thd->locked_tables_mode == LTM_LOCK_TABLES ||
       thd->locked_tables_mode == LTM_PRELOCKED_UNDER_LOCK_TABLES))
  {
    table->mdl_ticket->downgrade_lock(MDL_SHARED_NO_READ_WRITE);
  }
}

/*
  Every two entries in the table_list form a pair of original name and
  the new name.
  1. only support one table copy data to one table
*/
bool mysql_copy_table(THD *thd, TABLE_LIST *table_list)
{
  TABLE_LIST* src_table = table_list;
  TABLE_LIST* dest_table = table_list->next_local;
  handlerton* hton = nullptr;
  Alter_table_prelocking_strategy alter_prelocking_strategy;
  bool has_upgraded_shared_lock = false;

  dd::cache::Dictionary_client::Auto_releaser releaser(thd->dd_client());

  if (prepare_for_copy_table(thd, src_table, dest_table))
    return true;

  if (check_copy_table_legally(src_table, dest_table))
    goto err;

  if (mdl_upgrade_shared_lock(thd, dest_table->table))
    goto err;

  has_upgraded_shared_lock = true;

  if (load_source_table_data_to_dest(thd, src_table, dest_table))
    goto err;

  if (commit_copy_table_cmd(thd))
    goto err;

  hton = table_list->table->s->db_type();
  if (table_list->table->s->tmp_table == NO_TMP_TABLE &&
      (hton->flags & HTON_SUPPORTS_ATOMIC_DDL) && hton->post_ddl)
    hton->post_ddl(thd);

  if (thd->locked_tables_mode && thd->locked_tables_list.reopen_tables(thd))
    goto err;

  mdl_downgrade_lock(thd, dest_table->table);

  clear_up_after_copy_table(thd, dest_table);

  return false;

err:
  if (has_upgraded_shared_lock)
    mdl_downgrade_lock(thd, dest_table->table);

  rollback_for_copy_table(thd);

  return true;
}

#endif // HAVE_ZSQL_COPY_TABLE
