/* Copyright (c) 2009, 2018, Oracle and/or its affiliates. All rights reserved.
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

#include "sql/gtm_gtid_idx_file.h"

#include "sql/derror.h"
#include "sql/binlog.h"
#include "sql/log.h"                            // sql_print_warning
#include "mysql/psi/mysql_file.h"

#define GTMGTID_IDX_FILE_COMPLETED  "0"
#define GTMGTID_IDX_FILE_UNCOMPLETE "1"
#define GTMGTID_IDX_FILE_FLAG_SIZE  1U

#define GTMGTID_IDX_FILE_TITLE_STR  "#gtm_gtid #binlog_start_pos"
#define GTMGTID_IDX_FILE_TITLE_SIZE sizeof(GTMGTID_IDX_FILE_TITLE_STR)

#define GTMGTID_IDX_MY_FILEPOS_ERROR (~0)


bool MYSQL_BIN_LOG::open_gtmgtid_idx_file_and_write_title()
{
  DBUG_ENTER("MYSQL_BIN_LOG::open_gtmgtid_idx_file_and_write_title");
  if (gtmgtid_idx_switch)
    DBUG_RETURN(open_gtmgtid_idx_file() || write_gtmgtid_idx_title());
  else
    DBUG_RETURN(false);
}

bool MYSQL_BIN_LOG::open_gtmgtid_idx_file()
{
  File fd = -1;
  my_off_t gtm_gtid_idx_pos = 0;
  DBUG_ENTER("MYSQL_BIN_LOG::open_gtmgtid_idx_file");

  if (is_relay_log)
    DBUG_RETURN(false);

  gtmgtid_idx_write_state = GTMGTID_IDX_WRITE_NORMAL;

  snprintf(gtmgtid_idx_file_name, 
           sizeof(gtmgtid_idx_file_name),
           "%s.gtid_idx", log_file_name);

  if ((fd = mysql_file_open(m_key_file_log_gtmgtid_idx,
                            gtmgtid_idx_file_name, O_CREAT | O_RDWR,
                            MYF(MY_WME))) < 0)
    goto err;

  if ((gtm_gtid_idx_pos = mysql_file_tell(fd, MYF(MY_WME))) == 
      static_cast<my_off_t>(GTMGTID_IDX_MY_FILEPOS_ERROR))
  {
    if (my_errno() == ESPIPE)
      gtm_gtid_idx_pos = 0;
    else
      goto err;
  }

  if (init_io_cache(&gtmgtid_idx_file, fd, IO_SIZE, 
                    WRITE_CACHE, gtm_gtid_idx_pos, 0, 
                    MYF(MY_WME | MY_NABP | MY_WAIT_IF_FULL)))
    goto err;
  
  DBUG_RETURN(false);

err:
  if (fd >= 0)
    mysql_file_close(fd, MYF(0));
  end_io_cache(&gtmgtid_idx_file);

  DBUG_RETURN(true);
}

bool MYSQL_BIN_LOG::write_gtmgtid_idx_title()
{
  DBUG_ENTER("MYSQL_BIN_LOG::write_gtmgtid_idx_title");

  if (!is_relay_log && !my_b_filelength(&gtmgtid_idx_file))
  {
    /*
      The gtid_idx file was empty (probably newly created)
      In this case we write a header to it:
        flag#gtm_gtid #binlog_start_pos
        flag: 1 byte. 0 means gtid_idx file completed, 1 means not;
        #...: col name. ' ' between col names;
    */
    if (my_b_safe_write(&gtmgtid_idx_file,
                        reinterpret_cast<const uchar*>(GTMGTID_IDX_FILE_UNCOMPLETE), 
                        GTMGTID_IDX_FILE_FLAG_SIZE)      ||
        my_b_safe_write(&gtmgtid_idx_file, 
                        reinterpret_cast<const uchar*>(GTMGTID_IDX_FILE_TITLE_STR), 
                        GTMGTID_IDX_FILE_TITLE_SIZE - 1) ||
        my_b_safe_write(&gtmgtid_idx_file, reinterpret_cast<const uchar*>("\n"), 1U))
    {
      mark_gtmgtid_idx_write_error();
      DBUG_RETURN(true);
    }

    if (flush_io_cache(&gtmgtid_idx_file))
    {
      mark_gtmgtid_idx_write_error();
      DBUG_RETURN(true);
    }
    // no need: mysql_file_sync(gtmgtid_idx_file.file, MYF(MY_WME))
  }
  DBUG_RETURN(false);
}

/**
   Write one line to the gtm_gtid idx file:
     "cur_gtm_gtid cur_binlog_pos" 
   NOTE:
     between this two is ' ';
     end with '\n';
 
   @retval true IO error.
   @retval false Success.
*/
void MYSQL_BIN_LOG::flush_gtmgtid_idx(THD *thd)
{
  DBUG_ENTER("MYSQL_BIN_LOG::flush_gtmgtid_idx()");

  if (gtmgtid_idx_switch &&
      GTMGTID_IDX_WRITE_NORMAL == gtmgtid_idx_write_state && 
      0 != thd->m_trx_gtm_gtid[0])
  {
    /* buff: gtm_gtid binlog_pos \n \0 */
    char buff[MAX_GTID_LEN + MAX_BINLOG_POS_NUMLEN + 3];
    size_t length = snprintf(buff, sizeof(buff), "%s %llu\n", 
                             thd->m_trx_gtm_gtid, 
                             thd->get_prev_trans_end_pos());

    if (0 != my_b_write(&gtmgtid_idx_file, 
                        reinterpret_cast<const uchar*>(buff), length))
    {
      mark_gtmgtid_idx_write_error();
    }
  }
  DBUG_VOID_RETURN;
}

