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

#include "sql/rpl_semisync_master.h"

#include <assert.h>
#include <time.h>

#include "my_byteorder.h"
#include "my_compiler.h"
#include "my_systime.h"
#include "sql/mysqld.h"  // max_connections
#include "sql/sql_parse.h"
#include "sql/threadpool.h"

#if defined(ENABLED_DEBUG_SYNC)
#include "sql/current_thd.h"
#include "sql/debug_sync.h"
#endif

/* This indicates whether semi-synchronous replication is enabled. */
bool rpl_semi_sync_master_enabled;
unsigned long rpl_semi_sync_master_timeout;
unsigned long rpl_semi_sync_master_trace_level;
char rpl_semi_sync_master_status = 0;
unsigned long rpl_semi_sync_master_yes_transactions = 0;
unsigned long rpl_semi_sync_master_no_transactions = 0;
unsigned long rpl_semi_sync_master_off_times = 0;
unsigned long rpl_semi_sync_master_timefunc_fails = 0;
unsigned long rpl_semi_sync_master_wait_timeouts = 0;
unsigned long rpl_semi_sync_master_wait_sessions = 0;
unsigned long rpl_semi_sync_master_wait_pos_backtraverse = 0;
unsigned long rpl_semi_sync_master_avg_trx_wait_time = 0;
unsigned long long rpl_semi_sync_master_trx_wait_num = 0;
unsigned long rpl_semi_sync_master_avg_net_wait_time = 0;
unsigned long long rpl_semi_sync_master_net_wait_num = 0;
unsigned long rpl_semi_sync_master_clients = 0;
unsigned long long rpl_semi_sync_master_net_wait_time = 0;
unsigned long long rpl_semi_sync_master_trx_wait_time = 0;
bool rpl_semi_sync_master_wait_no_slave = 1;
unsigned int rpl_semi_sync_master_wait_for_slave_count = 1;

/************************ add for quick semisync ***********************/
/***** the rpl variables *****/
// the quick sync mode enabled config. 
char rpl_semi_sync_master_quick_sync_enabled = 1;
// the high_water_mask of quick sync wait condition 
char *rpl_semi_sync_master_wait_cond_hwm = NULL;
// the low_water_mask of quick sync wait condition 
char *rpl_semi_sync_master_wait_cond_lwm = NULL;
unsigned long rpl_semi_sync_master_inspection_time  = 0;
char *rpl_semi_sync_master_group[MAX_RPL_GROUP_NUM] = {0};

/***** the rpl status for user *****/
// the flag of quick_sync_err, 0 is ok, 1 and 2 mean error
unsigned int rpl_semi_sync_master_err_flag            = 0;
// all the active groups at this moment
unsigned int rpl_semi_sync_master_active_groups       = 0;
// the current stat master flag, 0 is not stat master, 1 is stat master
char rpl_semi_sync_master_current_stat_master_flag    = 0;
// the current wait group num, not contain the group not wait 
unsigned int rpl_semi_sync_master_current_wait_groups = 0;
// all the group status
char rpl_semi_sync_master_group_status[MAX_RPL_GROUP_NUM][MAX_GROUP_STATUS_LEN] = {{0}};

/***** the rpl status for interior *****/
// the num of active master_group in hwm, the value is 0 or 1
unsigned int rpl_active_master_groups_hwm = 0;
// the num of active master_group in lwm, the value is 0 or 1
unsigned int rpl_active_master_groups_lwm = 0;
unsigned int rpl_active_groups_without_master_group = 0;
unsigned int rpl_wait_count_hwm   = 1;
unsigned int rpl_wait_count_lwm   = 0;
char rpl_with_master_stat_hwm     = 0;
char rpl_with_master_stat_lwm     = 0;
unsigned int rpl_serverid   = 0;

static int getWaitTime(const struct timespec &start_ts);

static unsigned long long timespec_to_usec(const struct timespec *ts) {
  return (unsigned long long)ts->tv_sec * TIME_MILLION +
         ts->tv_nsec / TIME_THOUSAND;
}

static void set_trans_start_and_end_time(TranxNode *entry, unsigned long timeout)
{
  struct timespec *ts = &entry->time_end;
  set_timespec(ts, 0);
  set_timespec(&entry->time_start,0);
  
  /* Calcuate the waiting period. */
    ts->tv_sec  = ts->tv_sec + timeout / TIME_THOUSAND;
    ts->tv_nsec = ts->tv_nsec + (timeout % TIME_THOUSAND) * TIME_MILLION;
    if (ts->tv_nsec >= TIME_BILLION)
    {
      ts->tv_sec++;
      ts->tv_nsec -= TIME_BILLION;
    }
}

static bool set_quick_sync_flag(THD *thd, int err_flag)
{
  if(err_flag)
  {
    THD *tmp_thd = thd;
    while(tmp_thd)
    {
      tmp_thd->m_quick_sync_err = err_flag;
      tmp_thd = tmp_thd->next_to_commit;
    }
  }

  return (err_flag!=0);
}

static bool is_space(char c)
{
  if((c == ' ')||(c == '\t'))
   return true;
  
  return false;
}

static bool is_digital(char c)
{
  if(c >= '0' && c <= '9')
   return true;
  
  return false;
}

static bool is_space_or_digital(char c)
{
  if(is_space(c) || is_digital(c))
   return true;
  
  return false;
}

/* find the digital in the string between begin and end */
static bool is_str_with_digital(const char *begin, char *end)
{
  const char *pos = begin;
  while(*pos && (pos<end))
  {
    if(is_digital(*pos))
      return true;
    pos++;
  }
  return false;
}

static bool is_str_null(char *str)
{
  char *pos = str;
  while(*pos)
  {
    if(!is_space(*pos))
      return false;
    pos++;
  }
  return true;
}
static longlong get_number(char *begin, char *end)
{
  int error = 0;
  longlong number = 0;
  const char *end_pos = (const char *)end;
  
  number = (longlong)my_strtoll10(begin, &end_pos, &error);
  if(error || number>MAX_INT32_NUM || number<0)
    return -1;

  /** 
    if the string is '31 , 21 : 1' ,the begin_pos is '3', the end_pos is',',
    after the call of my_strtoll10, the end_pos will be the ' ' before the first ','.
    so we should reserve the original pos.
    (the func my_strtoll10 will change the end_pos)
  */
  if(end > end_pos)
  {
    if(is_str_with_digital(end_pos, end))
      return -1;
  }
  
  return number;
}

static void set_master_serverid()
{
  rpl_serverid = (uint)server_id;
}

static bool is_below_lwm()
{
  if(rpl_active_groups_without_master_group+rpl_active_master_groups_lwm < rpl_wait_count_lwm)
    return true;
  
  return false;
}

static bool is_below_hwm()
{
  if(rpl_active_groups_without_master_group+rpl_active_master_groups_hwm < rpl_wait_count_hwm)
    return true;
  
  return false;
}

static const char *err_flag_to_str(unsigned int err_flag)
{
  if(GROUP_NORMAL_OK == err_flag)
    return "group status is ok";
  else if(GROUP_BELOW_LWM_ALARM_ERR == err_flag)
    return "group status is below lwm mode";
  else if(GROUP_BELOW_HWM_ALARM_ERR == err_flag)
    return "group status is below hwm mode";
  else
    return "unknown err_flag";
}

static int init_err_flag()
{
  if(GROUP_NORMAL_OK != rpl_semi_sync_master_err_flag)
  {
    sql_print_information("Quick-sync master:init err_flag[%d,%s].",
      GROUP_NORMAL_OK, err_flag_to_str(GROUP_NORMAL_OK));
  }
  rpl_semi_sync_master_err_flag = GROUP_NORMAL_OK;
  g_quick_sync_err_flag.store(0);
  return 0;
}

static int set_err_flag()
{
  unsigned int old_err_flag = rpl_semi_sync_master_err_flag;
  
  if(is_below_lwm())
    rpl_semi_sync_master_err_flag = GROUP_BELOW_LWM_ALARM_ERR;
  else if(is_below_hwm())
    rpl_semi_sync_master_err_flag = GROUP_BELOW_HWM_ALARM_ERR;
  else
    rpl_semi_sync_master_err_flag = GROUP_NORMAL_OK;

  if(old_err_flag != rpl_semi_sync_master_err_flag)
  {
    g_quick_sync_err_flag.store(rpl_semi_sync_master_err_flag);
    sql_print_information("Quick-sync master:set err_flag[%d,%s].", 
      rpl_semi_sync_master_err_flag, err_flag_to_str(rpl_semi_sync_master_err_flag));
  }
  
  return 0;
}

void rpl_set_ack_wait_start_time(THD *thd)
{
  if(!thd)
    return;

  thd->ack_wait_time = ulonglong2uint64_t(thd->current_utime());

  return;
}


/**
   slow log
   set ack_wait_time when slave notify acknowledge

   @param thd         THD handle

   @return void
*/
void rpl_set_ack_wait_time(THD *thd, bool set_followers)
{
  if(!thd)
    return;
  
  uint64_t ack_wait_time = ulonglong2uint64_t(thd->current_utime()) - thd->ack_wait_time;
  
  if (!set_followers)
  {
    thd->ack_wait_time = ack_wait_time;
    return;
  }
    
  THD *follower  = thd;
  while (follower)
  {
    follower->ack_wait_time =  ack_wait_time;   
    follower = follower->next_to_commit;
  }
  
  return;
}


/*******************************************************************************
 *
 * <ActiveTranx> class : manage all active transaction nodes
 *
 ******************************************************************************/

ActiveTranx::ActiveTranx(mysql_mutex_t *lock, unsigned long trace_level)
    : Trace(trace_level),
      allocator_(max_connections),
      num_entries_(max_connections << 1), /* Transaction hash table size
                                           * is set to double the size
                                           * of max_connections */
      lock_(lock) {
  /* No transactions are in the list initially. */
  trx_front_ = NULL;
  trx_rear_ = NULL;

  /* Create the hash table to find a transaction's ending event. */
  trx_htb_ = new TranxNode *[num_entries_];
  for (int idx = 0; idx < num_entries_; ++idx) trx_htb_[idx] = NULL;

  LogErr(INFORMATION_LEVEL, ER_SEMISYNC_RPL_INIT_FOR_TRX);
}

ActiveTranx::~ActiveTranx() {
  delete[] trx_htb_;
  trx_htb_ = NULL;
  num_entries_ = 0;
}

unsigned int ActiveTranx::calc_hash(const unsigned char *key,
                                    unsigned int length) {
  unsigned int nr = 1, nr2 = 4;

  /* The hash implementation comes from calc_hashnr() in mysys/hash.c. */
  while (length--) {
    nr ^=
        (((nr & 63) + nr2) * ((unsigned int)(unsigned char)*key++)) + (nr << 8);
    nr2 += 3;
  }
  return ((unsigned int)nr);
}

unsigned int ActiveTranx::get_hash_value(const char *log_file_name,
                                         my_off_t log_file_pos) {
  unsigned int hash1 =
      calc_hash((const unsigned char *)log_file_name, strlen(log_file_name));
  unsigned int hash2 =
      calc_hash((const unsigned char *)(&log_file_pos), sizeof(log_file_pos));

  return (hash1 + hash2) % num_entries_;
}

int ActiveTranx::compare(const char *log_file_name1, my_off_t log_file_pos1,
                         const char *log_file_name2, my_off_t log_file_pos2) {
  int cmp = strcmp(log_file_name1, log_file_name2);

  if (cmp != 0) return cmp;

  if (log_file_pos1 > log_file_pos2)
    return 1;
  else if (log_file_pos1 < log_file_pos2)
    return -1;
  return 0;
}

int ActiveTranx::insert_tranx_node(const char *log_file_name,
                                   my_off_t log_file_pos) {
  const char *kWho = "ActiveTranx:insert_tranx_node";
  TranxNode *ins_node;
  int result = 0;
  unsigned int hash_val;

  function_enter(kWho);

  ins_node = allocator_.allocate_node();
  if (!ins_node) {
    LogErr(ERROR_LEVEL, ER_SEMISYNC_FAILED_TO_ALLOCATE_TRX_NODE, kWho,
           log_file_name, (unsigned long)log_file_pos);
    result = -1;
    goto l_end;
  }

  /* insert the binlog position in the active transaction list. */
  strncpy(ins_node->log_name_, log_file_name, FN_REFLEN - 1);
  ins_node->log_name_[FN_REFLEN - 1] = 0; /* make sure it ends properly */
  ins_node->log_pos_ = log_file_pos;

  if (!trx_front_) {
    /* The list is empty. */
    trx_front_ = trx_rear_ = ins_node;
  } else {
    int cmp = compare(ins_node, trx_rear_);
    if (cmp > 0) {
      /* Compare with the tail first.  If the transaction happens later in
       * binlog, then make it the new tail.
       */
      trx_rear_->next_ = ins_node;
      trx_rear_ = ins_node;
    } else {
      /* Otherwise, it is an error because the transaction should hold the
       * mysql_bin_log.LOCK_log when appending events.
       */
      LogErr(ERROR_LEVEL, ER_SEMISYNC_BINLOG_WRITE_OUT_OF_ORDER, kWho,
             trx_rear_->log_name_, (unsigned long)trx_rear_->log_pos_,
             ins_node->log_name_, (unsigned long)ins_node->log_pos_);
      result = -1;
      goto l_end;
    }
  }

  hash_val = get_hash_value(ins_node->log_name_, ins_node->log_pos_);
  ins_node->hash_next_ = trx_htb_[hash_val];
  trx_htb_[hash_val] = ins_node;

  if (trace_level_ & kTraceDetail)
    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_INSERT_LOG_INFO_IN_ENTRY, kWho,
           ins_node->log_name_, (unsigned long)ins_node->log_pos_, hash_val);

l_end:
  return function_exit(kWho, result);
}

