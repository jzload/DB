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

#include <stddef.h>
#include <sys/types.h>

#include "my_inttypes.h"
#include "my_macros.h"
#include "my_psi_config.h"
#include "mysql/psi/mysql_memory.h"
#include "mysql/psi/mysql_stage.h"
#include "sql/rpl_semisync_master_ack_receiver.h"
#include "sql/current_thd.h"
#include "sql/protocol_classic.h"
#include "sql/sql_class.h"  // THD
#include "typelib.h"
#include "sql/mysqld.h"  // max_connections
#include "sql/rpl_semisync_master_plugin.h"

ReplSemiSyncMaster *repl_semisync_master = nullptr;
Ack_receiver *ack_receiver = nullptr;
Time_inspection *time_inspection = nullptr;

ulong rpl_semi_sync_master_wait_point = WAIT_AFTER_COMMIT;

thread_local bool THR_RPL_SEMI_SYNC_DUMP = false;

extern void rpl_set_ack_wait_start_time(THD *thd);
extern void rpl_set_ack_wait_time(THD *thd, bool set_followers);


static inline bool is_semi_sync_dump() 
{ 
  return THR_RPL_SEMI_SYNC_DUMP;
}

int set_is_quick_sync_enabled()
{
  if(rpl_semi_sync_master_enabled &&
     rpl_semi_sync_master_quick_sync_enabled &&
     WAIT_AFTER_SYNC == rpl_semi_sync_master_wait_point)
  {
    g_is_quick_sync_enabled.store(1);
  }
  else
  {
    g_is_quick_sync_enabled.store(0);
  }

  return 0;
}

int repl_semi_report_binlog_update(Binlog_storage_param *,
                                   const char *log_file,
                                   my_off_t log_pos) {
  int error = 0;

  if (repl_semisync_master->getMasterEnabled()) {
    /*
      Let us store the binlog file name and the position, so that
      we know how long to wait for the binlog to the replicated to
      the slave in synchronous replication.
    */
    error = repl_semisync_master->writeTranxInBinlog(log_file, log_pos);
  }

  return error;
}

int repl_semi_report_binlog_sync(Binlog_storage_param *param,
                                 const char *log_file,
                                 my_off_t log_pos) {
  THD *thd = param->thd;

  if (thd && thd->m_group_quick_sync) {
    rpl_set_ack_wait_start_time(thd);
    return repl_semisync_master->quick_sync_commitTrx(thd, log_file, log_pos);
  }

  int ret = 0;
  if (rpl_semi_sync_master_quick_sync_enabled) {
    if (rpl_semi_sync_master_wait_point == WAIT_AFTER_SYNC) {
      rpl_set_ack_wait_start_time(thd);
      ret = repl_semisync_master->quick_sync_wait_commitTrx(thd,
                                                            log_file,
                                                            log_pos);
      rpl_set_ack_wait_time(thd, true);
    }
  } else {
    if (rpl_semi_sync_master_wait_point == WAIT_AFTER_SYNC) {
      rpl_set_ack_wait_start_time(thd);
      ret = repl_semisync_master->commitTrx(log_file, log_pos);
      rpl_set_ack_wait_time(thd, true);
    }
  }

  return ret;
}

int repl_semi_report_before_dml(Trans_param *, int &) { return 0; }

int repl_semi_report_before_commit(Trans_param *) { return 0; }

int repl_semi_report_before_rollback(Trans_param *) { return 0; }

int repl_semi_report_commit(Trans_param *param) {
  int ret = 0;
  THD *thd = param->thd;
  bool is_real_trans = param->flags & TRANS_IS_REAL_TRANS;
  
  if(rpl_semi_sync_master_quick_sync_enabled)
  {
    if (rpl_semi_sync_master_wait_point == WAIT_AFTER_COMMIT &&
        is_real_trans && param->log_pos)
    {
      rpl_set_ack_wait_start_time(thd);
      const char *binlog_name= param->log_file;
      ret = repl_semisync_master->quick_sync_wait_commitTrx(param->thd, binlog_name, param->log_pos);
      rpl_set_ack_wait_time(thd, false);
    }
  }
  else
  {
    if (rpl_semi_sync_master_wait_point == WAIT_AFTER_COMMIT && is_real_trans &&
        param->log_pos) {
      rpl_set_ack_wait_start_time(thd);
      const char *binlog_name = param->log_file;
      return repl_semisync_master->commitTrx(binlog_name, param->log_pos);
      rpl_set_ack_wait_time(thd, false);
    }
  }
  
  return ret;
}

int repl_semi_report_rollback(Trans_param *param) {
  return repl_semi_report_commit(param);
}

int repl_semi_report_begin(Trans_param *, int &) { return 0; }