/*
  Flush the I/O cache to gtid_idx file.
*/
int MYSQL_BIN_LOG::flush_cache_to_gtmgtid_idx_file()
{
  int error = 0;
  if (gtmgtid_idx_switch && (error = flush_io_cache(&gtmgtid_idx_file)))
    mark_gtmgtid_idx_write_error();
  return error;
}

void MYSQL_BIN_LOG::close_gtmgtid_idx_file()
{
  DBUG_ENTER("MYSQL_BIN_LOG::close_gtmgtid_idx_file");

  if (gtmgtid_idx_switch && !is_relay_log && my_b_inited(&gtmgtid_idx_file))
  {
    // no need: set 0 to first byte of IO_CACHE 

    if (flush_io_cache(&gtmgtid_idx_file))
      mark_gtmgtid_idx_write_error();
    if (end_io_cache(&gtmgtid_idx_file))
      mark_gtmgtid_idx_write_error();

    // no need: mysql_file_sync(gtmgtid_idx_file.file, MYF(MY_WME))

    if (mysql_file_close(gtmgtid_idx_file.file, MYF(MY_WME)))
      mark_gtmgtid_idx_write_error();
  }
  DBUG_VOID_RETURN;
}

int MYSQL_BIN_LOG::register_purge_gtmgtid_idx_entry(const char *entry, 
                                                 size_t entry_len)
{
  int error = 0;
  DBUG_ENTER("MYSQL_BIN_LOG::register_purge_index_entry");

  if (!gtmgtid_idx_switch)
    DBUG_RETURN (0);

  !is_relay_log &&
  ((error = my_b_write(&purge_index_file,
                       reinterpret_cast<const uchar*>(entry), 
                       entry_len)) ||
   (error = my_b_write(&purge_index_file,
                       reinterpret_cast<const uchar*>(".gtid_idx\n"),
                       static_cast<size_t>(GTMGTID_IDX_FILE_SUFFIX_LEN + 1))));
  DBUG_RETURN (error);
}

/**
   reset_logs() call this func
     RESET MASTER or RESET SLAVE
 
   @param binlog_fname   binlog file name 

   @retval true  error.
   @retval false Success.
*/
bool MYSQL_BIN_LOG::del_gtmgtid_idx_file_allow_opened(const char *binlog_fname)
{
  char gtmgtid_idx_fname[FN_REFLEN + GTMGTID_IDX_FILE_SUFFIX_LEN];
  DBUG_ENTER("MYSQL_BIN_LOG::del_gtmgtid_idx_file_allow_opened");

  if (!gtmgtid_idx_switch)
    DBUG_RETURN(false);

  snprintf(gtmgtid_idx_fname, sizeof(gtmgtid_idx_fname),
           "%s.gtid_idx", binlog_fname);

  if (!is_relay_log && my_delete_allow_opened(gtmgtid_idx_fname, MYF(0)) != 0)
  {
    if (my_errno() == ENOENT) 
    {
      push_warning_printf(current_thd, Sql_condition::SL_WARNING,
                          ER_LOG_PURGE_NO_FILE, ER_THD(current_thd, ER_LOG_PURGE_NO_FILE),
                          gtmgtid_idx_fname);
      sql_print_information("Failed to delete file '%s'",
                            gtmgtid_idx_fname);
      set_my_errno(0);
    }
    else
    {
      push_warning_printf(current_thd, Sql_condition::SL_WARNING,
                          ER_BINLOG_PURGE_FATAL_ERR,
                          "a problem with deleting %s; "
                          "consider examining correspondence "
                          "of your binlog index file "
                          "to the actual binlog files and gtid_idx files",
                          gtmgtid_idx_fname);
      DBUG_RETURN(true);
    }
  }
  DBUG_RETURN(false);
}

void MYSQL_BIN_LOG::set_gtmgtid_idx_write_state(enum_gtmgtid_idx_write_state wstate)
{
  DBUG_ENTER("MYSQL_BIN_LOG::set_gtmgtid_idx_write_state");
  if (gtmgtid_idx_switch)
  {
    mysql_mutex_lock(&LOCK_log);
    gtmgtid_idx_write_state = wstate;
    mysql_mutex_unlock(&LOCK_log);
  }
  DBUG_VOID_RETURN;
}

void MYSQL_BIN_LOG::mark_gtmgtid_idx_write_error()
{
  char errbuf[MYSYS_STRERROR_SIZE];
  gtmgtid_idx_write_state = GTMGTID_IDX_WRITE_ERROR;
  sql_print_error(ER_DEFAULT(ER_ERROR_ON_WRITE), gtmgtid_idx_file_name,
                  errno, my_strerror(errbuf, sizeof(errbuf), errno));
}