bool ActiveTranx::is_tranx_end_pos(const char *log_file_name,
                                   my_off_t log_file_pos) {
  const char *kWho = "ActiveTranx::is_tranx_end_pos";
  function_enter(kWho);

  unsigned int hash_val = get_hash_value(log_file_name, log_file_pos);
  TranxNode *entry = trx_htb_[hash_val];

  while (entry != NULL) {
    if (compare(entry, log_file_name, log_file_pos) == 0) break;

    entry = entry->hash_next_;
  }

  if (trace_level_ & kTraceDetail)
    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_PROBE_LOG_INFO_IN_ENTRY, kWho,
           log_file_name, (unsigned long)log_file_pos, hash_val);

  function_exit(kWho, (entry != NULL));
  return (entry != NULL);
}

int ActiveTranx::signal_waiting_sessions_all() {
  const char *kWho = "ActiveTranx::signal_waiting_sessions_all";
  function_enter(kWho);
  TranxNode* entry_next= NULL;
  for (TranxNode* entry= trx_front_; entry; )
  {
    /* Fix a bug: If the entry is a quick_sync tranxnode, it will be cleared.*/
    entry_next = entry->next_;
    signal_one_tranxnode(entry);
    entry = entry_next;
  }

  return function_exit(kWho, 0);
}

int ActiveTranx::signal_waiting_sessions_up_to(const char *log_file_name,
                                               my_off_t log_file_pos) {
  const char *kWho = "ActiveTranx::signal_waiting_sessions_up_to";
  function_enter(kWho);

  TranxNode *entry = trx_front_;
  TranxNode* entry_next= NULL;
  
  /* Fix a bug when call this func, the trx_front_ may be NULL. */
  if(NULL == entry)
    return function_exit(kWho, 0);
  
  int cmp = ActiveTranx::compare(entry->log_name_, entry->log_pos_,
                                 log_file_name, log_file_pos);
  while (entry && cmp <= 0) {
    entry_next = entry->next_;
    signal_one_tranxnode(entry);
    
    entry= entry_next;
    if (entry)
      cmp = ActiveTranx::compare(entry->log_name_, entry->log_pos_,
                                 log_file_name, log_file_pos);
  }

  return function_exit(kWho, (entry != NULL));
}

int ActiveTranx::release_unused_node()
{
  const char *kWho = "ActiveTranx::release_unused_node";
  function_enter(kWho);

  TranxNode* entry  = trx_front_;
  TranxNode* entry_pre = NULL;
  while (entry)
  {
    if(entry->thd)
      break;
    entry_pre = entry;
    entry  = entry->next_;
  }

  if(entry_pre)
    clear_active_tranx_nodes(entry_pre->log_name_, entry_pre->log_pos_);

  return function_exit(kWho, 0);
}

int ActiveTranx::signal_one_tranxnode(TranxNode *node)
{
  const char *kWho = "ActiveTranx::signal_one_tranxnode";
  function_enter(kWho);

  THD *tmp_thd = NULL;
  THD *next_thd = NULL;
  bool result = false;

  if(GROUP_NORMAL_OK != rpl_semi_sync_master_err_flag)
  {
    node->error_flag = rpl_semi_sync_master_err_flag;
  }

  /**
    In some cases, node->thd can be NULL. if the master receive the acks
    before the caller of commitTrx, the node->thd will be NULL.
  */
  tmp_thd = node->thd;
  while(tmp_thd)
  {   
    if(true == tmp_thd->m_group_quick_sync)
    {
      /** 
        If the tmp_thd be added to queue, we should not use it again.
        So we should reserve the m_next_leader of thd before add queue.
      */
      next_thd = tmp_thd->m_next_leader;
      tmp_thd->m_next_leader = NULL;
      result = set_quick_sync_flag(tmp_thd, node->error_flag);
      rpl_set_ack_wait_time(tmp_thd, true);
      threadpool_append_commit_to_high_prio_queue(tmp_thd);
      clear_after_quicksync(node, result);
      tmp_thd = next_thd;
    }
    else
    {
      next_thd = tmp_thd->m_next_leader;
      tmp_thd->m_next_leader = NULL;
      set_quick_sync_flag(tmp_thd, node->error_flag);
      tmp_thd = next_thd;
    }
  }

  node->thd = NULL;
  mysql_cond_broadcast(&node->cond);
  return function_exit(kWho, 0);
}

void ActiveTranx::clear_after_quicksync(TranxNode *node, bool flag)
{
  const char *kWho = "ActiveTranx::clear_after_quicksync";
  function_enter(kWho);

  if (node->n_waiters > 0)
  {
    node->n_waiters--;
    rpl_semi_sync_master_wait_sessions--;
  }
  
  int wait_time;
  wait_time = getWaitTime(node->time_start);
  if (wait_time < 0)
  {
    if (trace_level_ & kTraceGeneral)
    {
      sql_print_information("Assessment of waiting time for commitTrx "
                          "failed at wait position (%s, %lu)",
                          node->log_name_,
                          (unsigned long)(node->log_pos_));
    }
    rpl_semi_sync_master_timefunc_fails++;
  }
  else
  {
    rpl_semi_sync_master_trx_wait_num++;
    rpl_semi_sync_master_trx_wait_time += wait_time;
  }

  if((true == flag) || (0 == rpl_semi_sync_master_status))
  {
    rpl_semi_sync_master_no_transactions++;
  }
  else
  {
    rpl_semi_sync_master_yes_transactions++;
  }

  if(node->n_waiters == 0)
  {
    clear_active_tranx_nodes(node->log_name_, node->log_pos_);
  }
  
  return function_exit(kWho);
}

TranxNode *ActiveTranx::find_active_tranx_node(const char *log_file_name,
                                               my_off_t log_file_pos) {
  const char *kWho = "ActiveTranx::find_active_tranx_node";
  function_enter(kWho);

  TranxNode *entry = trx_front_;

  while (entry) {
    if (ActiveTranx::compare(log_file_name, log_file_pos, entry->log_name_,
                             entry->log_pos_) <= 0)
      break;
    entry = entry->next_;
  }
  function_exit(kWho, 0);
  return entry;
}

int ActiveTranx::clear_active_tranx_nodes(const char *log_file_name,
                                          my_off_t log_file_pos) {
  const char *kWho = "ActiveTranx::::clear_active_tranx_nodes";
  TranxNode *new_front;

  function_enter(kWho);

  if (log_file_name != NULL) {
    new_front = trx_front_;

    while (new_front) {
      if (compare(new_front, log_file_name, log_file_pos) > 0 ||
          new_front->n_waiters > 0)
        break;
      new_front = new_front->next_;
    }
  } else {
    /* If log_file_name is NULL, clear everything. */
    new_front = NULL;
  }

  if (new_front == NULL) {
    /* No active transaction nodes after the call. */

    /* Clear the hash table. */
    memset(trx_htb_, 0, num_entries_ * sizeof(TranxNode *));
    allocator_.free_all_nodes();

    /* Clear the active transaction list. */
    if (trx_front_ != NULL) {
      trx_front_ = NULL;
      trx_rear_ = NULL;
    }

    if (trace_level_ & kTraceDetail)
      LogErr(INFORMATION_LEVEL,
             ER_SEMISYNC_CLEARED_ALL_ACTIVE_TRANSACTION_NODES, kWho);
  } else if (new_front != trx_front_) {
    TranxNode *curr_node, *next_node;

    /* Delete all transaction nodes before the confirmation point. */
    int n_frees = 0;
    curr_node = trx_front_;
    while (curr_node != new_front) {
      next_node = curr_node->next_;
      n_frees++;

      /* Remove the node from the hash table. */
      unsigned int hash_val =
          get_hash_value(curr_node->log_name_, curr_node->log_pos_);
      TranxNode **hash_ptr = &(trx_htb_[hash_val]);
      while ((*hash_ptr) != NULL) {
        if ((*hash_ptr) == curr_node) {
          (*hash_ptr) = curr_node->hash_next_;
          break;
        }
        hash_ptr = &((*hash_ptr)->hash_next_);
      }

      curr_node = next_node;
    }

    trx_front_ = new_front;
    allocator_.free_nodes_before(trx_front_);

    if (trace_level_ & kTraceDetail)
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_CLEARED_ACTIVE_TRANSACTION_TILL_POS,
             kWho, n_frees, trx_front_->log_name_,
             (unsigned long)trx_front_->log_pos_);
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::reportReplyPacket(uint32 server_id, const uchar *packet,
                                          ulong packet_len) {
  const char *kWho = "ReplSemiSyncMaster::reportReplyPacket";
  int result = -1;
  char log_file_name[FN_REFLEN + 1];
  my_off_t log_file_pos;
  ulong log_file_len = 0;

  function_enter(kWho);

  if (unlikely(packet[REPLY_MAGIC_NUM_OFFSET] !=
               ReplSemiSyncMaster::kPacketMagicNum)) {
    LogErr(ERROR_LEVEL, ER_SEMISYNC_REPLY_MAGIC_NO_ERROR);
    goto l_end;
  }

  if (unlikely(packet_len < REPLY_BINLOG_NAME_OFFSET)) {
    LogErr(ERROR_LEVEL, ER_SEMISYNC_REPLY_PKT_LENGTH_TOO_SMALL);
    goto l_end;
  }

  log_file_pos = uint8korr(packet + REPLY_BINLOG_POS_OFFSET);
  log_file_len = packet_len - REPLY_BINLOG_NAME_OFFSET;
  if (unlikely(log_file_len >= FN_REFLEN)) {
    LogErr(ERROR_LEVEL, ER_SEMISYNC_REPLY_BINLOG_FILE_TOO_LARGE);
    goto l_end;
  }
  strncpy(log_file_name, (const char *)packet + REPLY_BINLOG_NAME_OFFSET,
          log_file_len);
  log_file_name[log_file_len] = 0;

  if (trace_level_ & kTraceDetail)
    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_SERVER_REPLY, kWho, log_file_name,
           (ulong)log_file_pos, server_id);

  handleAck(server_id, log_file_name, log_file_pos);

l_end:
  return function_exit(kWho, result);
}

/*******************************************************************************
 *
 * <ReplSemiSyncMaster> class: the basic code layer for sync-replication master.
 * <ReplSemiSyncSlave>  class: the basic code layer for sync-replication slave.
 *
 * The most important functions during semi-syn replication listed:
 *
 * Master:
 *  . reportReplyBinlog():  called by the binlog dump thread when it receives
 *                          the slave's status information.
 *  . updateSyncHeader():   based on transaction waiting information, decide
 *                          whether to request the slave to reply.
 *  . writeTranxInBinlog(): called by the transaction thread when it finishes
 *                          writing all transaction events in binlog.
 *  . commitTrx():          transaction thread wait for the slave reply.
 *
 * Slave:
 *  . slaveReadSyncHeader(): read the semi-sync header from the master, get the
 *                           sync status and get the payload for events.
 *  . slaveReply():          reply to the master about the replication progress.
 *
 ******************************************************************************/

ReplSemiSyncMaster::ReplSemiSyncMaster() {
  reply_file_name_[0] = '\0';
  wait_file_name_[0] = '\0';
  commit_file_name_[0] = '\0';
  // init the server_info_, the group_id will be set [1,10]
  server_info_.init();
}

int ReplSemiSyncMaster::initObject() {
  int result;
  const char *kWho = "ReplSemiSyncMaster::initObject";

  if (init_done_) {
    LogErr(WARNING_LEVEL, ER_SEMISYNC_FUNCTION_CALLED_TWICE, kWho);
    return 1;
  }
  init_done_ = true;
  
  set_master_serverid();
  set_wait_no_slave_inline(rpl_semi_sync_master_wait_no_slave);
  set_wait_slave_count_inline(rpl_semi_sync_master_wait_for_slave_count);

  /* References to the parameter works after set_options(). */
  setWaitTimeout(rpl_semi_sync_master_timeout);
  setTraceLevel(rpl_semi_sync_master_trace_level);

  /* Mutex initialization can only be done after MY_INIT(). */
  mysql_mutex_init(key_ss_mutex_LOCK_binlog_, &LOCK_binlog_,
                   MY_MUTEX_INIT_FAST);

  /*
    rpl_semi_sync_master_wait_for_slave_count may be set through mysqld option.
    So call setWaitSlaveCount to initialize the internal ack container.
  */
  if (setWaitSlaveCount(get_wait_slave_count_inline())) return 1;
  
  /* set the flag of ack_container_, this is needed before use it */
  ack_container_.set_container_flag(GROUPS_BETWEEN_CONTAINER);

  /* init the wait_cond_hwm with 1:0, wait_cond_lwm with 0:0 */
  if(!init_wait_cond())
    return 1;

  if(rpl_semi_sync_master_wait_cond_hwm &&
     strlen(rpl_semi_sync_master_wait_cond_hwm))
  {
    if(!set_wait_cond_hwm(rpl_semi_sync_master_wait_cond_hwm))
      return 1;
  }

  if(rpl_semi_sync_master_wait_cond_lwm &&
     strlen(rpl_semi_sync_master_wait_cond_lwm))
  {
    if(!set_wait_cond_lwm(rpl_semi_sync_master_wait_cond_lwm))
      return 1;
  }

  /* Parse all the group* string and set the group_info */  
  if(false == set_server_info())
    return 1;

  /* init the curr_stat_master_flag by rpl_with_master_stat_hwm */
  set_curr_stat_master_flag(rpl_with_master_stat_hwm);

  if (rpl_semi_sync_master_enabled)
    result = enableMaster();
  else
    result = disableMaster();

  return result;
}

int ReplSemiSyncMaster::deinit()
{
  g_quick_sync_err_flag.store(0);
  g_is_quick_sync_enabled.store(0);
  
  if (init_done_)
  {
    mysql_mutex_destroy(&LOCK_binlog_);
    init_done_ = false;
  }
  if (active_tranxs_)
  {
    delete active_tranxs_;
    active_tranxs_ = NULL;
  }

  return 0;
}

