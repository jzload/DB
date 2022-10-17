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


#ifndef RPL_SEMISYNC_MASTER_PLUGIN_H
#define RPL_SEMISYNC_MASTER_PLUGIN_H

#include "sql/rpl_semisync_master.h"
#include "sql/rpl_semisync_master_ack_receiver.h"
#include "sql/rpl_semisync_master_inspection.h"

extern ReplSemiSyncMaster *repl_semisync_master;
extern Ack_receiver *ack_receiver;
extern Time_inspection *time_inspection;

/* The places at where semisync waits for binlog ACKs. */
enum enum_wait_point { WAIT_AFTER_SYNC, WAIT_AFTER_COMMIT };

extern ulong rpl_semi_sync_master_wait_point;

extern int semi_sync_master_plugin_init();
extern int semi_sync_master_plugin_deinit();

int set_is_quick_sync_enabled();
int repl_semi_after_send_event(Binlog_transmit_param *param,
                               const char *event_buf, unsigned long,
                               const char * skipped_log_file,
                               my_off_t skipped_log_pos);
int repl_semi_before_send_event(Binlog_transmit_param *param,
                                unsigned char *packet, unsigned long,
                                const char *log_file, my_off_t log_pos);
int repl_semi_reserve_header(Binlog_transmit_param *,
                             unsigned char *header,
                             unsigned long size, unsigned long *len);
int repl_semi_binlog_dump_end(Binlog_transmit_param *param);
int repl_semi_report_begin(Trans_param *, int &);
int repl_semi_binlog_dump_start(Binlog_transmit_param *param,
                                const char *log_file,
                                my_off_t log_pos);
int repl_semi_report_commit(Trans_param *param);
int repl_semi_report_binlog_sync(Binlog_storage_param *param,
                                 const char *log_file, 
                                 my_off_t log_pos);
int repl_semi_report_binlog_update(Binlog_storage_param *,
                                   const char *log_file,
                                   my_off_t log_pos);
int repl_semi_report_before_dml(Trans_param *, int&);
int repl_semi_report_before_commit(Trans_param *);
int repl_semi_report_before_rollback(Trans_param *);
int repl_semi_report_rollback(Trans_param *param);
int repl_semi_reset_master(Binlog_transmit_param *param);

#ifdef HAVE_PSI_INTERFACE
extern void init_semisync_psi_keys(void);
#endif /* HAVE_PSI_INTERFACE */

#endif /* RPL_SEMISYNC_MASTER_PLUGIN_H */