int repl_semi_binlog_dump_start(Binlog_transmit_param *param,
                                const char *log_file, my_off_t log_pos) {
  long long semi_sync_slave = 0;

  /*
    semi_sync_slave will be 0 if the user variable doesn't exist. Otherwise, it
    will be set to the value of the user variable.
    'rpl_semi_sync_slave = 0' means that it is not a semisync slave.
  */
  get_user_var_int("rpl_semi_sync_slave", &semi_sync_slave, NULL);

  if (semi_sync_slave != 0) {
    if (ack_receiver->add_slave(current_thd)) {
      LogErr(ERROR_LEVEL, ER_SEMISYNC_FAILED_REGISTER_SLAVE_TO_RECEIVER);
      return -1;
    }

    THR_RPL_SEMI_SYNC_DUMP = true;

    /* One more semi-sync slave */
    repl_semisync_master->add_slave(param->server_id);

    /* Tell server it will observe the transmission.*/
    param->set_observe_flag();

    /*
      Let's assume this semi-sync slave has already received all
      binlog events before the filename and position it requests.
    */
    repl_semisync_master->handleAck(param->server_id, log_file, log_pos);
  } else
    param->set_dont_observe_flag();

  LogErr(INFORMATION_LEVEL, ER_SEMISYNC_START_BINLOG_DUMP_TO_SLAVE,
         semi_sync_slave != 0 ? "semi-sync" : "asynchronous", param->server_id,
         log_file, (unsigned long)log_pos);
  return 0;
}

int repl_semi_binlog_dump_end(Binlog_transmit_param *param) {
  bool semi_sync_slave = is_semi_sync_dump();

  LogErr(INFORMATION_LEVEL, ER_SEMISYNC_STOP_BINLOG_DUMP_TO_SLAVE,
         semi_sync_slave ? "semi-sync" : "asynchronous", param->server_id);

  if (semi_sync_slave) {
    ack_receiver->remove_slave(current_thd);
    /* One less semi-sync slave */
    repl_semisync_master->remove_slave(param->server_id);
    THR_RPL_SEMI_SYNC_DUMP = false;
  }
  return 0;
}

int repl_semi_reserve_header(Binlog_transmit_param *,
                             unsigned char *header, unsigned long size,
                             unsigned long *len) {
  if (is_semi_sync_dump())
    *len += repl_semisync_master->reserveSyncHeader(header, size);
  return 0;
}

int repl_semi_before_send_event(Binlog_transmit_param *param,
                                unsigned char *packet, unsigned long,
                                const char *log_file, my_off_t log_pos) {
  if (!is_semi_sync_dump()) return 0;

  return repl_semisync_master->updateSyncHeader(packet, log_file, log_pos,
                                         param->server_id);
}

int repl_semi_after_send_event(Binlog_transmit_param *param,
                               const char *event_buf, unsigned long,
                               const char *skipped_log_file,
                               my_off_t skipped_log_pos) {
  if (is_semi_sync_dump()) {
    if (skipped_log_pos > 0)
      repl_semisync_master->skipSlaveReply(event_buf, param->server_id,
                                    skipped_log_file, skipped_log_pos);
    else {
      THD *thd = current_thd;
      /*
        Possible errors in reading slave reply are ignored deliberately
        because we do not want dump thread to quit on this. Error
        messages are already reported.
      */
      (void)repl_semisync_master->readSlaveReply(
          thd->get_protocol_classic()->get_net(), event_buf);
      thd->clear_error();
    }
  }
  return 0;
}

int repl_semi_reset_master(Binlog_transmit_param *) {
  if (repl_semisync_master->resetMaster()) return 1;
  return 0;
}



Trans_observer trans_observer = {
    sizeof(Trans_observer),  // len

    repl_semi_report_before_dml,       // before_dml
    repl_semi_report_before_commit,    // before_commit
    repl_semi_report_before_rollback,  // before_rollback
    repl_semi_report_commit,           // after_commit
    repl_semi_report_rollback,         // after_rollback
    repl_semi_report_begin,            // begin
};

Binlog_storage_observer storage_observer = {
    sizeof(Binlog_storage_observer),  // len

    repl_semi_report_binlog_update,  // report_update
    repl_semi_report_binlog_sync,    // after_sync
};

Binlog_transmit_observer transmit_observer = {
    sizeof(Binlog_transmit_observer),  // len

    repl_semi_binlog_dump_start,  // start
    repl_semi_binlog_dump_end,    // stop
    repl_semi_reserve_header,     // reserve_header
    repl_semi_before_send_event,  // before_send_event
    repl_semi_after_send_event,   // after_send_event
    repl_semi_reset_master,       // reset
};


#ifdef HAVE_PSI_INTERFACE

PSI_mutex_key key_ss_mutex_LOCK_binlog_;
PSI_mutex_key key_ss_mutex_Ack_receiver_mutex;
PSI_mutex_key key_ss_mutex_Time_inspection_mutex;