int ReplSemiSyncMaster::enableMaster() {
  int result = 0;

  /* Must have the lock when we do enable of disable. */
  lock();

  if (!getMasterEnabled()) {
    if (active_tranxs_ == NULL)
      active_tranxs_ = new ActiveTranx(&LOCK_binlog_, trace_level_);

    if (active_tranxs_ != NULL) {
      commit_file_name_inited_ = false;
      reply_file_name_inited_ = false;
      wait_file_name_inited_ = false;

      set_master_enabled(true);

      if(rpl_semi_sync_master_quick_sync_enabled)
      {
        if(get_wait_no_slave_inline())
          set_current_wait_group_count(get_actually_wait_count(rpl_wait_count_hwm));
        else
          proc_active_groups_changed();
        state_ = true;
        sql_print_information("Quick-sync replication enabled on the master.");
      }
      else
      {
        const AckInfo *ackinfo= NULL;
        /* reset the ack_container size by wait_slave_count */
        if(ack_container_.resize(get_wait_slave_count_inline(), &ackinfo))
        {
          sql_print_error("ack_container_ resize failed with wait_for_slave_count.");
          result = -1;
          goto l_end;
        }
        
        /*
          state_ will be set off when users don't want to wait(
          wait_no_slave_ == 0) if there is no enough active
          semisync clients
        */
        state_ = (get_wait_no_slave_inline() != 0 ||
                  (rpl_semi_sync_master_clients >=
                   get_wait_slave_count_inline()));
        LogErr(INFORMATION_LEVEL, ER_SEMISYNC_RPL_ENABLED_ON_MASTER);
      }
      rpl_semi_sync_master_status = state_;
      
    } else {
      LogErr(ERROR_LEVEL, ER_SEMISYNC_MASTER_OOM);
      result = -1;
    }
  }

l_end:
  unlock();
  return result;
}

int ReplSemiSyncMaster::disableMaster() {
  /* Must have the lock when we do enable of disable. */
  lock();

  if (getMasterEnabled()) {
    set_err_flag_and_stat_flag(INIT_ERR_FLAG);
    
    /* Switch off the semi-sync first so that waiting transaction will be
     * waken up.
     */
    switch_off();

    if (active_tranxs_ && active_tranxs_->is_empty()) {
      delete active_tranxs_;
      active_tranxs_ = NULL;
    }

    reply_file_name_inited_ = false;
    wait_file_name_inited_ = false;
    commit_file_name_inited_ = false;

    ack_container_.clear();
    for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
      ack_group_container_[i].clear();
    lwm_master_group_ack_container_.clear();
 
    server_info_.init_all_group_status();
    
    server_info_.set_current_wait_groups(0);

    set_master_enabled(false);
    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_DISABLED_ON_MASTER);
  }

  unlock();

  return 0;
}

ReplSemiSyncMaster::~ReplSemiSyncMaster() {
  // if (init_done_) {
  //   mysql_mutex_destroy(&LOCK_binlog_);
  // }

  // delete active_tranxs_;
}

void ReplSemiSyncMaster::lock() { mysql_mutex_lock(&LOCK_binlog_); }

void ReplSemiSyncMaster::unlock() { mysql_mutex_unlock(&LOCK_binlog_); }

void ReplSemiSyncMaster::add_slave(int server_id) {
  lock();
  rpl_semi_sync_master_clients++;
  
  server_info_.add_slave_server((unsigned int)server_id);

  if (rpl_semi_sync_master_quick_sync_enabled && getMasterEnabled())
  {
    if(!is_need_wait_no_slave())
      proc_active_groups_changed();
  }
  
  unlock();
}

void ReplSemiSyncMaster::remove_slave(int server_id) {
  lock();
  rpl_semi_sync_master_clients--;
  
  server_info_.remove_slave_server((unsigned int)server_id);
  
  if(rpl_semi_sync_master_quick_sync_enabled)
  {
    if(getMasterEnabled())
    {
      if(!is_need_wait_no_slave() || connection_events_loop_aborted())
      {
        proc_active_groups_changed();
      }
    }
  }
  else
  {
    /* Only switch off if semi-sync is enabled and is on */
    if (getMasterEnabled() && is_on()) {
      /*
        If user has chosen not to wait if no enough semi-sync slave available
        and after a slave exists, turn off semi-semi master immediately if active
        slaves are less then required slave numbers.
      */
      if ((rpl_semi_sync_master_clients ==
           get_wait_slave_count_inline() - 1) &&
          (!get_wait_no_slave_inline() ||
           connection_events_loop_aborted())) {
        if (connection_events_loop_aborted()) {
          if (commit_file_name_inited_ && reply_file_name_inited_) {
            int cmp = ActiveTranx::compare(reply_file_name_, reply_file_pos_,
                                           commit_file_name_, commit_file_pos_);
            if (cmp < 0) LogErr(WARNING_LEVEL, ER_SEMISYNC_FORCED_SHUTDOWN);
          }
        }
        switch_off();
      }
    }
  }
  
  unlock();
}

bool ReplSemiSyncMaster::is_semi_sync_slave() {
  int null_value;
  long long val = 0;
  get_user_var_int("rpl_semi_sync_slave", &val, &null_value);
  return val;
}

void ReplSemiSyncMaster::reportReplyBinlog(const char *log_file_name,
                                           my_off_t log_file_pos) {
  const char *kWho = "ReplSemiSyncMaster::reportReplyBinlog";
  int cmp;
  bool can_release_threads = false;
  bool need_copy_send_pos = true;

  function_enter(kWho);
  mysql_mutex_assert_owner(&LOCK_binlog_);

  if (!getMasterEnabled()) goto l_end;

  if (!rpl_semi_sync_master_quick_sync_enabled && !is_on()) 
    /* We check to see whether we can switch semi-sync ON. */
    try_switch_on(log_file_name, log_file_pos);

  /* The position should increase monotonically, if there is only one
   * thread sending the binlog to the slave.
   * In reality, to improve the transaction availability, we allow multiple
   * sync replication slaves.  So, if any one of them get the transaction,
   * the transaction session in the primary can move forward.
   */
  if (reply_file_name_inited_) {
    cmp = ActiveTranx::compare(log_file_name, log_file_pos, reply_file_name_,
                               reply_file_pos_);

    /* If the requested position is behind the sending binlog position,
     * would not adjust sending binlog position.
     * We based on the assumption that there are multiple semi-sync slave,
     * and at least one of them shou/ld be up to date.
     * If all semi-sync slaves are behind, at least initially, the primary
     * can find the situation after the waiting timeout.  After that, some
     * slaves should catch up quickly.
     */
    if (cmp < 0) {
      /* If the position is behind, do not copy it. */
      need_copy_send_pos = false;
    }
  }

  if (need_copy_send_pos) {
    strncpy(reply_file_name_, log_file_name, sizeof(reply_file_name_) - 1);
    reply_file_name_[sizeof(reply_file_name_) - 1] = '\0';
    reply_file_pos_ = log_file_pos;
    reply_file_name_inited_ = true;

    if (trace_level_ & kTraceDetail)
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MASTER_GOT_REPLY_AT_POS, kWho,
             log_file_name, (unsigned long)log_file_pos);
  }

  if (rpl_semi_sync_master_wait_sessions > 0) {
    /* Let us check if some of the waiting threads doing a trx
     * commit can now proceed.
     */
    cmp = ActiveTranx::compare(reply_file_name_, reply_file_pos_,
                               wait_file_name_, wait_file_pos_);
    if (cmp >= 0) {
      /* Yes, at least one waiting thread can now proceed:
       * let us release all waiting threads with a broadcast
       */
      can_release_threads = true;
      wait_file_name_inited_ = false;
    }
  }

l_end:

  if (can_release_threads) {
    if (trace_level_ & kTraceDetail)
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MASTER_SIGNAL_ALL_WAITING_THREADS,
             kWho);
    active_tranxs_->signal_waiting_sessions_up_to(reply_file_name_,
                                                  reply_file_pos_);
  }

  function_exit(kWho, 0);
}

int ReplSemiSyncMaster::commitTrx(const char *trx_wait_binlog_name,
                                  my_off_t trx_wait_binlog_pos) {
  const char *kWho = "ReplSemiSyncMaster::commitTrx";

  function_enter(kWho);
  PSI_stage_info old_stage;

#if defined(ENABLED_DEBUG_SYNC)
  /* debug sync may not be initialized for a master */
  if (current_thd->debug_sync_control)
    DEBUG_SYNC(current_thd, "rpl_semisync_master_commit_trx_before_lock");
#endif
  /* Acquire the mutex. */
  lock();

  TranxNode *entry = NULL;
  mysql_cond_t *thd_cond = NULL;
  bool is_semi_sync_trans = true;
  if (active_tranxs_ != NULL && trx_wait_binlog_name) {
    entry = active_tranxs_->find_active_tranx_node(trx_wait_binlog_name,
                                                   trx_wait_binlog_pos);
    if (entry) thd_cond = &entry->cond;
  }
  /* This must be called after acquired the lock */
  THD_ENTER_COND(NULL, thd_cond, &LOCK_binlog_,
                 &stage_waiting_for_semi_sync_ack_from_slave, &old_stage);

  if (getMasterEnabled() && trx_wait_binlog_name) {
    struct timespec start_ts;
    struct timespec abstime;
    int wait_result;

    set_timespec(&start_ts, 0);
    /* This is the real check inside the mutex. */
    if (!getMasterEnabled() || !is_on()) goto l_end;

    if (trace_level_ & kTraceDetail) {
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MASTER_TRX_WAIT_POS, kWho,
             trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos,
             (int)is_on());
    }

    /* Calcuate the waiting period. */
    abstime.tv_sec = start_ts.tv_sec + wait_timeout_ / TIME_THOUSAND;
    abstime.tv_nsec =
        start_ts.tv_nsec + (wait_timeout_ % TIME_THOUSAND) * TIME_MILLION;
    if (abstime.tv_nsec >= TIME_BILLION) {
      abstime.tv_sec++;
      abstime.tv_nsec -= TIME_BILLION;
    }

    while (is_on()) {
      if (reply_file_name_inited_) {
        int cmp =
            ActiveTranx::compare(reply_file_name_, reply_file_pos_,
                                 trx_wait_binlog_name, trx_wait_binlog_pos);
        if (cmp >= 0) {
          /* We have already sent the relevant binlog to the slave: no need to
           * wait here.
           */
          if (trace_level_ & kTraceDetail)
            LogErr(INFORMATION_LEVEL, ER_SEMISYNC_BINLOG_REPLY_IS_AHEAD, kWho,
                   reply_file_name_, (unsigned long)reply_file_pos_);
          break;
        }
      }
      /*
        When code reaches here an Entry object may not be present in the
        following scenario.

        Semi sync was not enabled when transaction entered into ordered_commit
        process. During flush stage, semi sync was not enabled and there was no
        'Entry' object created for the transaction being committed and at a
        later stage it was enabled. In this case trx_wait_binlog_name and
        trx_wait_binlog_pos are set but the 'Entry' object is not present. Hence
        dump thread will not wait for reply from slave and it will not update
        reply_file_name. In such case the committing transaction should not wait
        for an ack from slave and it should be considered as an async
        transaction.
      */
      if (!entry) {
        is_semi_sync_trans = false;
        goto l_end;
      }

      /* Let us update the info about the minimum binlog position of waiting
       * threads.
       */
      if (wait_file_name_inited_) {
        int cmp =
            ActiveTranx::compare(trx_wait_binlog_name, trx_wait_binlog_pos,
                                 wait_file_name_, wait_file_pos_);
        if (cmp <= 0) {
          /* This thd has a lower position, let's update the minimum info. */
          strncpy(wait_file_name_, trx_wait_binlog_name,
                  sizeof(wait_file_name_) - 1);
          wait_file_name_[sizeof(wait_file_name_) - 1] = '\0';
          wait_file_pos_ = trx_wait_binlog_pos;

          rpl_semi_sync_master_wait_pos_backtraverse++;
          if (trace_level_ & kTraceDetail)
            LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MOVE_BACK_WAIT_POS, kWho,
                   wait_file_name_, (unsigned long)wait_file_pos_);
        }
      } else {
        strncpy(wait_file_name_, trx_wait_binlog_name,
                sizeof(wait_file_name_) - 1);
        wait_file_name_[sizeof(wait_file_name_) - 1] = '\0';
        wait_file_pos_ = trx_wait_binlog_pos;
        wait_file_name_inited_ = true;

        if (trace_level_ & kTraceDetail)
          LogErr(INFORMATION_LEVEL, ER_SEMISYNC_INIT_WAIT_POS, kWho,
                 wait_file_name_, (unsigned long)wait_file_pos_);
      }

      /* In semi-synchronous replication, we wait until the binlog-dump
       * thread has received the reply on the relevant binlog segment from the
       * replication slave.
       *
       * Let us suspend this thread to wait on the condition;
       * when replication has progressed far enough, we will release
       * these waiting threads.
       */
      if (connection_events_loop_aborted() &&
          (rpl_semi_sync_master_clients ==
           get_wait_slave_count_inline() - 1) &&
          is_on()) {
        LogErr(WARNING_LEVEL, ER_SEMISYNC_FORCED_SHUTDOWN);
        switch_off();
        break;
      }

      rpl_semi_sync_master_wait_sessions++;

      if (trace_level_ & kTraceDetail)
        LogErr(INFORMATION_LEVEL, ER_SEMISYNC_WAIT_TIME_FOR_BINLOG_SENT, kWho,
               wait_timeout_, wait_file_name_, (unsigned long)wait_file_pos_);

      /* wait for the position to be ACK'ed back */
      assert(entry);
      entry->n_waiters++;
      wait_result = mysql_cond_timedwait(&entry->cond, &LOCK_binlog_, &abstime);
      entry->n_waiters--;
      /*
        After we release LOCK_binlog_ above while waiting for the condition,
        it can happen that some other parallel client session executed
        RESET MASTER. That can set rpl_semi_sync_master_wait_sessions to zero.
        Hence check the value before decrementing it and decrement it only if it
        is non-zero value.
      */
      if (rpl_semi_sync_master_wait_sessions > 0)
        rpl_semi_sync_master_wait_sessions--;

      if (wait_result != 0) {
        /* This is a real wait timeout. */
        LogErr(WARNING_LEVEL, ER_SEMISYNC_WAIT_FOR_BINLOG_TIMEDOUT,
               trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos,
               reply_file_name_, (unsigned long)reply_file_pos_);
        rpl_semi_sync_master_wait_timeouts++;

        /* switch semi-sync off */
        switch_off();
      } else {
        int wait_time;

        wait_time = getWaitTime(start_ts);
        if (wait_time < 0) {
          if (trace_level_ & kTraceGeneral) {
            LogErr(INFORMATION_LEVEL,
                   ER_SEMISYNC_WAIT_TIME_ASSESSMENT_FOR_COMMIT_TRX_FAILED,
                   trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos);
          }
          rpl_semi_sync_master_timefunc_fails++;
        } else {
          rpl_semi_sync_master_trx_wait_num++;
          rpl_semi_sync_master_trx_wait_time += wait_time;
        }
      }
    }

  l_end:
    /* Update the status counter. */
    if (is_on() && is_semi_sync_trans)
      rpl_semi_sync_master_yes_transactions++;
    else
      rpl_semi_sync_master_no_transactions++;
  }

  /* Last waiter removes the TranxNode */
  if (trx_wait_binlog_name && active_tranxs_ && entry && entry->n_waiters == 0)
    active_tranxs_->clear_active_tranx_nodes(trx_wait_binlog_name,
                                             trx_wait_binlog_pos);

  unlock();
  THD_EXIT_COND(NULL, &old_stage);
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::quick_sync_commitTrx(THD  *thd,
                                             const char* trx_wait_binlog_name,
                                             my_off_t trx_wait_binlog_pos)
{
  const char *kWho = "ReplSemiSyncMaster::quick_sync_commitTrx";
  function_enter(kWho);
  /* Acquire the mutex. */
  lock();

  bool need_commit = true;
  TranxNode* entry= NULL;
  if (active_tranxs_ != NULL && trx_wait_binlog_name)
  {
    entry = active_tranxs_->find_active_tranx_node(trx_wait_binlog_name,
                                                   trx_wait_binlog_pos);
    if (entry)
    {
      // first leader use this tranxnode, set time
      if (!add_thd_to_tranx_node(entry, thd))
        set_trans_start_and_end_time(entry,wait_timeout_); 
      
      /* If the thd is add to a tranxnode, need add the entry->n_waiters. */
      rpl_semi_sync_master_wait_sessions++;
      entry->n_waiters++;
    }
  }

  if (getMasterEnabled() && trx_wait_binlog_name)
  {
    /* This is the real check inside the mutex. */
    if (!getMasterEnabled() || !is_on())
      goto l_end;

    if(GROUP_BELOW_LWM_ALARM_ERR == rpl_semi_sync_master_err_flag)
    {
      set_quick_sync_flag(thd, rpl_semi_sync_master_err_flag);
      if(trace_level_ & kTraceDetail)
        sql_print_information("in quick sync commitTrx, the active groups are less than lwm.");
      goto l_end;
    }

    /* the current_wait_groups is 0, do not need wait. */
    if(0 == rpl_semi_sync_master_current_wait_groups)
      goto l_end;

    if (trace_level_ & kTraceDetail)
    {
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MASTER_TRX_WAIT_POS, kWho,
             trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos,
             (int)is_on());
    }
  
    if (reply_file_name_inited_)
    {
      int cmp = ActiveTranx::compare(reply_file_name_, reply_file_pos_,
                                     trx_wait_binlog_name, trx_wait_binlog_pos);
      if (cmp >= 0)
      {
        if (trace_level_ & kTraceDetail)
          LogErr(INFORMATION_LEVEL, ER_SEMISYNC_BINLOG_REPLY_IS_AHEAD, kWho,
                 reply_file_name_, (unsigned long)reply_file_pos_);
        goto l_end;
      }
    }

    if (!entry)
    {
      goto l_end;
    }
  
    /* Let us update the info about the minimum binlog position of waiting
     * threads.
     */
    if (wait_file_name_inited_)
    {
      int cmp = ActiveTranx::compare(trx_wait_binlog_name, trx_wait_binlog_pos,
                                                     wait_file_name_, wait_file_pos_);
      if (cmp <= 0)
      {
        /* This thd has a lower position, let's update the minimum info. */
        strncpy(wait_file_name_, trx_wait_binlog_name, sizeof(wait_file_name_) - 1);
        wait_file_name_[sizeof(wait_file_name_) - 1]= '\0';
        wait_file_pos_ = trx_wait_binlog_pos;
  
        rpl_semi_sync_master_wait_pos_backtraverse++;
        if (trace_level_ & kTraceDetail)
          LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MOVE_BACK_WAIT_POS, kWho,
                 wait_file_name_, (unsigned long)wait_file_pos_);
      }
    }
    else
    {
      strncpy(wait_file_name_, trx_wait_binlog_name, sizeof(wait_file_name_) - 1);
      wait_file_name_[sizeof(wait_file_name_) - 1]= '\0';
      wait_file_pos_ = trx_wait_binlog_pos;
      wait_file_name_inited_ = true;
  
      if (trace_level_ & kTraceDetail)
        LogErr(INFORMATION_LEVEL, ER_SEMISYNC_INIT_WAIT_POS, kWho,
                 wait_file_name_, (unsigned long)wait_file_pos_);
    }

    need_commit = false;
    
    if (trace_level_ & kTraceDetail)
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_WAIT_TIME_FOR_BINLOG_SENT, kWho,
             wait_timeout_, wait_file_name_, (unsigned long)wait_file_pos_);
  
  }

