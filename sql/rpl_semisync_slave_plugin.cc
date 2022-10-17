/* Copyright (C) 2007 Google Inc.
   Copyright (c) 2008, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include <mysql.h>
#include <mysqld_error.h>
#include <stdlib.h>
#include <sys/types.h>

#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_macros.h"
#include "sql/rpl_semisync_slave.h"
#include "sql/rpl_semisync_slave_plugin.h"

ReplSemiSyncSlave *repl_semisync_slave = nullptr;

/*
  indicate whether or not the slave should send a reply to the master.

  This is set to true in repl_semi_slave_read_event if the current
  event read is the last event of a transaction. And the value is
  checked in repl_semi_slave_queue_event.
*/
bool semi_sync_need_reply = false;


int repl_semi_apply_slave(Binlog_relay_IO_param *, Trans_param *,
                                 int &) {
  // TODO: implement
  return 0;
}

int repl_semi_reset_slave(Binlog_relay_IO_param *) {
  // TODO: reset semi-sync slave status here
  return 0;
}

int repl_semi_slave_request_dump(Binlog_relay_IO_param *param, uint32) {
  MYSQL *mysql = param->mysql;
  MYSQL_RES *res = 0;
#ifndef DBUG_OFF
  MYSQL_ROW row = NULL;
#endif
  const char *query;
  uint mysql_error = 0;

  if (!repl_semisync_slave->getSlaveEnabled()) return 0;

  /* Check if master server has semi-sync plugin installed */
  query = "SELECT @@global.rpl_semi_sync_master_enabled";
  if (mysql_real_query(mysql, query, static_cast<ulong>(strlen(query))) ||
      !(res = mysql_store_result(mysql))) {
    mysql_error = mysql_errno(mysql);
    if (mysql_error != ER_UNKNOWN_SYSTEM_VARIABLE) {
      LogErr(ERROR_LEVEL, ER_SEMISYNC_EXECUTION_FAILED_ON_MASTER, query,
                   mysql_error);
      return 1;
    }
  } else {
#ifndef DBUG_OFF
    row =
#endif
        mysql_fetch_row(res);
  }

  DBUG_ASSERT(mysql_error == ER_UNKNOWN_SYSTEM_VARIABLE ||
              strtoul(row[0], 0, 10) == 0 || strtoul(row[0], 0, 10) == 1);

  if (mysql_error == ER_UNKNOWN_SYSTEM_VARIABLE) {
    /* Master does not support semi-sync */
    LogErr(WARNING_LEVEL, ER_SEMISYNC_NOT_SUPPORTED_BY_MASTER);
    rpl_semi_sync_slave_status = 0;
    mysql_free_result(res);
    return 0;
  }
  mysql_free_result(res);

  /*
    Tell master dump thread that we want to do semi-sync
    replication
  */
  query = "SET @rpl_semi_sync_slave= 1";
  if (mysql_real_query(mysql, query, static_cast<ulong>(strlen(query)))) {
    LogErr(ERROR_LEVEL, ER_SEMISYNC_SLAVE_SET_FAILED);
    return 1;
  }
  mysql_free_result(mysql_store_result(mysql));
  rpl_semi_sync_slave_status = 1;
  return 0;
}

int repl_semi_slave_read_event(Binlog_relay_IO_param *,
                                      const char *packet, unsigned long len,
                                      const char **event_buf,
                                      unsigned long *event_len) {
  if (rpl_semi_sync_slave_status)
    return repl_semisync_slave->slaveReadSyncHeader(
        packet, len, &semi_sync_need_reply, event_buf, event_len);
  *event_buf = packet;
  *event_len = len;
  return 0;
}
int repl_semi_slave_queue_event(Binlog_relay_IO_param *param,
                                       const char *, unsigned long, uint32) {
  if (rpl_semi_sync_slave_status && semi_sync_need_reply) {
    /*
      We deliberately ignore the error in slaveReply, such error
      should not cause the slave IO thread to stop, and the error
      messages are already reported.
    */
    (void)repl_semisync_slave->slaveReply(param->mysql, param->master_log_name,
                                    param->master_log_pos);
  }
  return 0;
}

int repl_semi_slave_io_start(Binlog_relay_IO_param *param) {
  return repl_semisync_slave->slaveStart(param);
}

int repl_semi_slave_io_end(Binlog_relay_IO_param *param) {
  return repl_semisync_slave->slaveStop(param);
}

int repl_semi_slave_sql_start(Binlog_relay_IO_param *) { return 0; }

int repl_semi_slave_sql_stop(Binlog_relay_IO_param *, bool) { return 0; }


Binlog_relay_IO_observer relay_io_observer = {
    sizeof(Binlog_relay_IO_observer),  // len

    repl_semi_slave_io_start,      // start
    repl_semi_slave_io_end,        // stop
    repl_semi_slave_sql_start,     // start sql thread
    repl_semi_slave_sql_stop,      // stop sql thread
    repl_semi_slave_request_dump,  // request_transmit
    repl_semi_slave_read_event,    // after_read_event
    repl_semi_slave_queue_event,   // after_queue_event
    repl_semi_reset_slave,         // reset
    repl_semi_apply_slave          // apply
};

int semi_sync_slave_plugin_init() {
  repl_semisync_slave = new ReplSemiSyncSlave();
  if (repl_semisync_slave->initObject()) {
    return 1;
  }

  return 0;
}

int semi_sync_slave_plugin_deinit() {
  // the plugin was not initialized, there is nothing to do here
  if (repl_semisync_slave == nullptr) return 0;

  delete repl_semisync_slave;
  repl_semisync_slave = nullptr;
  return 0;
}