static PSI_mutex_info all_semisync_mutexes[] = {
    {&key_ss_mutex_LOCK_binlog_, "LOCK_binlog_", 0, 0, PSI_DOCUMENT_ME},
    {&key_ss_mutex_Ack_receiver_mutex, "Ack_receiver::m_mutex", 0, 0, PSI_DOCUMENT_ME},
    {&key_ss_mutex_Time_inspection_mutex, "Time_inspection::m_mutex", 0,0, PSI_DOCUMENT_ME}
};

PSI_cond_key key_ss_cond_COND_binlog_send_;
PSI_cond_key key_ss_cond_Ack_receiver_cond;
PSI_cond_key key_ss_cond_Time_inspection_cond;

static PSI_cond_info all_semisync_conds[] = {
    {&key_ss_cond_COND_binlog_send_, "COND_binlog_send_", 0, 0,
     PSI_DOCUMENT_ME},
    {&key_ss_cond_Ack_receiver_cond, "Ack_receiver::m_cond", 0, 0,
     PSI_DOCUMENT_ME},
    {&key_ss_cond_Time_inspection_cond, "Time_inspection::m_cond", 0, 0,
     PSI_DOCUMENT_ME}
};

PSI_thread_key key_ss_thread_Ack_receiver_thread;
PSI_thread_key key_ss_thread_Time_inspection_thread;

static PSI_thread_info all_semisync_threads[] = {
    {&key_ss_thread_Ack_receiver_thread, "Ack_receiver", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME},
    {&key_ss_thread_Time_inspection_thread, "Time_inspection", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME}
};
#endif /* HAVE_PSI_INTERFACE */

PSI_stage_info stage_waiting_for_semi_sync_ack_from_slave = {
    0, "Waiting for semi-sync ACK from slave", 0, PSI_DOCUMENT_ME};

PSI_stage_info stage_waiting_for_semi_sync_slave = {
    0, "Waiting for semi-sync slave connection", 0, PSI_DOCUMENT_ME};

PSI_stage_info stage_reading_semi_sync_ack = {
    0, "Reading semi-sync ACK from slave", 0, PSI_DOCUMENT_ME};

/* Always defined. */
PSI_memory_key key_ss_memory_TranxNodeAllocator_block;

#ifdef HAVE_PSI_INTERFACE
PSI_stage_info *all_semisync_stages[] = {
    &stage_waiting_for_semi_sync_ack_from_slave,
    &stage_waiting_for_semi_sync_slave, &stage_reading_semi_sync_ack};

PSI_memory_info all_semisync_memory[] = {
    {&key_ss_memory_TranxNodeAllocator_block, "TranxNodeAllocator::block", 0, 0,
     PSI_DOCUMENT_ME}};

void init_semisync_psi_keys(void) {
  const char *category = "semisync";
  int count;

  count = static_cast<int>(array_elements(all_semisync_mutexes));
  mysql_mutex_register(category, all_semisync_mutexes, count);

  count = static_cast<int>(array_elements(all_semisync_conds));
  mysql_cond_register(category, all_semisync_conds, count);

  count = static_cast<int>(array_elements(all_semisync_stages));
  mysql_stage_register(category, all_semisync_stages, count);

  count = static_cast<int>(array_elements(all_semisync_memory));
  mysql_memory_register(category, all_semisync_memory, count);

  count = static_cast<int>(array_elements(all_semisync_threads));
  mysql_thread_register(category, all_semisync_threads, count);
}
#endif /* HAVE_PSI_INTERFACE */

int semi_sync_master_plugin_init() {
#ifdef HAVE_PSI_INTERFACE
  init_semisync_psi_keys();
#endif

  THR_RPL_SEMI_SYNC_DUMP = false;

  /*
    In case the plugin has been unloaded, and reloaded, we may need to
    re-initialize some global variables.
    These are initialized to zero by the linker, but may need to be
    re-initialized
  */
  rpl_semi_sync_master_no_transactions = 0;
  rpl_semi_sync_master_yes_transactions = 0;

  repl_semisync_master = new ReplSemiSyncMaster();
  ack_receiver = new Ack_receiver();
  time_inspection = new Time_inspection();

  if (repl_semisync_master->initObject()) {
    return 1;
  }
  
  set_is_quick_sync_enabled();
  
  if (ack_receiver->init()) {
    return 1;
  }
  
  if (time_inspection->init()) {
    return 1;
  }

  return 0;
}

int semi_sync_master_plugin_deinit() {
  // the plugin was not initialized, there is nothing to do here
  if (ack_receiver == nullptr || repl_semisync_master == nullptr) return 0;

  THR_RPL_SEMI_SYNC_DUMP = false;

  repl_semisync_master->deinit();
  time_inspection->stop();

  delete ack_receiver;
  ack_receiver = nullptr;
  delete repl_semisync_master;
  repl_semisync_master = nullptr;
  delete time_inspection;
  time_inspection = nullptr;

  LogErr(INFORMATION_LEVEL, ER_SEMISYNC_UNREGISTERED_REPLICATOR);
  return 0;
}