l_end:
  if (need_commit)
  {
    if (entry)
    {
      active_tranxs_->signal_waiting_sessions_up_to(entry->log_name_, entry->log_pos_);
    }
    else
    {
      /**
         there is only two case: switch ON to OFF, switch OFF to ON;
         1:in writeTranxInBinlog the state_ is OFF, and then in commitTrx the state_ is ON.
         2:quick_sync go in this func but has been switch to off;
         explain:in enabled=off mode, use the commitTrx instead of quick_sync_commitTrx.
      */
      threadpool_append_commit_to_high_prio_queue(thd);
      rpl_semi_sync_master_no_transactions++;
      
      if(trace_level_ & kTraceDetail)
        sql_print_information("%s: add the thd[%p] to high_prio_queue without valid tranxnode", kWho, thd);
    }
    
    if (trace_level_ & kTraceDetail)
      sql_print_information("%s: the trx (%s, %lu) need commit in quick_sync_commitTrx, TranxNode is[%p]", 
          kWho, trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos, entry);
  }
  else
  {
    active_tranxs_->release_unused_node();
  }
   
  unlock();
  return function_exit(kWho, 0); 
}

int ReplSemiSyncMaster::quick_sync_wait_commitTrx(THD *thd, const char* trx_wait_binlog_name,
                    my_off_t trx_wait_binlog_pos)
{
  const char *kWho = "ReplSemiSyncMaster::quick_sync_wait_commitTrx";

  function_enter(kWho);
  PSI_stage_info old_stage;

#if defined(ENABLED_DEBUG_SYNC)
  /* debug sync may not be initialized for a master */
  if (current_thd->debug_sync_control)
    DEBUG_SYNC(current_thd, "rpl_semisync_master_commit_trx_before_lock");
#endif
  /* Acquire the mutex. */
  lock();

  TranxNode* entry= NULL;
  mysql_cond_t* thd_cond= NULL;
  bool is_semi_sync_trans= true;
  if (active_tranxs_ != NULL && trx_wait_binlog_name)
  {
    entry=
      active_tranxs_->find_active_tranx_node(trx_wait_binlog_name,
                                             trx_wait_binlog_pos);
    if (entry)
      thd_cond= &entry->cond;
  }
  /* This must be called after acquired the lock */
  THD_ENTER_COND(NULL, thd_cond, &LOCK_binlog_,
                 & stage_waiting_for_semi_sync_ack_from_slave,
                 & old_stage);

  if (getMasterEnabled() && trx_wait_binlog_name)
  {
    struct timespec start_ts;

    set_timespec(&start_ts, 0);
    /* This is the real check inside the mutex. */
    if (!getMasterEnabled())
      goto l_end;

    /* less than lwm error. */
    if(GROUP_BELOW_LWM_ALARM_ERR == rpl_semi_sync_master_err_flag)
    {
      is_semi_sync_trans = false;
      set_quick_sync_flag(thd, rpl_semi_sync_master_err_flag);
      if(trace_level_ & kTraceDetail)
        sql_print_information("Before wait ack, the active groups are less than lwm.");
      goto l_end;
    }

    /* the current_wait_groups is 0 but the err_flag is not less then LWM, do not need wait. */
    if(0 == rpl_semi_sync_master_current_wait_groups)
    {
      is_semi_sync_trans = false;
      goto l_end;
    }

    if (trace_level_ & kTraceDetail)
    {
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MASTER_TRX_WAIT_POS, kWho,
             trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos,
             (int)is_on());
    }

    {
      if (reply_file_name_inited_)
      {
        int cmp = ActiveTranx::compare(reply_file_name_, reply_file_pos_,
                                       trx_wait_binlog_name, trx_wait_binlog_pos);
        if (cmp >= 0)
        {
          /* We have already sent the relevant binlog to the slave: no need to
           * wait here.
           */
          if (trace_level_ & kTraceDetail)
            LogErr(INFORMATION_LEVEL, ER_SEMISYNC_BINLOG_REPLY_IS_AHEAD, kWho,
                 reply_file_name_, (unsigned long)reply_file_pos_);
          goto l_end;
        }
      }

      if (!entry)
      {
        is_semi_sync_trans= false;
        goto l_end;
      }
      else
      {
        // first leader use this tranxnode, set time
        if (!add_thd_to_tranx_node(entry, thd))
          set_trans_start_and_end_time(entry,wait_timeout_);
      }

      /* Let us update the info about the minimum binlog position of waiting
       * threads.
       */
      if (wait_file_name_inited_)
      {
        int cmp = ActiveTranx::compare(trx_wait_binlog_name, trx_wait_binlog_pos,
                                       wait_file_name_, wait_file_pos_);
        if (cmp <= 0)
        {
          /* This thd has a lower position, let's update the minimum info. */
          strncpy(wait_file_name_, trx_wait_binlog_name, sizeof(wait_file_name_) - 1);
          wait_file_name_[sizeof(wait_file_name_) - 1]= '\0';
          wait_file_pos_ = trx_wait_binlog_pos;

          rpl_semi_sync_master_wait_pos_backtraverse++;
          if (trace_level_ & kTraceDetail)
            LogErr(INFORMATION_LEVEL, ER_SEMISYNC_MOVE_BACK_WAIT_POS, kWho,
                 wait_file_name_, (unsigned long)wait_file_pos_);
        }
      }
      else
      {
        strncpy(wait_file_name_, trx_wait_binlog_name, sizeof(wait_file_name_) - 1);
        wait_file_name_[sizeof(wait_file_name_) - 1]= '\0';
        wait_file_pos_ = trx_wait_binlog_pos;
        wait_file_name_inited_ = true;

        if (trace_level_ & kTraceDetail)
          LogErr(INFORMATION_LEVEL, ER_SEMISYNC_INIT_WAIT_POS, kWho,
                 wait_file_name_, (unsigned long)wait_file_pos_);
      }

      rpl_semi_sync_master_wait_sessions++;
      
      if (trace_level_ & kTraceDetail)
        LogErr(INFORMATION_LEVEL, ER_SEMISYNC_WAIT_TIME_FOR_BINLOG_SENT, kWho,
               wait_timeout_, wait_file_name_, (unsigned long)wait_file_pos_);
      
      /* wait for the position to be ACK'ed back without timeout in quicksync */
      assert(entry);
      entry->n_waiters++;
      mysql_cond_wait(&entry->cond, &LOCK_binlog_);
      entry->n_waiters--;
      rpl_semi_sync_master_wait_sessions--;
      
      int wait_time;        
      wait_time = getWaitTime(start_ts);
      if (wait_time < 0)
      {
        if (trace_level_ & kTraceGeneral)
        {
          LogErr(INFORMATION_LEVEL,
                   ER_SEMISYNC_WAIT_TIME_ASSESSMENT_FOR_COMMIT_TRX_FAILED,
                   trx_wait_binlog_name, (unsigned long)trx_wait_binlog_pos);
        }
        rpl_semi_sync_master_timefunc_fails++;
      }
      else
      {
        rpl_semi_sync_master_trx_wait_num++;
        rpl_semi_sync_master_trx_wait_time += wait_time;
      }

     /*
       if quicksync turn to abnormal, need to return error,
       after mysql_cond_wait need check the error_flag
     */
      if(entry->error_flag)
      {
        is_semi_sync_trans = false;
        goto l_end;
      }
    }

l_end:
    /* Update the status counter. */
    if (is_semi_sync_trans)
      rpl_semi_sync_master_yes_transactions++;
    else
      rpl_semi_sync_master_no_transactions++;

  }

  /* Last waiter removes the TranxNode */
  if (trx_wait_binlog_name && active_tranxs_
      && entry && entry->n_waiters == 0)
    active_tranxs_->clear_active_tranx_nodes(trx_wait_binlog_name,
                                             trx_wait_binlog_pos);

  unlock();
  THD_EXIT_COND(NULL, & old_stage);
  return function_exit(kWho, 0);
}



void ReplSemiSyncMaster::set_wait_no_slave(char val) {
  lock();
  char set_switch = val;

  if(get_wait_no_slave_inline() == set_switch)
    goto end;

  set_wait_no_slave_inline(set_switch);
  if(rpl_semi_sync_master_quick_sync_enabled)
  {
    /* quick-sync mode */
    if(getMasterEnabled())
    {
      if(set_switch == 0)
      {
        if(rpl_semi_sync_master_err_flag == GROUP_NORMAL_OK &&
           rpl_semi_sync_master_active_groups == 0)
        {
          proc_active_groups_changed();
        }
      }
      else
      {
        if(rpl_semi_sync_master_err_flag != GROUP_NORMAL_OK)
        {
          set_err_flag_and_stat_flag(INIT_ERR_FLAG);
          set_current_wait_group_count(get_actually_wait_count(rpl_wait_count_hwm));
        }
      }
    }
  }
  else
  {
    /* semi-sync mode */
    if (set_switch == 0) {
      if ((rpl_semi_sync_master_clients == 0) && (is_on())) switch_off();
    } else {
      if (!is_on() && getMasterEnabled()) force_switch_on();
    }
  }
  
end:  
  unlock();
}

