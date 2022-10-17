/* Copyright (c) 2006 MySQL AB, 2009 Sun Microsystems, Inc.
   Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.
   Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */


#ifndef RPL_SEMISYNC_SLAVE_PLUGIN_H
#define RPL_SEMISYNC_SLAVE_PLUGIN_H

#include "sql/rpl_semisync_slave.h"

extern ReplSemiSyncSlave *repl_semisync_slave;

extern int semi_sync_slave_plugin_init();
extern int semi_sync_slave_plugin_deinit();

int repl_semi_slave_io_end(Binlog_relay_IO_param *param);
int repl_semi_slave_io_start(Binlog_relay_IO_param *param);
int repl_semi_slave_queue_event(Binlog_relay_IO_param *param,
                const char *event_buf,
                unsigned long event_len,
                uint32 flags);
int repl_semi_slave_read_event(Binlog_relay_IO_param *param,
                   const char *packet, unsigned long len,
                   const char **event_buf, unsigned long *event_len);
int repl_semi_slave_request_dump(Binlog_relay_IO_param *param,
                 uint32 flags);
int repl_semi_apply_slave(Binlog_relay_IO_param *, Trans_param *,
                          int &);
int repl_semi_reset_slave(Binlog_relay_IO_param *param);
int repl_semi_slave_sql_start(Binlog_relay_IO_param *param);
int repl_semi_slave_sql_stop(Binlog_relay_IO_param *param, bool aborted);

#endif /* RPL_SEMISYNC_SLAVE_PLUGIN_H */