void ReplSemiSyncMaster::force_switch_on() { state_ = true; }

/* Indicate that semi-sync replication is OFF now.
 *
 * What should we do when it is disabled?  The problem is that we want
 * the semi-sync replication enabled again when the slave catches up
 * later.  But, it is not that easy to detect that the slave has caught
 * up.  This is caused by the fact that MySQL's replication protocol is
 * asynchronous, meaning that if the master does not use the semi-sync
 * protocol, the slave would not send anything to the master.
 * Still, if the master is sending (N+1)-th event, we assume that it is
 * an indicator that the slave has received N-th event and earlier ones.
 *
 * If semi-sync is disabled, all transactions still update the wait
 * position with the last position in binlog.  But no transactions will
 * wait for confirmations maintained.  In binlog dump thread,
 * updateSyncHeader() checks whether the current sending event catches
 * up with last wait position.  If it does match, semi-sync will be
 * switched on again.
 */
int ReplSemiSyncMaster::switch_off() {
  const char *kWho = "ReplSemiSyncMaster::switch_off";

  function_enter(kWho);
  state_ = false;
  rpl_semi_sync_master_status = state_;

  rpl_semi_sync_master_off_times++;
  wait_file_name_inited_ = false;
  reply_file_name_inited_ = false;
  LogErr(INFORMATION_LEVEL, ER_SEMISYNC_RPL_SWITCHED_OFF);

  /* signal waiting sessions */
  active_tranxs_->signal_waiting_sessions_all();

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::try_switch_on(const char *log_file_name,
                                      my_off_t log_file_pos) {
  const char *kWho = "ReplSemiSyncMaster::try_switch_on";
  bool semi_sync_on = false;

  function_enter(kWho);

  /* If the current sending event's position is larger than or equal to the
   * 'largest' commit transaction binlog position, the slave is already
   * catching up now and we can switch semi-sync on here.
   * If commit_file_name_inited_ indicates there are no recent transactions,
   * we can enable semi-sync immediately.
   */
  if (commit_file_name_inited_) {
    int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                   commit_file_name_, commit_file_pos_);
    semi_sync_on = (cmp >= 0);
  } else {
    semi_sync_on = true;
  }

  if (semi_sync_on) {
    /* Switch semi-sync replication on. */
    state_ = true;

    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_RPL_SWITCHED_ON, log_file_name,
           (unsigned long)log_file_pos);
  }

  return function_exit(kWho, 0);
}

bool ReplSemiSyncMaster::try_switch_group_active(unsigned int groupid,
                                                         const char *log_file_name,
                                                         my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::try_switch_group_active";
  function_enter(kWho);
  bool group_active = false;

  /* If the current sending event's position is larger than or equal to the
   * 'largest' commit transaction binlog position, the slave is already
   * catching up now and we can switch to active here.
   * If commit_file_name_inited_ indicates there are no recent transactions,
   * we can switch to active immediately.
   */
  if (commit_file_name_inited_)
  {
    int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                   commit_file_name_, commit_file_pos_);
    group_active = (cmp >= 0);
  }
  else
  {
    group_active = true;
  }

  if (group_active)
  {
    server_info_.switch_timeout_to_active(groupid);

    if(!is_need_wait_no_slave())
      proc_active_groups_changed();

    if(server_info_.is_master_group(groupid))
      sql_print_information("master_group[%d] in hwm mode switch from TIMEOUT to ACTIVE at (%s, %lu)", 
             groupid, log_file_name, (unsigned long)log_file_pos);
    else
      sql_print_information("group[%d] switch from TIMEOUT to ACTIVE at (%s, %lu)", 
             groupid, log_file_name, (unsigned long)log_file_pos);
    
    return function_exit(kWho, true);
  }

  return function_exit(kWho, false);
}

bool ReplSemiSyncMaster::try_switch_lwm_master_group_active(unsigned int groupid,
                                                         const char *log_file_name,
                                                         my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::try_switch_lwm_master_group_active";
  function_enter(kWho);
  bool group_active = false;

  /* If the current sending event's position is larger than or equal to the
   * 'largest' commit transaction binlog position, the slave is already
   * catching up now and we can switch to active here.
   * If commit_file_name_inited_ indicates there are no recent transactions,
   * we can switch to active immediately.
   */
  if (commit_file_name_inited_)
  {
    int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                   commit_file_name_, commit_file_pos_);
    group_active = (cmp >= 0);
  }
  else
  {
    group_active = true;
  }

  if (group_active)
  {
    server_info_.switch_lwm_master_group_active(groupid);

    if(!is_need_wait_no_slave())
      proc_active_groups_changed();
      
    sql_print_information("master_group[%d] in lwm mode switch from TIMEOUT to ACTIVE at (%s, %lu)", 
            groupid, log_file_name, (unsigned long)log_file_pos);
    
    return function_exit(kWho, true);
  }

  return function_exit(kWho, false);
}

int ReplSemiSyncMaster::reserveSyncHeader(unsigned char *header,
                                          unsigned long size) {
  const char *kWho = "ReplSemiSyncMaster::reserveSyncHeader";
  function_enter(kWho);

  int hlen = 0;
  {
    /* No enough space for the extra header, disable semi-sync master */
    if (sizeof(kSyncHeader) > size) {
      LogErr(WARNING_LEVEL, ER_SEMISYNC_NO_SPACE_IN_THE_PKT);
      disableMaster();
      return 0;
    }

    /* Set the magic number and the sync status.  By default, no sync
     * is required.
     */
    memcpy(header, kSyncHeader, sizeof(kSyncHeader));
    hlen = sizeof(kSyncHeader);
  }
  return function_exit(kWho, hlen);
}

int ReplSemiSyncMaster::updateSyncHeader(unsigned char *packet,
                                         const char *log_file_name,
                                         my_off_t log_file_pos,
                                         uint32 server_id) {
  const char *kWho = "ReplSemiSyncMaster::updateSyncHeader";
  int cmp = 0;
  bool sync = false;

  /* If the semi-sync master is not enabled, do not request replies from the
     slave.
   */
  if (!getMasterEnabled()) return 0;

  function_enter(kWho);

  lock();

  /* This is the real check inside the mutex. */
  if (!getMasterEnabled()) goto l_end;  // sync= false at this point in time

  if((rpl_semi_sync_master_quick_sync_enabled && !is_group_timeout(server_id)) ||
     (!rpl_semi_sync_master_quick_sync_enabled && is_on()))
  {
    /* semi-sync is ON */
    /* sync= false; No sync unless a transaction is involved. */

    if (reply_file_name_inited_) {
      cmp = ActiveTranx::compare(log_file_name, log_file_pos, reply_file_name_,
                                 reply_file_pos_);
      if (cmp <= 0) {
        /* If we have already got the reply for the event, then we do
         * not need to sync the transaction again.
         */
        goto l_end;
      }
    }

    if (wait_file_name_inited_) {
      cmp = ActiveTranx::compare(log_file_name, log_file_pos, wait_file_name_,
                                 wait_file_pos_);
    } else {
      cmp = 1;
    }

    /* If we are already waiting for some transaction replies which
     * are later in binlog, do not wait for this one event.
     */
    if (cmp >= 0) {
      /*
       * We only wait if the event is a transaction's ending event.
       */
      assert(active_tranxs_ != NULL);
      sync = active_tranxs_->is_tranx_end_pos(log_file_name, log_file_pos);
    }
  } else {
    if (commit_file_name_inited_) {
      int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                     commit_file_name_, commit_file_pos_);
      sync = (cmp >= 0);
    } else {
      sync = true;
    }
  }

  if (trace_level_ & kTraceDetail)
    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_SYNC_HEADER_UPDATE_INFO, kWho,
           server_id, log_file_name, (unsigned long)log_file_pos, sync,
           (int)is_on());

l_end:
  unlock();

  /* We do not need to clear sync flag because we set it to 0 when we
   * reserve the packet header.
   */
  if (sync) {
    (packet)[2] = kPacketFlagSync;
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::writeTranxInBinlog(const char *log_file_name,
                                           my_off_t log_file_pos) {
  const char *kWho = "ReplSemiSyncMaster::writeTranxInBinlog";
  int result = 0;

  function_enter(kWho);

  do
  {
    lock();
    
    result = 0;
    /* This is the real check inside the mutex. */
    if (!getMasterEnabled()) goto l_end;

    /* Update the 'largest' transaction commit position seen so far even
     * though semi-sync is switched off.
     * It is much better that we update commit_file_* here, instead of
     * inside commitTrx().  This is mostly because updateSyncHeader()
     * will watch for commit_file_* to decide whether to switch semi-sync
     * on. The detailed reason is explained in function updateSyncHeader().
     */
    if (commit_file_name_inited_) {
      int cmp = ActiveTranx::compare(log_file_name, log_file_pos,
                                     commit_file_name_, commit_file_pos_);
      if (cmp > 0) {
        /* This is a larger position, let's update the maximum info. */
        strncpy(commit_file_name_, log_file_name, FN_REFLEN - 1);
        commit_file_name_[FN_REFLEN - 1] = 0; /* make sure it ends properly */
        commit_file_pos_ = log_file_pos;
      }
    } else {
      strncpy(commit_file_name_, log_file_name, FN_REFLEN - 1);
      commit_file_name_[FN_REFLEN - 1] = 0; /* make sure it ends properly */
      commit_file_pos_ = log_file_pos;
      commit_file_name_inited_ = true;
    }
    
    if(rpl_semi_sync_master_quick_sync_enabled)
    {
      /* quick-sync mode */
      assert(active_tranxs_ != NULL);
      result = active_tranxs_->insert_tranx_node(log_file_name, log_file_pos);
      if(result)
      {
        sql_print_warning("Quick-sync failed to insert tranx_node for binlog file: %s, position: %lu",
                 log_file_name, (ulong)log_file_pos);
      }
    }
    else
    {
      /* semi-sync mode */
      if (is_on()) {
        assert(active_tranxs_ != NULL);
        if (active_tranxs_->insert_tranx_node(log_file_name, log_file_pos)) {
          /*
            if insert tranx_node failed, print a warning message
            and turn off semi-sync
          */
          LogErr(WARNING_LEVEL, ER_SEMISYNC_FAILED_TO_INSERT_TRX_NODE,
                 log_file_name, (ulong)log_file_pos);
          switch_off();
        }
      }
      result = 0;
    }

l_end:
  unlock();
  
    if(result)
    {
      my_sleep(10);
    }
  }while(result);

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::skipSlaveReply(const char *event_buf, uint32 server_id,
                                       const char *skipped_log_file,
                                       my_off_t skipped_log_pos) {
  const char *kWho = "ReplSemiSyncMaster::skipSlaveReply";

  function_enter(kWho);

  assert((unsigned char)event_buf[1] == kPacketMagicNum);
  if ((unsigned char)event_buf[2] != kPacketFlagSync) {
    /* current event would not require a reply anyway */
    goto l_end;
  }

  if (trace_level_ & kTraceDetail)
    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_TRX_SKIPPED_AT_POS, kWho,
           skipped_log_file, (unsigned long)skipped_log_pos);

  /* Treat skipped event as a received ack */
  handleAck(server_id, skipped_log_file, skipped_log_pos);

l_end:
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::readSlaveReply(NET *net, const char *event_buf) {
  const char *kWho = "ReplSemiSyncMaster::readSlaveReply";
  int result = -1;

  function_enter(kWho);

  assert((unsigned char)event_buf[1] == kPacketMagicNum);
  if ((unsigned char)event_buf[2] != kPacketFlagSync) {
    /* current event does not require reply */
    result = 0;
    goto l_end;
  }

  /* We flush to make sure that the current event is sent to the network,
   * instead of being buffered in the TCP/IP stack.
   */
  if (net_flush(net)) {
    LogErr(ERROR_LEVEL, ER_SEMISYNC_MASTER_FAILED_ON_NET_FLUSH);
    goto l_end;
  }

  net_clear(net, 0);
  net->pkt_nr++;
  result = 0;
  rpl_semi_sync_master_net_wait_num++;

l_end:
  return function_exit(kWho, result);
}

int ReplSemiSyncMaster::resetMaster() {
  const char *kWho = "ReplSemiSyncMaster::resetMaster";
  int result = 0;

  function_enter(kWho);

  lock();

  ack_container_.clear();
  for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
    ack_group_container_[i].clear();
  lwm_master_group_ack_container_.clear();

  wait_file_name_inited_ = false;
  reply_file_name_inited_ = false;
  commit_file_name_inited_ = false;

  rpl_semi_sync_master_yes_transactions = 0;
  rpl_semi_sync_master_no_transactions = 0;
  rpl_semi_sync_master_off_times = 0;
  rpl_semi_sync_master_timefunc_fails = 0;
  rpl_semi_sync_master_wait_sessions = 0;
  rpl_semi_sync_master_wait_pos_backtraverse = 0;
  rpl_semi_sync_master_trx_wait_num = 0;
  rpl_semi_sync_master_trx_wait_time = 0;
  rpl_semi_sync_master_net_wait_num = 0;
  rpl_semi_sync_master_net_wait_time = 0;

  unlock();

  return function_exit(kWho, result);
}

void ReplSemiSyncMaster::setExportStats() {
  lock();

  rpl_semi_sync_master_status = state_;
  rpl_semi_sync_master_avg_trx_wait_time =
      ((rpl_semi_sync_master_trx_wait_num)
           ? (unsigned long)((double)rpl_semi_sync_master_trx_wait_time /
                             ((double)rpl_semi_sync_master_trx_wait_num))
           : 0);
  rpl_semi_sync_master_avg_net_wait_time =
      ((rpl_semi_sync_master_net_wait_num)
           ? (unsigned long)((double)rpl_semi_sync_master_net_wait_time /
                             ((double)rpl_semi_sync_master_net_wait_num))
           : 0);

  unlock();
}

int ReplSemiSyncMaster::setWaitSlaveCount(unsigned int new_value) {
  const AckInfo *ackinfo = NULL;
  int result = 0;

  const char *kWho = "ReplSemiSyncMaster::updateWaitSlaves";
  function_enter(kWho);

  lock();

  result = ack_container_.resize(new_value, &ackinfo);
  if (result == 0) {
    set_wait_slave_count_inline(new_value);
    if (ackinfo != NULL)
      reportReplyBinlog(ackinfo->binlog_name, ackinfo->binlog_pos);
  }

  unlock();
  return function_exit(kWho, result);
}

int ReplSemiSyncMaster::set_quick_sync_enabled(char quick_sync_enabled)
{
  const char *kWho = "ReplSemiSyncMaster::set_quick_sync_enabled";
  function_enter(kWho);

  lock();

  if(getMasterEnabled())
  {
    sql_print_information("rpl_semi_sync_master_quick_sync_enabled only be set"
                          " on rpl_semi_sync_master_enabled=OFF status");
    unlock();
    return function_exit(kWho, 1);
  }

  rpl_semi_sync_master_quick_sync_enabled = quick_sync_enabled;
  unlock();
  return function_exit(kWho, 0);
}

bool ReplSemiSyncMaster::init_wait_cond()
{
  const char *kWho = "ReplSemiSyncMaster::init_wait_cond";
  function_enter(kWho);

  lock();

  if(!check_wait_cond(rpl_wait_count_hwm, rpl_with_master_stat_hwm,
                      rpl_wait_count_lwm, rpl_with_master_stat_lwm))
  {
    unlock();
    return function_exit(kWho, false);
  }
  
  unlock();
  return function_exit(kWho, true);
}

void ReplSemiSyncMaster::set_curr_stat_master_flag(char flag)
{
  const char *kWho = "ReplSemiSyncMaster::set_curr_stat_master_flag";
  function_enter(kWho);

  rpl_semi_sync_master_current_stat_master_flag = flag;
  return function_exit(kWho);
}

bool ReplSemiSyncMaster::set_wait_cond_hwm(char *strPos)
{
  unsigned int wait_group_count = 0;
  char with_master_group = 0;
  const char *kWho = "ReplSemiSyncMaster::set_wait_cond_hwm";
  function_enter(kWho);

  if(!parse_wait_cond(strPos, &wait_group_count, &with_master_group))
    return function_exit(kWho, false);

  lock();

  if(!check_wait_cond(wait_group_count, with_master_group,
                      rpl_wait_count_lwm, rpl_with_master_stat_lwm))
  {
    unlock();
    return function_exit(kWho, false);
  }

  if(set_wait_cond_hwm_val(wait_group_count, with_master_group))
  {
    unlock();
    return function_exit(kWho, false);
  }
  
  unlock();
  return function_exit(kWho, true);
}

bool ReplSemiSyncMaster::set_wait_cond_lwm(char *strPos)
{
  unsigned int wait_group_count_lwm = 0;
  char with_master_group_lwm = 0;
  const char *kWho = "ReplSemiSyncMaster::set_wait_cond_lwm";
  function_enter(kWho);

  if(!parse_wait_cond(strPos, &wait_group_count_lwm, &with_master_group_lwm))
    return function_exit(kWho, false);

  lock();

  if(!check_wait_cond(rpl_wait_count_hwm, rpl_with_master_stat_hwm,
                      wait_group_count_lwm, with_master_group_lwm))
  {
    unlock();
    return function_exit(kWho, false);
  }

  if(set_wait_cond_lwm_val(wait_group_count_lwm, with_master_group_lwm))
  {
    unlock();
    return function_exit(kWho, false);
  }
  
  unlock();
  return function_exit(kWho, true);
}

bool ReplSemiSyncMaster::parse_wait_cond(char *strPos, uint *wait_count, char *with_master)
{
  char *colon_pos = NULL;
  longlong tmp_wait_count = 0;
  longlong tmp_with_master = 0;
  const char *kWho = "ReplSemiSyncMaster::parse_wait_cond";
  function_enter(kWho);

  // when init or set NULL can enter this case.
  if(NULL == strPos || is_str_null(strPos))
  {
    if (trace_level_ & kTraceDetail)
      sql_print_information("Semi-sync master: get the wait_cond* string is NULL");
    return function_exit(kWho, true);
  }

  colon_pos = check_and_get_colon_pos(strPos);
  if(NULL == colon_pos)
    return function_exit(kWho, false);

  tmp_wait_count = get_number(strPos, colon_pos);
  if(-1 == tmp_wait_count || tmp_wait_count >10)
  {
    sql_print_information("Semi-sync master: parse wait_group_count error[%s].",strPos);
    return function_exit(kWho, false);
  }

  tmp_with_master = get_number(colon_pos+1, strPos+strlen(strPos));
  if(-1 == tmp_with_master || tmp_with_master>1)
  {
    sql_print_information("Semi-sync master: parse with_master_group error[%s].",colon_pos+1);
    return function_exit(kWho, false);
  }

  *wait_count = (uint)tmp_wait_count;
  *with_master = (char)tmp_with_master;

  return function_exit(kWho, true);
}

char *ReplSemiSyncMaster::check_and_get_colon_pos(char *strPos)
{
  char *pos = NULL;
  char *colon_pos = NULL;
  const char *kWho = "ReplSemiSyncMaster::check_and_get_colon_pos";
  function_enter(kWho);
  pos = strPos;
  while(*pos)
  {
    if(is_space_or_digital(*pos))
    {
      pos++;
    }
    else if((*pos == ':')&&(colon_pos == NULL))
    {
      colon_pos = pos;
      pos++;
    }
    else
    {
      sql_print_information("Semi-sync master: wait_cond parse error char[%s]", pos);
      function_exit(kWho, false);
      return NULL;
    }
  }

  if(NULL == colon_pos)
     sql_print_information("Semi-sync master: wait_cond str without colon ':'.");
  
  function_exit(kWho, true);
  return colon_pos;
}

bool ReplSemiSyncMaster::check_wait_cond(unsigned int wait_count_hwm,
                                                 char with_master_hwm,
                                                 unsigned int wait_count_lwm,
                                                 char with_master_lwm)
{
  bool result = true;
  const char *kWho = "ReplSemiSyncMaster::check_wait_cond";
  function_enter(kWho);

  if(0 == wait_count_lwm)
    return function_exit(kWho, result);

  if(wait_count_lwm > wait_count_hwm)
  {
    result = false;
    sql_print_information("quick-sync: wait_cond_lwm is bigger than wait_cond_hwm.");
  }
  else if(wait_count_lwm == wait_count_hwm)
  {
    if(1 == with_master_hwm &&
       0 == with_master_lwm)
    {
      result = false;
      sql_print_information("quick-sync: wait_cond_lwm is bigger than wait_cond_hwm.");
    }
  }

  return function_exit(kWho, result);
}

int ReplSemiSyncMaster::set_wait_cond_hwm_val(unsigned int wait_count,char with_master)
{
  const char *kWho = "ReplSemiSyncMaster::set_wait_cond_hwm_val";
  function_enter(kWho);

  rpl_wait_count_hwm       = wait_count;
  rpl_with_master_stat_hwm = with_master;

  set_master_group_container();

  if(getMasterEnabled() && is_on())
  {
    if(is_need_wait_no_slave())
    {
      set_err_flag_and_stat_flag(INIT_ERR_FLAG);
      set_current_wait_group_count(get_actually_wait_count(rpl_wait_count_hwm));
    }
    else
    {
      proc_active_groups_changed();
    }
  }
  else
  {
    set_err_flag_and_stat_flag(INIT_ERR_FLAG);
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::set_wait_cond_lwm_val(unsigned int wait_count,char with_master)
{
  const char *kWho = "ReplSemiSyncMaster::set_wait_cond_lwm_val";
  function_enter(kWho);

  rpl_wait_count_lwm  = wait_count;
  rpl_with_master_stat_lwm = with_master;

  set_master_group_container();

  if(getMasterEnabled() && is_on())
  {
    if(!is_need_wait_no_slave())
      proc_active_groups_changed();
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::set_without_master_group_container(unsigned int group_id,
                                                                          unsigned int wait_num)
{
  const AckInfo *ackinfo= NULL;
  const char *kWho = "ReplSemiSyncMaster::set_without_master_group_container";
  function_enter(kWho);

  /** 
    resize the ack_group_container_ and if is_full return an ack_info.
    insert the ack_info to ack_container_ and if is_full report the position.
  */
  if(wait_num)
  {
    while(ack_group_container_[group_id-1].resize(wait_num, &ackinfo))
    {
      sql_print_error("Malloc memory failed in ack_group_container_.resize, need free memory.");
      my_sleep(10);
    }

    if(ackinfo)
      insert_without_master_group(ackinfo->server_id, ackinfo->group_id,
                                  ackinfo->binlog_name, ackinfo->binlog_pos);
  }
  
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::set_master_group_container()
{
  const char *kWho = "ReplSemiSyncMaster::set_master_group_container";
  function_enter(kWho);
  unsigned int master_group_id = 0;
  
  master_group_id = server_info_.get_group_id_with_master();

  if(master_group_id)
  {
    server_info_.reset_master_group_hwm();
    server_info_.reset_master_group_lwm();
    set_hwm_master_group_container(master_group_id, server_info_.get_curr_wait_ack_num(master_group_id));
    set_lwm_master_group_container(server_info_.get_lwm_curr_wait_ack_num());
  }
  
  return function_exit(kWho, 0);  
}

int ReplSemiSyncMaster::set_hwm_master_group_container(unsigned int group_id,
                                                                       unsigned int wait_num)
{
  const AckInfo *ackinfo= NULL;
  const char *kWho = "ReplSemiSyncMaster::set_hwm_master_group_container";
  function_enter(kWho);

  /** 
    resize the ack_group_container_ and if is_full return an ack_info.
    insert the ack_info to ack_container_ and if is_full report the position.
  */
  if(wait_num)
  {
    while(ack_group_container_[group_id-1].resize(wait_num, &ackinfo))
    {
      sql_print_error("Malloc memory failed in ack_group_container_.resize, need free memory.");
      my_sleep(10);
    }

    if(ackinfo)
      insert_hwm_master_group(ackinfo->server_id, ackinfo->group_id,
                              ackinfo->binlog_name, ackinfo->binlog_pos);
  }
  
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::set_lwm_master_group_container(unsigned int wait_num)
{
  const AckInfo *ackinfo= NULL;
  const char *kWho = "ReplSemiSyncMaster::set_lwm_master_group_container";
  function_enter(kWho);

  /** 
    resize the lwm_master_group_ack_container_ and if is_full return an ack_info.
    insert the ack_info to ack_container_ and if is_full report the position.
  */
  if(wait_num)
  {
    while(lwm_master_group_ack_container_.resize(wait_num, &ackinfo))
    {
      sql_print_error("Malloc memory failed in lwm_master_group_ack_container_.resize, need free memory.");
      my_sleep(10);
    }
    
    if(ackinfo)
      insert_lwm_master_group(ackinfo->server_id, ackinfo->group_id,
                              ackinfo->binlog_name, ackinfo->binlog_pos);
  }
  
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::set_current_wait_group_count(unsigned int new_value)
{
  const AckInfo *ackinfo= NULL;

  const char *kWho = "ReplSemiSyncMaster::set_current_wait_group_count";
  function_enter(kWho);

  if(0 == new_value)
  {
    server_info_.set_current_wait_groups(new_value);
    active_tranxs_->signal_waiting_sessions_all();
    ack_container_.clear();
    return function_exit(kWho, 0);
  }

  while(ack_container_.resize(new_value, &ackinfo))
  {
    sql_print_error("Malloc memory failed in ack_container_.resize, need free memory");
    my_sleep(10);
  }

  server_info_.set_current_wait_groups(new_value);
    
  if (ackinfo)
    reportReplyBinlog(ackinfo->binlog_name, ackinfo->binlog_pos);

  return function_exit(kWho, 0);
}

unsigned int ReplSemiSyncMaster::get_actually_wait_count(unsigned int wait_count)
{
  // if the wait_count=0, we should not wait
  if(0 == wait_count)
    return wait_count;

  if(server_info_.is_active_master_group_not_wait())
    return wait_count-1;
  
  return wait_count;
}

bool ReplSemiSyncMaster::set_server_info()
{
  const char *kWho = "ReplSemiSyncMaster::set_server_info";
  function_enter(kWho);

  for(int i=0; i<MAX_RPL_GROUP_NUM; i++)
  {
    if(!set_ack_group_info(rpl_semi_sync_master_group[i],i+1))
    {
      return function_exit(kWho, false);
    }
  }
  
  return function_exit(kWho, true);
}

bool ReplSemiSyncMaster::set_ack_group_info(char *strPos, unsigned int group_id)
{
  GroupInfo group_info;
  const char *kWho = "ReplSemiSyncMaster::set_ack_group_info";
  function_enter(kWho);

  memset(&group_info, 0, sizeof(group_info));

  /*
    parse_one_group_info has two cases:
    case 1: The group* string is NULL, the group_info->wait_ack_num will be 0.
            It will reset the group info.
    case 2: The group* string is not NULL, the wait_ack_num and server_num not 0.
            It will update the group_info and resize the ack_group_container_.
  */
  if(!parse_one_group_info(&group_info, strPos))
  {
    return function_exit(kWho, false);
  }

  lock();

  if(!check_one_group_info(&group_info, group_id))
  {
    unlock();
    return function_exit(kWho, false);
  }

  set_one_group_status(&group_info);
    
  /* set the group_info in server_info_ */
  server_info_.set_group_info(group_id, &group_info);

  if(server_info_.is_master_group(group_id))
    set_master_group_container();
  else
    set_without_master_group_container(group_id, server_info_.get_curr_wait_ack_num(group_id));

  if (!getMasterEnabled() || !is_on())
    goto l_end;

  /* process the active_gourps add or remove */
  if(is_need_wait_no_slave())
    set_current_wait_group_count(get_actually_wait_count(rpl_wait_count_hwm));
  else
    proc_active_groups_changed();

l_end:  
  unlock();
  return function_exit(kWho, true);
}

bool ReplSemiSyncMaster::parse_one_group_info(GroupInfo *group_info, char *strPos)
{
  char *wait_num_pos = NULL;

  const char *kWho = "ReplSemiSyncMaster::parse_one_group_info";
  function_enter(kWho);

  // when init or set NULL can enter this case.
  if(NULL == strPos || is_str_null(strPos))
  {
    if (trace_level_ & kTraceDetail)
    {
      sql_print_information("Semi-sync master: get the group* string is NULL");
    }
    return function_exit(kWho, true);
  }

  // check the len of the group* string
  if(strlen(strPos) >= MAX_RPL_GROUP_LEN)
  {
    sql_print_information("Semi-sync master: the group* string is longer than 1024");
    return function_exit(kWho, false);
  }

  wait_num_pos = parse_server_id_list(group_info, strPos);
  if(NULL == wait_num_pos)
    return function_exit(kWho, false);

  if(!parse_wait_ack_num(group_info, wait_num_pos))
    return function_exit(kWho, false);

  return function_exit(kWho, true);
}

char* ReplSemiSyncMaster::parse_server_id_list(GroupInfo *group_info, char *str_pos)
{
  longlong server_id = 0;
  char *pos = NULL;
  char *begin_pos = str_pos;

  const char *kWho = "ReplSemiSyncMaster::parse_server_id_list";
  function_enter(kWho);

  pos = str_pos;
  while(*pos)
  {
    if(is_space_or_digital(*pos))
    {
      pos++;
      continue;
    }
    
    if(*pos == ',' || *pos == ':')
    {
      server_id = get_number(begin_pos, pos);
      if(-1 == server_id)
      {
        sql_print_information("Semi-sync master: parse error server_id num[%s].",begin_pos);
        function_exit(kWho, false);
        return NULL;
      }
      group_info->server_id[group_info->server_num++] = (uint)server_id;
      pos++;
      begin_pos = pos;
      if(*(pos-1) == ':')
      {
        function_exit(kWho, true);
        return begin_pos;
      }
      continue;
    }
    
    // other no support char will return false
    sql_print_information("Semi-sync master: server_id list parse error char[%s]", pos);
    function_exit(kWho, false);
    return NULL;
  }

  // format error char will return false
  sql_print_information("Semi-sync master: server_id list parse error [%s]", str_pos);
  function_exit(kWho, false);
  return NULL;
}

bool ReplSemiSyncMaster::parse_wait_ack_num(GroupInfo *group_info, char *str_pos)
{
  longlong wait_ack_num = 0;
  char *pos = NULL;
  char *begin_pos = str_pos;

  const char *kWho = "ReplSemiSyncMaster::parse_wait_ack_num";
  function_enter(kWho);

  pos = str_pos;
  if(0 == *(pos))
  {
    sql_print_information("Semi-sync master: parse error with no wait_ack_num");
    return function_exit(kWho, false);
  }
  
  while(*pos)
  {
    if(is_space_or_digital(*pos))
    {
      pos++;
      continue;
    }
    sql_print_information("Semi-sync master: wait_ack_num parse error char[%s]", pos);
    return function_exit(kWho, false);
  }

  wait_ack_num = get_number(begin_pos, pos);
  if(-1 == wait_ack_num)
  {
    sql_print_information("Semi-sync master: parse error wait_ack_num[%s].",begin_pos);
    return function_exit(kWho, false);
  }
  
  group_info->wait_ack_num = (uint)wait_ack_num;
  return function_exit(kWho, true);
}

bool ReplSemiSyncMaster::check_one_group_info(GroupInfo *group_info, unsigned int group_id)
{
  unsigned int i =0;
  unsigned int j =0;
  unsigned int k =0;

  const char *kWho = "ReplSemiSyncMaster::check_one_group_info";
  function_enter(kWho);

  // the rpl_semi_sync_master_group* = NULL
  if(0 == group_info->server_num && 0 == group_info->wait_ack_num)
  {
    return function_exit(kWho, true);
  }

  // with no server_id or no wait_ack_num will return an error
  if(0 == group_info->server_num)
  {
    sql_print_information("Semi-sync master: server_id num[%d] is 0", group_info->server_num);
    return function_exit(kWho, false);
  }

  // format error, too many server_num
  if(group_info->server_num > MAX_GROUP_SLAVE_NUM)
  {
    sql_print_information("Semi-sync master: too many slave servers[%d] than 256",
        group_info->server_num);
    return function_exit(kWho, false);
  }

  // with more than 256 wait_ack_num will return an error
  if(group_info->wait_ack_num > MAX_GROUP_SLAVE_NUM)
  {
    sql_print_information("Semi-sync master: wait_ack_num[%d] bigger than 256", group_info->wait_ack_num);
    return function_exit(kWho, false);
  }

  // wait_ack_num>server_num will return an error
  if(group_info->wait_ack_num > group_info->server_num)
  {
    sql_print_information("Semi-sync master: wait_num[%d] is bigger than server_id num[%d]",\
        group_info->wait_ack_num, group_info->server_num);
    return function_exit(kWho, false);
  }

  for(unsigned int i=0; i<group_info->server_num; i++)
  {
    // the server_id is 0 will return an error
    if(0 == group_info->server_id[i])
    {
      sql_print_information("Semi-sync master: the server_id can not be set 0");
      return function_exit(kWho, false);
    }
    for(unsigned int j=i+1; j<group_info->server_num; j++)
    {
      // has same server_id in one group string will return an error
      if(group_info->server_id[i] == group_info->server_id[j])
      {
        sql_print_information("Semi-sync master: has same server_id[%u]", group_info->server_id[i]);
        return function_exit(kWho, false);
      }
    }
  }
  
  // has same server_id in other groups will return an error
  for(i=0; i<group_info->server_num; i++)
  {
    for(j=0; j<MAX_RPL_GROUP_NUM; j++)
    {
      for(k=0; k<server_info_.group_info[j].server_num; k++)
      {
        if(group_info->server_id[i] == server_info_.group_info[j].server_id[k]
           && group_id != server_info_.group_info[j].group_id)
        {
          sql_print_information("Semi-sync master: there are same server_id[%u] in group[%d].",\
            group_info->server_id[i], server_info_.group_info[j].group_id);
          return function_exit(kWho, false);
        }
      }
    }
  }

  return function_exit(kWho, true);
}

int ReplSemiSyncMaster::set_one_group_status(GroupInfo *group_info)
{
  unsigned int i =0;
  unsigned int j =0;

  const char *kWho = "ReplSemiSyncMaster::set_one_group_status";
  function_enter(kWho);

  // set the listened_server_num
  for(i=0; i<group_info->server_num; i++)
  {
    for(j=0; j<server_info_.listened_all_server_num; j++)
    {
      if(group_info->server_id[i] == server_info_.listened_server_id[j])
      {
        group_info->listened_server_num++;
      }
    }
  }

  // set the with_master_server status
  for(i=0; i<group_info->server_num; i++)
  {
    if(group_info->server_id[i] == rpl_serverid)
    {
      group_info->with_master_server = true;
    }
  }

  /* 
    1: set group* = NULL will cause wait_ack_num = 0
    2: the wait_ack_num is set to 0, example: group1=1111,2222:0
  */
  if(0 == group_info->wait_ack_num)
    return function_exit(kWho, 0);

  // set the active_status
  if(rpl_with_master_stat_hwm && group_info->with_master_server)
  {
    if(group_info->listened_server_num+1 >= group_info->wait_ack_num)
      group_info->active_status = GROUP_ACTIVE;
    group_info->curr_wait_ack_num = group_info->wait_ack_num-1;
  }
  else
  {
    if(group_info->listened_server_num >= group_info->wait_ack_num)
      group_info->active_status = GROUP_ACTIVE;
    group_info->curr_wait_ack_num = group_info->wait_ack_num;
  }
 
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::insert_without_master_group(unsigned int server_id, unsigned int group_id,
                                          const char *log_file_name, my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::insert_without_master_group";
  function_enter(kWho);

  if(GROUP_TIMEOUT == server_info_.group_info[group_id-1].active_status)
  {
    if(false == try_switch_group_active(group_id, log_file_name, log_file_pos))
      return function_exit(kWho, 0);
  }

  insert_group(server_id, group_id, log_file_name, log_file_pos);

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::insert_hwm_master_group(unsigned int server_id, unsigned int group_id,
                                          const char *log_file_name, my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::insert_hwm_master_group";
  function_enter(kWho);

  if(GROUP_TIMEOUT == server_info_.group_info[group_id-1].active_status)
  {
    if(false == try_switch_group_active(group_id, log_file_name, log_file_pos))
      return function_exit(kWho, 0);
  }

  if(rpl_semi_sync_master_err_flag)
    return function_exit(kWho, 0);

  insert_group(server_id, group_id, log_file_name, log_file_pos);

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::insert_lwm_master_group(unsigned int server_id, unsigned int group_id,
                                          const char *log_file_name, my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::insert_lwm_master_group";
  function_enter(kWho);

  if(GROUP_TIMEOUT == server_info_.lwm_master_group_info.active_status)
  {
    if(false == try_switch_lwm_master_group_active(group_id, log_file_name, log_file_pos))
      return function_exit(kWho, 0);
  }

  if(!rpl_semi_sync_master_err_flag)
    return function_exit(kWho, 0);

  insert_group(server_id, group_id, log_file_name, log_file_pos);

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::insert_group(unsigned int server_id, unsigned int group_id,
                                          const char *log_file_name, my_off_t log_file_pos)
{
  const char *kWho = "ReplSemiSyncMaster::insert_group";
  function_enter(kWho);

  if(1 == rpl_semi_sync_master_current_wait_groups)
  {
    reportReplyBinlog(log_file_name, log_file_pos);
  }
  else
  {
    const AckInfo *ack = ack_container_.insert_quick_sync(server_id, group_id, log_file_name, log_file_pos);
    if(NULL != ack)
    {
      reportReplyBinlog(ack->binlog_name, ack->binlog_pos);
    }
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::proc_active_groups_changed()
{
  const char *kWho = "ReplSemiSyncMaster::proc_active_groups_changed";
  function_enter(kWho);
  
  set_err_flag_and_stat_flag(SET_ERR_FLAG);
  
  if(GROUP_NORMAL_OK == rpl_semi_sync_master_err_flag)
  {
    set_current_wait_group_count(get_actually_wait_count(rpl_wait_count_hwm));
  }
  else
  {
    set_current_wait_group_count(get_actually_wait_count(rpl_wait_count_lwm));

    if(GROUP_BELOW_LWM_ALARM_ERR == rpl_semi_sync_master_err_flag)
    {
      active_tranxs_->signal_waiting_sessions_all();
      ack_container_.clear();
    }
  }

  return function_exit(kWho, 0);
}

bool ReplSemiSyncMaster::is_group_timeout(uint32 server_id)
{
  const char *kWho = "ReplSemiSyncMaster::is_group_timeout";
  function_enter(kWho);
  
  bool result = false;

  if(GROUP_NORMAL_OK == rpl_semi_sync_master_err_flag)
    goto l_end;

  if(0 == server_info_.get_group_id(server_id))
    goto l_end;

  result = server_info_.is_timeout_group_exist();

l_end:
  return function_exit(kWho, result);
}

bool ReplSemiSyncMaster::is_need_wait_no_slave()
{
  const char *kWho = "ReplSemiSyncMaster::is_need_wait_no_slave";
  function_enter(kWho);

  /**
    If the wait_no_slave_ is ON, quick sync should wait until
    quick sync wait timeout.
  */
  if(get_wait_no_slave_inline() != 0
     && rpl_semi_sync_master_err_flag == GROUP_NORMAL_OK)
  {
    return function_exit(kWho, true);
  }

  return function_exit(kWho, false);
}

int ReplSemiSyncMaster::print_timeout_serverid(unsigned int group_id, TranxNode *node)
{
  const char *kWho = "ReplSemiSyncMaster::print_timeout_serverid";
  function_enter(kWho);

  bool is_unreceived_serverid = false;
  bool is_first_timeout_server = true;
  unsigned int serverid_rcv[MAX_GROUP_SLAVE_NUM] = {0};
  String Str_buf;
  char tmp_buf[256]={0};
  int tmp_len=0;
  
  if(GROUP_NORMAL_OK != rpl_semi_sync_master_err_flag &&
     server_info_.is_master_group(group_id))
  {
    lwm_master_group_ack_container_.get_received_serverid(serverid_rcv,node);
  }
  else
  {
    ack_group_container_[group_id-1].get_received_serverid(serverid_rcv,node);
  }

  for(unsigned int i=0; i<server_info_.group_info[group_id-1].server_num; i++)
  {
    if(!server_info_.is_server_listened(server_info_.group_info[group_id-1].server_id[i]))
      continue;
  
    is_unreceived_serverid = true;
    for(unsigned int j=0; j<MAX_GROUP_SLAVE_NUM; j++)
    {
      if(server_info_.group_info[group_id-1].server_id[i]== serverid_rcv[j])
      {
        is_unreceived_serverid = false;
        break;
      }
    }

    if(is_unreceived_serverid)
    {
      if(is_first_timeout_server)
      {
        tmp_len = sprintf((char*)tmp_buf,"%u",server_info_.group_info[group_id-1].server_id[i]);
        is_first_timeout_server = false;
      }
      else
      {
        tmp_len = sprintf((char*)tmp_buf,",%u",server_info_.group_info[group_id-1].server_id[i]);
      }
      tmp_buf[tmp_len] = 0;
      Str_buf.append(tmp_buf,tmp_len);
    }
  }
  
  sql_print_information("Timeout group[%d], the server_id[%s] not reply ack.", group_id, Str_buf.c_ptr());
  
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::set_timeout_groups(TranxNode *node)
{
  const char *kWho = "ReplSemiSyncMaster::set_timeout_groups";
  function_enter(kWho);
  
  unsigned int groupid_rcv[MAX_RPL_GROUP_NUM] = {0};
  bool is_unreceived_group = true;

  ack_container_.find_received_groups(groupid_rcv, node);

  for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
  {
    is_unreceived_group = true;
    for(unsigned int j=0; j<MAX_RPL_GROUP_NUM; j++)
    {
      if(server_info_.group_info[i].group_id == groupid_rcv[j])
      {
        is_unreceived_group = false;
        break;
      }
    }

    if(is_unreceived_group &&
       server_info_.is_active_group_need_wait(server_info_.group_info[i].group_id))
    {
      print_timeout_serverid(server_info_.group_info[i].group_id, node);
      
      proc_timeout_group(server_info_.group_info[i].group_id);
    }
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::proc_timeout_group(unsigned int group_id)
{
  const char *kWho = "ReplSemiSyncMaster::proc_timeout_group";
  function_enter(kWho);
  
  if(server_info_.is_master_group(group_id))
  {
    server_info_.switch_active_to_timeout(group_id);
    server_info_.reset_master_group_hwm();

    server_info_.switch_lwm_master_group_timeout(group_id);
    server_info_.reset_master_group_lwm();
  }
  else
  {
    server_info_.switch_active_to_timeout(group_id);
  }
  
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::switch_master_group_container_to_lwm()
{
  const char *kWho = "ReplSemiSyncMaster::switch_master_group_container_to_lwm";
  function_enter(kWho);

  const AckInfo *ackinfo= NULL;
  const AckInfo *ack_lwm= NULL;
  unsigned int tmp_server_id = 0;
  unsigned int group_id = 0;

  group_id = server_info_.get_group_id_with_master();
  if(0 == group_id)
    return function_exit(kWho, 0);

  for(unsigned int i=0; i<server_info_.group_info[group_id-1].server_num; i++)
  {
    ackinfo = NULL;
    ack_lwm = NULL;
    tmp_server_id = server_info_.group_info[group_id-1].server_id[i];
    ack_group_container_[group_id-1].get_ack_info_by_server_id(tmp_server_id, &ackinfo);
    if(ackinfo)
    {
      ack_lwm = lwm_master_group_ack_container_.insert_quick_sync(tmp_server_id, group_id,
                                             ackinfo->binlog_name, ackinfo->binlog_pos);
      if(ack_lwm)
      {
        server_info_.switch_lwm_master_group_active(group_id);
        set_err_flag_below_hwm();
        insert_group(ack_lwm->server_id, ack_lwm->group_id,
                     ack_lwm->binlog_name, ack_lwm->binlog_pos);
      }
    }
  }
  
  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::switch_master_group_container_to_hwm()
{
  const char *kWho = "ReplSemiSyncMaster::switch_master_group_container_to_hwm";
  function_enter(kWho);

  const AckInfo *ackinfo= NULL;
  const AckInfo *ack_hwm= NULL;
  unsigned int tmp_server_id = 0;
  unsigned int group_id = 0;

  group_id = server_info_.get_group_id_with_master();
  if(0 == group_id)
    return function_exit(kWho, 0);

  for(unsigned int i=0; i<server_info_.group_info[group_id-1].server_num; i++)
  {
    ackinfo = NULL;
    ack_hwm = NULL;
    tmp_server_id = server_info_.group_info[group_id-1].server_id[i];
    lwm_master_group_ack_container_.get_ack_info_by_server_id(tmp_server_id, &ackinfo);
    if(ackinfo)
    {
      ack_hwm = ack_group_container_[group_id-1].insert_quick_sync(tmp_server_id, group_id,
                                             ackinfo->binlog_name, ackinfo->binlog_pos);
      if(ack_hwm)
      {
        server_info_.switch_timeout_to_active(group_id);
        insert_group(ack_hwm->server_id, ack_hwm->group_id,
                     ack_hwm->binlog_name, ack_hwm->binlog_pos);
      }
    }
  }
  
  return function_exit(kWho, 0);
}

int  ReplSemiSyncMaster::inspection_transactions()
{
    const char *kWho = "ReplSemiSyncMaster::inspection_transactions";
    function_enter(kWho);
    TranxNode *new_front = NULL;
    AckInfo tmp_ack_info;
    
    lock();

    if (!getMasterEnabled() || !active_tranxs_)
      goto l_end;
    
    new_front = active_tranxs_->get_front();
    while (new_front)
    {
      if (new_front->thd)
        break;
      new_front = new_front->next_;
    } 

    if (!new_front)
      goto l_end;

    struct timespec current_ts;
    set_timespec(&current_ts, 0);
    // not timeout, next check
    if (cmp_timespec(&current_ts,&(new_front->time_end))>0)
    {
      sql_print_warning("Timeout waiting for reply of binlog (file: %s, pos: %lu), "
                          "quick-sync up to file %s, position %lu.",
                          new_front->log_name_, (unsigned long)new_front->log_pos_,
                          reply_file_name_, (unsigned long)reply_file_pos_);

      /* save the timeout position for the ack reply later */
      tmp_ack_info.binlog_pos = new_front->log_pos_;
      strncpy(tmp_ack_info.binlog_name, new_front->log_name_, sizeof(tmp_ack_info.binlog_name)-1);
      
      set_timeout_groups(new_front);

      /* if the err_flag=GROUP_BELOW_LWM_ALARM_ERR, this will signal all tranxnode. */      
      proc_active_groups_changed();

      /* should reply the ack when timeout in this case. */
      if(GROUP_BELOW_HWM_ALARM_ERR==rpl_semi_sync_master_err_flag)
        active_tranxs_->signal_waiting_sessions_up_to(tmp_ack_info.binlog_name, tmp_ack_info.binlog_pos);
    }

l_end:    
    unlock(); 
    return function_exit(kWho, 0);
}

bool  ReplSemiSyncMaster::add_thd_to_tranx_node(TranxNode *trx, THD *leader_thd)
{
    const char *kWho = "ReplSemiSyncMaster::add_thd_to_tranx_node";
    function_enter(kWho);
    /** 
      do not check whether trx or thd is null, caller ensure.
      do not get lock, caller ensure.
      m_next_leader will be used when has the two conditions:
      switch off to on and the binlog_order_commits = 0.
    */
    
    if (NULL == trx->thd)
    {
        trx->thd = leader_thd;
        return function_exit(kWho, 0);
    }

    THD *tmp = trx->thd;
    while (tmp)
    {
        if (NULL == tmp->m_next_leader) // tail
            break;
        tmp = tmp->m_next_leader;
    }
    tmp->m_next_leader = leader_thd;
    if(trace_level_ & kTraceDetail)
      sql_print_information("the tranxnode has two or more thds, the waiter num is[%d]", trx->n_waiters);
    
    return function_exit(kWho, 1);
}

int ReplSemiSyncMaster::set_err_flag_and_stat_flag(bool is_init)
{
  const char *kWho = "ReplSemiSyncMaster::set_err_flag_and_stat_flag";
  function_enter(kWho);

  if(is_init)
    init_err_flag();
  else
    set_err_flag();

  server_info_.update_master_group_status();
  server_info_.update_active_group_num();

  if(GROUP_NORMAL_OK == rpl_semi_sync_master_err_flag)
  {
    set_curr_stat_master_flag(rpl_with_master_stat_hwm);
    switch_master_group_container_to_hwm();
  }
  else
  {
    set_curr_stat_master_flag(rpl_with_master_stat_lwm);
    switch_master_group_container_to_lwm();
  }

  return function_exit(kWho, 0);
}

int ReplSemiSyncMaster::set_err_flag_below_hwm()
{
  const char *kWho = "ReplSemiSyncMaster::set_err_flag_below_hwm";
  function_enter(kWho);

  if(GROUP_NORMAL_OK == rpl_semi_sync_master_err_flag)
    return function_exit(kWho, 0);

  set_err_flag();

  return function_exit(kWho, 0);
}

const AckInfo *AckContainer::insert(int server_id, const char *log_file_name,
                                    my_off_t log_file_pos) {
  const AckInfo *ret_ack = NULL;

  const char *kWho = "AckContainer::insert";
  function_enter(kWho);

  if (!m_greatest_ack.less_than(log_file_name, log_file_pos)) {
    if (trace_level_ & kTraceDetail)
      LogErr(INFORMATION_LEVEL, ER_SEMISYNC_RECEIVED_ACK_IS_SMALLER);

    goto l_end;
  }

  /* Update the slave's ack position if it is in the ack array */
  if (updateIfExist(server_id, log_file_name, log_file_pos) < m_size)
    goto l_end;

  if (full()) {
    AckInfo *min_ack;

    ret_ack = &m_greatest_ack;

    /* Find the minimum ack which is smaller than the inserted ack. */
    min_ack = minAck(log_file_name, log_file_pos);
    if (likely(min_ack == NULL)) {
      m_greatest_ack.set(server_id, log_file_name, log_file_pos);

      /* Remove all slaves which have minimum ack position from the ack array */
      remove_all(log_file_name, log_file_pos);

      /* Don't insert current ack into container if it is the minimum ack. */
      goto l_end;
    } else {
      m_greatest_ack = *min_ack;
      remove_all(m_greatest_ack.binlog_name, m_greatest_ack.binlog_pos);
    }
  }

  m_ack_array[m_empty_slot].set(server_id, log_file_name, log_file_pos);

  if (trace_level_ & kTraceDetail)
    LogErr(INFORMATION_LEVEL, ER_SEMISYNC_ADD_ACK_TO_SLOT, m_empty_slot);

l_end:
  function_exit(kWho, 0);
  return ret_ack;
}

const AckInfo* AckContainer::insert_quick_sync(unsigned int server_id, unsigned int group_id,
                                     const char *log_file_name, my_off_t log_file_pos)
{
  const AckInfo *ret_ack= NULL;

  const char *kWho = "AckContainer::insert_quick_sync";
  function_enter(kWho);

  if (!m_greatest_ack.less_than(log_file_name, log_file_pos))
  {
    if (trace_level_ & kTraceDetail)
      sql_print_information("The received ack is smaller than m_greatest_ack");

    goto l_end;
  }

  /* Update the slave's ack position if it is in the ack array */
  if (update_if_exist_with_groupid(server_id, group_id, log_file_name, log_file_pos) < m_size)
    goto l_end;

  if (full())
  {
    AckInfo *min_ack;

    ret_ack= &m_greatest_ack;

    /* Find the minimum ack which is smaller than the inserted ack. */
    min_ack= minAck(log_file_name, log_file_pos);
    if (likely(min_ack == NULL))
    {
      m_greatest_ack.set_with_groupid(server_id, group_id, log_file_name, log_file_pos);

      /* Remove all slaves which have minimum ack position from the ack array */
      remove_all(log_file_name, log_file_pos);

      /* Don't insert current ack into container if it is the minimum ack. */
      goto l_end;
    }
    else
    {
      m_greatest_ack= *min_ack;
      remove_all(m_greatest_ack.binlog_name, m_greatest_ack.binlog_pos);
    }
  }

  m_ack_array[m_empty_slot].set_with_groupid(server_id, group_id, log_file_name, log_file_pos);

  if (trace_level_ & kTraceDetail)
    sql_print_information("Add the ack into slot %u", m_empty_slot);

l_end:
  function_exit(kWho, 0);
  return ret_ack;
}

int AckContainer::resize(unsigned int size, const AckInfo **ackinfo) {
  AckInfo *old_ack_array = m_ack_array;
  unsigned int old_array_size = m_size;
  unsigned int i;

  if (size - 1 == m_size  || size == 0) return 0;

  m_size = size - 1;
  m_ack_array = NULL;
  if (m_size) {
    m_ack_array = (AckInfo *)DBUG_EVALUATE_IF(
        "rpl_semisync_simulate_allocate_ack_container_failure", NULL,
        my_malloc(0, sizeof(AckInfo) * (size - 1), MYF(MY_ZEROFILL)));
    if (m_ack_array == NULL) {
      m_ack_array = old_ack_array;
      m_size = old_array_size;
      return -1;
    }
  }

  if (old_ack_array != NULL) {
    for (i = 0; i < old_array_size; i++) {
      const AckInfo *ack = NULL;
      if(rpl_semi_sync_master_quick_sync_enabled)
        ack= insert_quick_sync(old_ack_array[i]);
      else
        ack= insert(old_ack_array[i]);
      if (ack) *ackinfo = ack;
    }
    my_free(old_ack_array);
  }
  return 0;
}

int AckContainer::find_received_groups(unsigned int *groupid_rcv, TranxNode *node)
{
  unsigned int i;
  AckInfo *ack = NULL;
  
  for(i=0; i<size(); i++)
  {
    ack = &m_ack_array[i];
    if((false == ack->empty()) &&
       (ActiveTranx::compare(ack->binlog_name,ack->binlog_pos,
                             node->log_name_, node->log_pos_)>=0))
    {
      groupid_rcv[i] = ack->group_id;
    }
  }
  return 0;
}

int AckContainer::get_ack_info_by_server_id(unsigned int server_id,
                                                      const AckInfo **ackinfo)
{
  unsigned int i;
  AckInfo *ack = NULL;
  
  for(i=0; i<size(); i++)
  {
    ack = &m_ack_array[i];
    if((false == ack->empty()) && (server_id == ack->server_id))
    {
      *ackinfo = ack;
      break;
    }
  }
  return 0;
}

int AckContainer::get_received_serverid(unsigned int *serverid_rcv, TranxNode *node)
{
  unsigned int i;
  AckInfo *ack = NULL;
  
  for(i=0; i<size(); i++)
  {
    ack = &m_ack_array[i];
    if((false == ack->empty()) &&
       (ActiveTranx::compare(ack->binlog_name,ack->binlog_pos,
                             node->log_name_, node->log_pos_)>=0))
    {
      serverid_rcv[i] = ack->server_id;
    }
  }
  return 0;
}

/* Get the waiting time given the wait's staring time.
 *
 * Return:
 *  >= 0: the waiting time in microsecons(us)
 *   < 0: error in get time or time back traverse
 */
static int getWaitTime(const struct timespec &start_ts) {
  unsigned long long start_usecs, end_usecs;
  struct timespec end_ts;

  /* Starting time in microseconds(us). */
  start_usecs = timespec_to_usec(&start_ts);

  /* Get the wait time interval. */
  set_timespec(&end_ts, 0);

  /* Ending time in microseconds(us). */
  end_usecs = timespec_to_usec(&end_ts);

  if (end_usecs < start_usecs) return -1;

  return (int)(end_usecs - start_usecs);
}
