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

#ifndef RPL_SEMISYNC_MASTER_H
#define RPL_SEMISYNC_MASTER_H

#include <sys/types.h>

#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_io.h"
#include "my_psi_config.h"
#include "sql/rpl_semisync.h"

extern PSI_memory_key key_ss_memory_TranxNodeAllocator_block;

#ifdef HAVE_PSI_INTERFACE
extern PSI_mutex_key key_ss_mutex_LOCK_binlog_;
extern PSI_cond_key key_ss_cond_COND_binlog_send_;
#endif

extern PSI_stage_info stage_waiting_for_semi_sync_ack_from_slave;
extern PSI_stage_info stage_waiting_for_semi_sync_slave;
extern PSI_stage_info stage_reading_semi_sync_ack;

#define TIME_THOUSAND 1000
#define TIME_MILLION  1000000
#define TIME_BILLION  1000000000

// the max len of group string
#define MAX_RPL_GROUP_LEN    1024
// the max groups num supported by quick sync
#define MAX_RPL_GROUP_NUM    10
// the max server num supported in one group
#define MAX_GROUP_SLAVE_NUM  256
// the max group status string len
#define MAX_GROUP_STATUS_LEN 32
#define MAX_INT32_NUM 0xffffffff

#define GROUP_UNACTIVE  0    /* the group with listened server num less than wait num. */
#define GROUP_TIMEOUT   1    /* the timeout group with request listened server num. */
#define GROUP_ACTIVE    2    /* the group with request listened server num. */

#define GROUP_INNER_CONTAINER      0   /* the  ack_group_container_ */
#define GROUPS_BETWEEN_CONTAINER   1   /* the  ack_container_ */

#define WAIT_GROUP_COUNT_LWM    1   /* This flag means has set the wait_group_count_lwm */
#define WITH_MASTER_GROUP_LWM   2   /* This flag means has set the with_master_group_lwm */

#define SET_ERR_FLAG   0
#define INIT_ERR_FLAG  1

extern unsigned int rpl_semi_sync_master_wait_for_slave_count;

// add for quick semisync
extern char rpl_semi_sync_master_quick_sync_enabled;
extern char *rpl_semi_sync_master_wait_cond_hwm;
extern char *rpl_semi_sync_master_wait_cond_lwm;
extern unsigned long rpl_semi_sync_master_inspection_time;
extern char *rpl_semi_sync_master_group[MAX_RPL_GROUP_NUM];

extern unsigned int rpl_semi_sync_master_err_flag;
extern unsigned int rpl_semi_sync_master_active_groups;
extern char rpl_semi_sync_master_current_stat_master_flag;
extern unsigned int rpl_semi_sync_master_current_wait_groups;
extern char rpl_semi_sync_master_group_status[MAX_RPL_GROUP_NUM][MAX_GROUP_STATUS_LEN];

extern unsigned int rpl_active_master_groups_hwm;
extern unsigned int rpl_active_master_groups_lwm;
extern unsigned int rpl_active_groups_without_master_group;
extern char rpl_with_master_stat_hwm;
extern char rpl_with_master_stat_lwm;

struct TranxNode {
  char log_name_[FN_REFLEN];
  my_off_t log_pos_;
  mysql_cond_t cond;
  int n_waiters;
  int              error_flag;     /* 0 means success, others means false */
  struct timespec  time_start;     /* recode the thd wait the ack begin time */
  struct timespec  time_end;
  THD              *thd;           /* the leader thd of the commit trx */
  struct TranxNode *next_;      /* the next node in the sorted list */
  struct TranxNode *hash_next_; /* the next node during hash collision */
};

/**
  @class TranxNodeAllocator

  This class provides memory allocating and freeing methods for
  TranxNode. The main target is performance.

  @section ALLOCATE How to allocate a node
    The pointer of the first node after 'last_node' in current_block is
    returned. current_block will move to the next free Block when all nodes of
    it are in use. A new Block is allocated and is put into the rear of the
    Block link table if no Block is free.

    The list starts up empty (ie, there is no allocated Block).

    After some nodes are freed, there probably are some free nodes before
    the sequence of the allocated nodes, but we do not reuse it. It is better
    to keep the allocated nodes are in the sequence, for it is more efficient
    for allocating and freeing TranxNode.

  @section FREENODE How to free nodes
    There are two methods for freeing nodes. They are free_all_nodes and
    free_nodes_before.

    'A Block is free' means all of its nodes are free.
    @subsection free_nodes_before
    As all allocated nodes are in the sequence, 'Before one node' means all
    nodes before given node in the same Block and all Blocks before the Block
    which containing the given node. As such, all Blocks before the given one
    ('node') are free Block and moved into the rear of the Block link table.
    The Block containing the given 'node', however, is not. For at least the
    given 'node' is still in use. This will waste at most one Block, but it is
    more efficient.
 */
#define BLOCK_TRANX_NODES 16
class TranxNodeAllocator {
 public:
  /**
    @param reserved_nodes
      The number of reserved TranxNodes. It is used to set 'reserved_blocks'
      which can contain at least 'reserved_nodes' number of TranxNodes.  When
      freeing memory, we will reserve at least reserved_blocks of Blocks not
      freed.
   */
  TranxNodeAllocator(uint reserved_nodes)
      : reserved_blocks(reserved_nodes / BLOCK_TRANX_NODES +
                        (reserved_nodes % BLOCK_TRANX_NODES > 1 ? 2 : 1)),
        first_block(NULL),
        last_block(NULL),
        current_block(NULL),
        last_node(-1),
        block_num(0) {}

  ~TranxNodeAllocator() {
    Block *block = first_block;
    while (block != NULL) {
      Block *next = block->next;
      free_block(block);
      block = next;
    }
  }

  /**
    The pointer of the first node after 'last_node' in current_block is
    returned. current_block will move to the next free Block when all nodes of
    it are in use. A new Block is allocated and is put into the rear of the
    Block link table if no Block is free.

    @return Return a TranxNode *, or NULL if an error occurred.
   */
  TranxNode *allocate_node() {
    TranxNode *trx_node;
    Block *block = current_block;

    if (last_node == BLOCK_TRANX_NODES - 1) {
      current_block = current_block->next;
      last_node = -1;
    }

    if (current_block == NULL && allocate_block()) {
      current_block = block;
      if (current_block) last_node = BLOCK_TRANX_NODES - 1;
      return NULL;
    }

    trx_node = &(current_block->nodes[++last_node]);
    trx_node->log_name_[0] = '\0';
    trx_node->log_pos_ = 0;
    trx_node->next_ = 0;
    trx_node->error_flag = 0;
    memset(&trx_node->time_start, 0, sizeof(trx_node->time_start));
    trx_node->thd = 0;
    trx_node->hash_next_ = 0;
    trx_node->n_waiters = 0;
    return trx_node;
  }

  /**
    All nodes are freed.

    @return Return 0, or 1 if an error occurred.
   */
  int free_all_nodes() {
    current_block = first_block;
    last_node = -1;
    free_blocks();
    return 0;
  }

  /**
    All Blocks before the given 'node' are free Block and moved into the rear
    of the Block link table.

    @param node All nodes before 'node' will be freed

    @return Return 0, or 1 if an error occurred.
   */
  int free_nodes_before(TranxNode *node) {
    Block *block;
    Block *prev_block = NULL;

    block = first_block;
    while (block != current_block->next) {
      /* Find the Block containing the given node */
      if (&(block->nodes[0]) <= node &&
          &(block->nodes[BLOCK_TRANX_NODES]) >= node) {
        /* All Blocks before the given node are put into the rear */
        if (first_block != block) {
          last_block->next = first_block;
          first_block = block;
          last_block = prev_block;
          last_block->next = NULL;
          free_blocks();
        }
        return 0;
      }
      prev_block = block;
      block = block->next;
    }

    /* Node does not find should never happen */
    DBUG_ASSERT(0);
    return 1;
  }

 private:
  uint reserved_blocks;

  /**
    A sequence memory which contains BLOCK_TRANX_NODES TranxNodes.

    BLOCK_TRANX_NODES The number of TranxNodes which are in a Block.

    next Every Block has a 'next' pointer which points to the next Block.
         These linking Blocks constitute a Block link table.
   */
  struct Block {
    Block *next;
    TranxNode nodes[BLOCK_TRANX_NODES];
  };

  /**
    The 'first_block' is the head of the Block link table;
   */
  Block *first_block;
  /**
    The 'last_block' is the rear of the Block link table;
   */
  Block *last_block;

  /**
    current_block always points the Block in the Block link table in
    which the last allocated node is. The Blocks before it are all in use
    and the Blocks after it are all free.
   */
  Block *current_block;

  /**
    It always points to the last node which has been allocated in the
    current_block.
   */
  int last_node;

  /**
    How many Blocks are in the Block link table.
   */
  uint block_num;

  /**
    Allocate a block and then assign it to current_block.
  */
  int allocate_block() {
    Block *block = (Block *)my_malloc(key_ss_memory_TranxNodeAllocator_block,
                                      sizeof(Block), MYF(0));
    if (block) {
      block->next = NULL;

      if (first_block == NULL)
        first_block = block;
      else
        last_block->next = block;

      /* New Block is always put into the rear */
      last_block = block;
      /* New Block is always the current_block */
      current_block = block;
      ++block_num;

      for (int i = 0; i < BLOCK_TRANX_NODES; i++)
        mysql_cond_init(key_ss_cond_COND_binlog_send_,
                        &current_block->nodes[i].cond);

      return 0;
    }
    return 1;
  }

  /**
    Free a given Block.
    @param block The Block will be freed.
   */
  void free_block(Block *block) {
    for (int i = 0; i < BLOCK_TRANX_NODES; i++)
      mysql_cond_destroy(&block->nodes[i].cond);
    my_free(block);
    --block_num;
  }

  /**
    If there are some free Blocks and the total number of the Blocks in the
    Block link table is larger than the 'reserved_blocks', Some free Blocks
    will be freed until the total number of the Blocks is equal to the
    'reserved_blocks' or there is only one free Block behind the
    'current_block'.
   */
  void free_blocks() {
    if (current_block == NULL || current_block->next == NULL) return;

    /* One free Block is always kept behind the current block */
    Block *block = current_block->next->next;
    while (block_num > reserved_blocks && block != NULL) {
      Block *next = block->next;
      free_block(block);
      block = next;
    }
    current_block->next->next = block;
    if (block == NULL) last_block = current_block->next;
  }
};

/**
   This class manages memory for active transaction list.

   We record each active transaction with a TranxNode, each session
   can have only one open transaction. Because of EVENT, the total
   active transaction nodes can exceed the maximum allowed
   connections.
*/
class ActiveTranx : public Trace {
 private:
  TranxNodeAllocator allocator_;
  /* These two record the active transaction list in sort order. */
  TranxNode *trx_front_, *trx_rear_;

  TranxNode **trx_htb_; /* A hash table on active transactions. */

  int num_entries_;     /* maximum hash table entries */
  mysql_mutex_t *lock_; /* mutex lock */

  inline void assert_lock_owner();

  inline unsigned int calc_hash(const unsigned char *key, unsigned int length);
  unsigned int get_hash_value(const char *log_file_name, my_off_t log_file_pos);

  int compare(const char *log_file_name1, my_off_t log_file_pos1,
              const TranxNode *node2) {
    return compare(log_file_name1, log_file_pos1, node2->log_name_,
                   node2->log_pos_);
  }
  int compare(const TranxNode *node1, const char *log_file_name2,
              my_off_t log_file_pos2) {
    return compare(node1->log_name_, node1->log_pos_, log_file_name2,
                   log_file_pos2);
  }
  int compare(const TranxNode *node1, const TranxNode *node2) {
    return compare(node1->log_name_, node1->log_pos_, node2->log_name_,
                   node2->log_pos_);
  }

 public:
  int signal_waiting_sessions_all();
  int signal_waiting_sessions_up_to(const char *log_file_name,
                                    my_off_t log_file_pos);
                                    
  /* release the unused nodes to increase the performance.
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int release_unused_node();
  
  /* deal with a TranxNode by quicksync or normal semisync.
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int signal_one_tranxnode(TranxNode *node);

  /* handle the tranxNode after the quicksync.
   * flag: it means the quicksync is normal or error, true is error
   */
  void clear_after_quicksync(TranxNode *node, bool flag);                                  
                                    
  TranxNode *find_active_tranx_node(const char *log_file_name,
                                    my_off_t log_file_pos);
  ActiveTranx(mysql_mutex_t *lock, unsigned long trace_level);
  ~ActiveTranx();

  /* Insert an active transaction node with the specified position.
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int insert_tranx_node(const char *log_file_name, my_off_t log_file_pos);

  /* Clear the active transaction nodes until(inclusive) the specified
   * position.
   * If log_file_name is NULL, everything will be cleared: the sorted
   * list and the hash table will be reset to empty.
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int clear_active_tranx_nodes(const char *log_file_name,
                               my_off_t log_file_pos);

  /* Given a position, check to see whether the position is an active
   * transaction's ending position by probing the hash table.
   */
  bool is_tranx_end_pos(const char *log_file_name, my_off_t log_file_pos);

  /* Given two binlog positions, compare which one is bigger based on
   * (file_name, file_position).
   */
  static int compare(const char *log_file_name1, my_off_t log_file_pos1,
                     const char *log_file_name2, my_off_t log_file_pos2);

  TranxNode* get_front()
  {    
    return trx_front_;
  }

  TranxNode* get_tail()
  {    
    return trx_rear_;
  }
                   
                     
  /* Find out if active tranx node list is empty or not
   *
   * Return:
   *   True :  If there are no nodes
   *   False:  othewise
   */
  bool is_empty() { return (trx_front_ == NULL); }
};

/* add by quick sync */
/* an active group means the listened_server_num >= wait_ack_num except the wait_ack_num=0 */
struct GroupInfo
{
  unsigned int active_status;        // GROUP_UNACTIVE,GROUP_TIMEOUT,GROUP_ACTIVE
  bool with_master_server;           // true is with master server, false is without master server
  unsigned int group_id;             // the group id, [1,10]
  unsigned int listened_server_num;  // the num of listened server in the group
  // the num of need wait ack in cfg, set 0 means not care and the group status may be unactive
  unsigned int wait_ack_num;         
  unsigned int curr_wait_ack_num;    // the num of need wait ack actually
  unsigned int server_num;           // the slave server num in cfg
  unsigned int server_id[MAX_GROUP_SLAVE_NUM];
};

struct SlaveServerInfo
{
  bool timeout_group_exist_flag;
  unsigned int current_wait_groups;         // the num of wait group count actually
  unsigned int active_group_num_without_master_group;  // the num of active group without master_group
  unsigned int listened_all_server_num;     // the num of listened all slave server
  GroupInfo group_info[MAX_RPL_GROUP_NUM];  // the group info array
  /* store the group status of master_group in lwm, the master_group status in group_info in hwm*/
  GroupInfo lwm_master_group_info;
  unsigned int listened_server_id[MAX_RPL_GROUP_NUM*MAX_GROUP_SLAVE_NUM];  // record all listened slave server id

  void init()
  {
    timeout_group_exist_flag  = false;
    current_wait_groups       = 0;
    active_group_num_without_master_group = 0;
    listened_all_server_num   = 0;
    memset(group_info, 0, sizeof(GroupInfo)*MAX_RPL_GROUP_NUM);
    memset(&lwm_master_group_info, 0, sizeof(lwm_master_group_info));
    memset(listened_server_id, 0, sizeof(unsigned int)*MAX_RPL_GROUP_NUM*MAX_GROUP_SLAVE_NUM);
    for(int i=0; i<MAX_RPL_GROUP_NUM; i++)
    {
      group_info[i].group_id= i+1;
    }
    return;
  }

  const char *group_status_to_str(unsigned int status)
  {
    if(GROUP_UNACTIVE == status)
      return "GROUP_UNACTIVE";
    else if(GROUP_TIMEOUT == status)
      return "GROUP_TIMEOUT";
    else if(GROUP_ACTIVE == status)
      return "GROUP_ACTIVE";
    else
      return "UNKNOWN_STATUS";
  }

  void set_current_wait_groups(unsigned int num) 
  { 
    current_wait_groups = num;
    rpl_semi_sync_master_current_wait_groups = current_wait_groups;
    return;
  }

  void update_groups_status(unsigned int group_id)
  {
    /* the first case only for master_group in lwm */
    if(GROUP_NORMAL_OK != rpl_semi_sync_master_err_flag && is_master_group(group_id))
    {
      snprintf(rpl_semi_sync_master_group_status[group_id-1], 
                  sizeof(rpl_semi_sync_master_group_status[group_id-1]),
                  "%d:%s",
                  lwm_master_group_info.listened_server_num, 
                  group_status_to_str(lwm_master_group_info.active_status));
    }
    else
    {
      snprintf(rpl_semi_sync_master_group_status[group_id-1], 
                  sizeof(rpl_semi_sync_master_group_status[group_id-1]),
                  "%d:%s",
                  group_info[group_id-1].listened_server_num, 
                  group_status_to_str(group_info[group_id-1].active_status));
    }
    
    return;
  }

  void update_master_group_status()
  {
    unsigned int group_id = 0;
    group_id = get_group_id_with_master();
    if(0 == group_id)
      return;

    update_groups_status(group_id);
    return;
  }

  void update_active_group_num()
  {
    update_active_groups_without_master_group();
    update_active_master_groups_hwm();
    update_active_master_groups_lwm();
    
    if(GROUP_NORMAL_OK == rpl_semi_sync_master_err_flag)
      rpl_semi_sync_master_active_groups=rpl_active_groups_without_master_group+rpl_active_master_groups_hwm;
    else
      rpl_semi_sync_master_active_groups=rpl_active_groups_without_master_group+rpl_active_master_groups_lwm;

    return;
  }

  void update_active_groups_without_master_group()
  {
    active_group_num_without_master_group = 0;
    for(int i =0; i<MAX_RPL_GROUP_NUM; i++)
    {
      if(false == is_master_group(group_info[i].group_id) &&
         GROUP_ACTIVE == group_info[i].active_status)
      {
        active_group_num_without_master_group++;
      }
    }
    
    rpl_active_groups_without_master_group = active_group_num_without_master_group;
    return;
  }

  void update_active_master_groups_hwm()
  {
    unsigned int master_group_id = get_group_id_with_master();
    if(0 == master_group_id)
    {
      rpl_active_master_groups_hwm = 0;
    }
    else
    {
      if(GROUP_ACTIVE == group_info[master_group_id-1].active_status)
        rpl_active_master_groups_hwm = 1;
      else
        rpl_active_master_groups_hwm = 0;
    }
    return;
  }

  void update_active_master_groups_lwm()
  {
    unsigned int master_group_id = get_group_id_with_master();
    if(0 == master_group_id)
    {
      rpl_active_master_groups_lwm = 0;
    }
    else
    {
      if(GROUP_ACTIVE == lwm_master_group_info.active_status)
        rpl_active_master_groups_lwm = 1;
      else
        rpl_active_master_groups_lwm = 0;
    }
    return;
  }

  bool is_timeout_group_exist()
  {
    if(timeout_group_exist_flag)
      return true;
    return false;
  }

  bool is_master_group(unsigned int group_id)
  {
    if(group_info[group_id-1].with_master_server)
      return true;

    return false;
  }

  bool is_active_group_need_wait(unsigned int group_id)
  { 
    if(GROUP_NORMAL_OK != rpl_semi_sync_master_err_flag &&
       is_master_group(group_id))
    {
      if(GROUP_ACTIVE == lwm_master_group_info.active_status &&
         get_lwm_curr_wait_ack_num())
        return true;
    }
    else
    {
      if(GROUP_ACTIVE == group_info[group_id-1].active_status &&
         get_curr_wait_ack_num(group_id))
        return true;
    }

    return false;
  }

  bool is_active_master_group_not_wait()
  { 
    unsigned int master_group_id = get_group_id_with_master();
    if(0 == master_group_id)
      return false;

    if(GROUP_NORMAL_OK == rpl_semi_sync_master_err_flag)
    {
      if(GROUP_ACTIVE == group_info[master_group_id-1].active_status &&
         0 == get_curr_wait_ack_num(master_group_id))
        return true;
    }
    else
    {
      if(GROUP_ACTIVE == lwm_master_group_info.active_status &&
         0 == get_lwm_curr_wait_ack_num())
        return true;
    }
    
    return false;
  }

  bool is_group_can_be_active(unsigned int group_id)
  {
    if(0 == group_info[group_id-1].wait_ack_num)
      return false;
    
    if(group_info[group_id-1].listened_server_num >= group_info[group_id-1].curr_wait_ack_num)
      return true;
    
    return false;
  }

  bool is_lwm_master_group_can_be_active()
  {
    if(0 == lwm_master_group_info.wait_ack_num)
      return false;
    
    if(lwm_master_group_info.listened_server_num >= lwm_master_group_info.curr_wait_ack_num)
      return true;
    
    return false;
  }

  bool is_server_listened(unsigned int server_id)
  {
    for(unsigned int i=0; i<listened_all_server_num; i++)
    {
      if(server_id == listened_server_id[i])
        return true;
    }
    return false;
  }
  
  /* get the group_id by server_id. return 0 means the server_id not in any groups */  
  unsigned int get_group_id(unsigned int server_id)
  {
    for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
    {
      for(unsigned int j=0; j<group_info[i].server_num; j++)
      {
        if(group_info[i].server_id[j] == server_id)
        {
          return group_info[i].group_id;
        }
      }
    }
    return 0;
  }

  unsigned int get_group_id_with_master()
  {
    for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
    {
      if(is_master_group(group_info[i].group_id))
      {
        return group_info[i].group_id;
      }
    }
    return 0;
  }

  unsigned int get_wait_ack_num(unsigned int group_id)
  {
    return group_info[group_id-1].wait_ack_num;
  }

  unsigned int get_lwm_wait_ack_num()
  {
    return lwm_master_group_info.wait_ack_num;
  }

  unsigned int get_curr_wait_ack_num(unsigned int group_id)
  {
    return group_info[group_id-1].curr_wait_ack_num;
  }

  unsigned int get_lwm_curr_wait_ack_num()
  {
    return lwm_master_group_info.curr_wait_ack_num;
  }  

  void switch_timeout_to_active(unsigned int group_id)
  {
    if(GROUP_TIMEOUT == group_info[group_id-1].active_status)
    {
      group_info[group_id-1].active_status = GROUP_ACTIVE;
      reset_timeout_group_exist_flag();
    }

    update_groups_status(group_id);
    update_active_group_num();
    return;
  }

  void switch_active_to_timeout(unsigned int group_id)
  {
    if(GROUP_ACTIVE == group_info[group_id-1].active_status)
    {
      group_info[group_id-1].active_status = GROUP_TIMEOUT;
      timeout_group_exist_flag = true;
    }

    update_groups_status(group_id);
    update_active_group_num();
    return;
  }

  void switch_lwm_master_group_timeout(unsigned int group_id)
  {
    if(!is_master_group(group_id))
      return;
    
    if(GROUP_ACTIVE == lwm_master_group_info.active_status)
    {
      lwm_master_group_info.active_status = GROUP_TIMEOUT;
      timeout_group_exist_flag = true;
    }

    update_groups_status(group_id);
    update_active_group_num();
    return;
  }

  void switch_lwm_master_group_active(unsigned int group_id)
  {
    if(!is_master_group(group_id))
      return;
    
    if(GROUP_TIMEOUT == lwm_master_group_info.active_status)
    {
      lwm_master_group_info.active_status = GROUP_ACTIVE;
      reset_timeout_group_exist_flag();
    }

    update_groups_status(group_id);
    update_active_group_num();
    return;
  }

  void reset_master_group_hwm()
  {
    unsigned int master_group_id = get_group_id_with_master();
    if(0 == master_group_id)
      return;

    // if the wait_ack_num is 0, the curr_wait_ack_num and active_status not need update
    if(0 == get_wait_ack_num(master_group_id))
    {
      update_groups_status(master_group_id);
      update_active_group_num();
      return;
    }

    if(rpl_with_master_stat_hwm)
      group_info[master_group_id-1].curr_wait_ack_num = group_info[master_group_id-1].wait_ack_num-1;
    else
      group_info[master_group_id-1].curr_wait_ack_num = group_info[master_group_id-1].wait_ack_num;

    if(0 == get_curr_wait_ack_num(master_group_id))
    {
      group_info[master_group_id-1].active_status = GROUP_ACTIVE;
    }
    else if(!is_group_can_be_active(master_group_id))
    {
      group_info[master_group_id-1].active_status = GROUP_UNACTIVE;
    }
    else
    {
      /* the timeout status will not change in this case */
      if(GROUP_UNACTIVE == group_info[master_group_id-1].active_status)
        group_info[master_group_id-1].active_status = GROUP_ACTIVE;
    }

    update_groups_status(master_group_id);
    update_active_group_num();
    return;
  }

  void reset_master_group_lwm()
  {
    unsigned int master_group_id = get_group_id_with_master();
    if(0 == master_group_id)
      return;

    // if the wait_ack_num is 0, the curr_wait_ack_num and active_status not need update
    if(0 == get_lwm_wait_ack_num())
    {
      update_groups_status(master_group_id);
      update_active_group_num();
      return;
    }

    if(rpl_with_master_stat_lwm)
      lwm_master_group_info.curr_wait_ack_num = lwm_master_group_info.wait_ack_num-1;
    else
      lwm_master_group_info.curr_wait_ack_num = lwm_master_group_info.wait_ack_num;

    if(0 == get_lwm_curr_wait_ack_num())
    {
      lwm_master_group_info.active_status = GROUP_ACTIVE;
    }
    else if(!is_lwm_master_group_can_be_active())
    {
      lwm_master_group_info.active_status = GROUP_UNACTIVE;
    }
    else
    {
      /* the timeout status will not change in this case */
      if(GROUP_UNACTIVE == lwm_master_group_info.active_status)
        lwm_master_group_info.active_status = GROUP_ACTIVE;
    }

    update_groups_status(master_group_id);
    update_active_group_num();
    return;
  }

  void reset_timeout_group_exist_flag()
  {
    timeout_group_exist_flag = false;
    for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
    {
      if(GROUP_TIMEOUT == group_info[i].active_status)
      {
        timeout_group_exist_flag = true;
        break;
      }
      if(is_master_group(group_info[i].group_id) &&
         GROUP_TIMEOUT == lwm_master_group_info.active_status)
      {
        timeout_group_exist_flag = true;
        break;
      }
    }
    return;
  }

  void init_all_group_status() 
  {
    unsigned int master_group_id = 0;
    
    timeout_group_exist_flag = false;
    
    for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
    {
      if(is_group_can_be_active(group_info[i].group_id))
        group_info[i].active_status = GROUP_ACTIVE;
      else
        group_info[i].active_status = GROUP_UNACTIVE;
      
      update_groups_status(group_info[i].group_id);
    }
    update_active_group_num();

    master_group_id = get_group_id_with_master();
    if(0 == master_group_id)
      return;
    
    if(is_lwm_master_group_can_be_active())
      lwm_master_group_info.active_status = GROUP_ACTIVE;
    else
      lwm_master_group_info.active_status = GROUP_UNACTIVE;

    update_groups_status(master_group_id);
    update_active_group_num();
    return;
  }

  /* only be called in set rpl_semi_sync_master_group1 and others. */
  void set_group_info(unsigned int group_id, GroupInfo *info)
  {
    /* clear old lwm_master_group_info */
    if(is_master_group(group_id))
    {
      memset(&lwm_master_group_info, 0, sizeof(lwm_master_group_info));
    }
    
    group_info[group_id-1].active_status = info->active_status;
    group_info[group_id-1].with_master_server = info->with_master_server;
    group_info[group_id-1].listened_server_num = info->listened_server_num;
    group_info[group_id-1].wait_ack_num = info->wait_ack_num;
    group_info[group_id-1].curr_wait_ack_num = info->curr_wait_ack_num;
    group_info[group_id-1].server_num = info->server_num;
    memcpy(group_info[group_id-1].server_id, info->server_id, sizeof(unsigned int)*MAX_GROUP_SLAVE_NUM);

    /* set new lwm_master_group_info */
    if(is_master_group(group_id))
    {
      memcpy(&lwm_master_group_info, &group_info[group_id-1], sizeof(lwm_master_group_info));
      reset_master_group_lwm();
    }

    update_groups_status(group_id);
    update_active_group_num();
    return;
  }
  
  void add_slave_server(unsigned int server_id) 
  {
    unsigned int group_id;
    
    listened_server_id[listened_all_server_num] = server_id;
    listened_all_server_num++;
    
    group_id = get_group_id(server_id);
    if(0 == group_id)
      return;

    group_info[group_id-1].listened_server_num++;
    
    if(is_master_group(group_id))
    {
      lwm_master_group_info.listened_server_num++;
      
      reset_master_group_hwm();
      reset_master_group_lwm();
    }
    else
    {
      if(GROUP_UNACTIVE == group_info[group_id-1].active_status &&
         is_group_can_be_active(group_id))
      {
        group_info[group_id-1].active_status = GROUP_ACTIVE;
      }
    }

    update_groups_status(group_id);
    update_active_group_num();
    return;
  }
  
  void remove_slave_server(unsigned int server_id) 
  {
    unsigned int group_id;

    for(unsigned int i=0; i<listened_all_server_num; i++)
    {
      if(listened_server_id[i] == server_id)
      {
        listened_server_id[i] = listened_server_id[listened_all_server_num-1];
        break;
      }
    }
    listened_server_id[listened_all_server_num-1] = 0;
    listened_all_server_num--;
    
    group_id = get_group_id(server_id);
    if(0 == group_id)
      return;

    group_info[group_id-1].listened_server_num--;

    if(is_master_group(group_id))
    {
      lwm_master_group_info.listened_server_num--;
      
      reset_master_group_hwm();
      reset_master_group_lwm();
    }
    else
    {
      if(GROUP_UNACTIVE != group_info[group_id-1].active_status &&
         !is_group_can_be_active(group_id))
      {
        group_info[group_id-1].active_status = GROUP_UNACTIVE;
      }
    }
    
    update_groups_status(group_id);
    update_active_group_num();
    return;
  }

};

/**
   AckInfo is a POD. It defines a structure includes information related to an
   ack: server_id   - which slave the ack comes from. binlog_name - the binlog
   group_id         - indicate that the server is belonged to which group.
   file name included in the ack. binlog_pos  - the binlog file position
   included in the ack.
*/
struct AckInfo {
  unsigned int server_id;
  unsigned int group_id;
  char binlog_name[FN_REFLEN];
  unsigned long long binlog_pos = 0;

  AckInfo() { clear(); }

  void clear() { binlog_name[0] = '\0'; }
  bool empty() const { return binlog_name[0] == '\0'; }
  bool is_server(unsigned int server_id) const { return this->server_id == server_id; }
  bool is_groupid(unsigned int group_id) const { return this->group_id == group_id; }

  bool equal_to(const char *log_file_name, my_off_t log_file_pos) const {
    return (ActiveTranx::compare(binlog_name, binlog_pos, log_file_name,
                                 log_file_pos) == 0);
  }
  bool less_than(const char *log_file_name, my_off_t log_file_pos) const {
    return (ActiveTranx::compare(binlog_name, binlog_pos, log_file_name,
                                 log_file_pos) < 0);
  }

  void set(unsigned int server_id, const char *log_file_name, my_off_t log_file_pos) {
    this->server_id = server_id;
    update(log_file_name, log_file_pos);
  }
  
  void set_with_groupid(unsigned int server_id, unsigned int group_id, const char *log_file_name, my_off_t log_file_pos)
  {
    this->server_id= server_id;
    this->group_id = group_id;
    update(log_file_name, log_file_pos);
  }
  
  void update(const char *log_file_name, my_off_t log_file_pos) {
    strcpy(binlog_name, log_file_name);
    binlog_pos = log_file_pos;
  }
};

/**
   AckContainer stores received acks internally and tell the caller the
   ack's position when a transaction is fully acknowledged, so it can wake
   up the waiting transactions.
 */
class AckContainer : public Trace {
 public:
  AckContainer() : m_ack_array(NULL), m_size(0), m_empty_slot(0), m_container_flag(GROUP_INNER_CONTAINER) {}
  ~AckContainer() {
    if (m_ack_array) my_free(m_ack_array);
  }

  /** Clear the content of the ack array */
  void clear() {
    if (m_ack_array) {
      for (unsigned i = 0; i < m_size; ++i) {
        m_ack_array[i].clear();
        m_ack_array[i].server_id = 0;
        m_ack_array[i].binlog_pos = 0;
      }
      m_empty_slot = m_size;
    }
    m_greatest_ack.clear();
  }

  /**
     Adjust capacity for the container and report the ack to semisync master,
     if it is full.

     @param[in] size size of the container.
     @param ackinfo Acknowledgement information

     @return 0 if succeeds, otherwise fails.
  */
  int resize(unsigned int size, const AckInfo **ackinfo);

  /**
     Insert an ack's information into the container and report the minimum
     ack to semisync master if it is full.

     @param[in] server_id  slave server_id of the ack
     @param[in] log_file_name  binlog file name of the ack
     @param[in] log_file_pos   binlog file position of the ack

     @return Pointer of an ack if the ack should be reported to semisync master.
             Otherwise, NULL is returned.
  */
  const AckInfo *insert(int server_id, const char *log_file_name,
                        my_off_t log_file_pos);
  const AckInfo *insert(const AckInfo &ackinfo) {
    return insert(ackinfo.server_id, ackinfo.binlog_name, ackinfo.binlog_pos);
  }
    
  const AckInfo* insert_quick_sync(unsigned int server_id, unsigned int group_id,
                          const char *log_file_name, my_off_t log_file_pos);
  
  const AckInfo* insert_quick_sync(const AckInfo &ackinfo)
  {
    return insert_quick_sync(ackinfo.server_id, ackinfo.group_id, ackinfo.binlog_name, ackinfo.binlog_pos);
  }

  void set_container_flag(int flag)
  {
    m_container_flag = flag;
  }

  int find_received_groups(unsigned int *groupid_rcv, TranxNode *node);

  int get_ack_info_by_server_id(unsigned int server_id, const AckInfo **ackinfo);

  int get_received_serverid(unsigned int *serverid_rcv, TranxNode *node);

 private:
  /* The greatest ack of the acks already reported to semisync master. */
  AckInfo m_greatest_ack;

  AckInfo *m_ack_array;
  /* size of the array */
  unsigned int m_size;
  /* index of an empty slot, it helps improving insert speed. */
  unsigned int m_empty_slot;
  /* GROUPS_BETWEEN_CONTAINER is the ack_container_, GROUPS_INNER_CONTAINER is the ack_group_container_ */
  int m_container_flag;

  /* Prohibit to copy AckContainer objects */
  AckContainer(AckContainer &container);
  AckContainer &operator=(const AckContainer &container);

  bool full() { return m_empty_slot == m_size; }
  unsigned int size() { return m_size; }

  /**
     Remove all acks which equal to the given position.

     @param[in] log_file_name  binlog name of the ack that should be removed
     @param[in] log_file_pos   binlog position of the ack that should removed
  */
  void remove_all(const char *log_file_name, my_off_t log_file_pos) {
    unsigned int i = m_size;
    for (i = 0; i < m_size; i++) {
      if (m_ack_array[i].equal_to(log_file_name, log_file_pos)) {
        m_ack_array[i].clear();
        m_empty_slot = i;
      }
    }
  }

  /**
     Update a slave's ack into the container if another ack of the
     slave is already in it.

     @param[in] server_id      server_id of the ack
     @param[in] log_file_name  binlog file name of the ack
     @param[in] log_file_pos   binlog file position of the ack

     @return index of the slot that is updated. if it equals to
             the size of container, then no slot is updated.
  */
  unsigned int updateIfExist(int server_id, const char *log_file_name,
                             my_off_t log_file_pos) {
    unsigned int i;

    m_empty_slot = m_size;
    for (i = 0; i < m_size; i++) {
      if (m_ack_array[i].empty())
        m_empty_slot = i;
      else if (m_ack_array[i].is_server(server_id)) {
        m_ack_array[i].update(log_file_name, log_file_pos);
        if (trace_level_ & kTraceDetail)
          LogErr(INFORMATION_LEVEL, ER_SEMISYNC_UPDATE_EXISTING_SLAVE_ACK, i);
        break;
      }
    }
    return i;
  }
  
  unsigned int update_if_exist_with_groupid(unsigned int server_id, unsigned int group_id, 
                                 const char *log_file_name, my_off_t log_file_pos)
  {
    unsigned int i;

    m_empty_slot= m_size;
    for (i= 0; i < m_size; i++)
    {
      if (m_ack_array[i].empty())
        m_empty_slot= i;
      else if ((GROUP_INNER_CONTAINER == m_container_flag && m_ack_array[i].is_server(server_id)) ||
               (GROUPS_BETWEEN_CONTAINER == m_container_flag && m_ack_array[i].is_groupid(group_id)))
      {
        m_ack_array[i].update(log_file_name, log_file_pos);
        if (trace_level_ & kTraceDetail)
          sql_print_information("Update an exsiting ack in slot %u", i);
        break;
      }
    }
    return i;
  }

  /**
     Find the minimum ack which is smaller than given position. When more than
     one slots are minimum acks, it returns the one has smallest index.

     @param[in] log_file_name  binlog file name
     @param[in] log_file_pos   binlog file position

     @return NULL if no ack is smaller than given position, otherwise
              return its pointer.
  */
  AckInfo *minAck(const char *log_file_name, my_off_t log_file_pos) {
    unsigned int i;
    AckInfo *ackinfo = NULL;

    for (i = 0; i < m_size; i++) {
      if (m_ack_array[i].less_than(log_file_name, log_file_pos))
        ackinfo = m_ack_array + i;
    }

    return ackinfo;
  }
};

/**
   The extension class for the master of semi-synchronous replication
*/
class ReplSemiSyncMaster : public ReplSemiSyncBase {
 private:
  ActiveTranx *active_tranxs_ = nullptr;
  /* active transaction list: the list will
     be cleared when semi-sync switches off. */

  /* True when initObject has been called */
  bool init_done_ = false;

  /* Mutex that protects the following state variables and the active
   * transaction list.
   * Under no cirumstances we can acquire mysql_bin_log.LOCK_log if we are
   * already holding LOCK_binlog_ because it can cause deadlocks.
   */
  mysql_mutex_t LOCK_binlog_;

  /* This is set to true when reply_file_name_ contains meaningful data. */
  bool reply_file_name_inited_ = false;

  /* The binlog name up to which we have received replies from any slaves. */
  char reply_file_name_[FN_REFLEN];

  /* The position in that file up to which we have the reply from any slaves. */
  my_off_t reply_file_pos_ = 0;

  /* This is set to true when we know the 'smallest' wait position. */
  bool wait_file_name_inited_ = false;

  /* NULL, or the 'smallest' filename that a transaction is waiting for
   * slave replies.
   */
  char wait_file_name_[FN_REFLEN];

  /* The smallest position in that file that a trx is waiting for: the trx
   * can proceed and send an 'ok' to the client when the master has got the
   * reply from the slave indicating that it already got the binlog events.
   */
  my_off_t wait_file_pos_ = 0;

  /* This is set to true when we know the 'largest' transaction commit
   * position in the binlog file.
   * We always maintain the position no matter whether semi-sync is switched
   * on switched off.  When a transaction wait timeout occurs, semi-sync will
   * switch off.  Binlog-dump thread can use the three fields to detect when
   * slaves catch up on replication so that semi-sync can switch on again.
   */
  bool commit_file_name_inited_ = false;

  /* The 'largest' binlog filename that a commit transaction is seeing.       */
  char commit_file_name_[FN_REFLEN];

  /* The 'largest' position in that file that a commit transaction is seeing. */
  my_off_t commit_file_pos_ = 0;

  /* All global variables which can be set by parameters. */
  volatile bool master_enabled_ =
      false;                       /* semi-sync is enabled on the master */
  unsigned long wait_timeout_ = 0; /* timeout period(ms) during tranx wait */

  bool state_ = 0; /* whether semi-sync is switched */
  char wait_no_slave_;
  unsigned int wait_for_slave_count_;

  /* reserve all the server info of all the groups */
  SlaveServerInfo server_info_;
  /* stat all the group acks when one group receive all the need acks */
  AckContainer ack_container_;
  /* stat the acks in one group */
  AckContainer ack_group_container_[MAX_RPL_GROUP_NUM];
  /* store the ack_container of master_group in lwm */
  AckContainer lwm_master_group_ack_container_;

  void lock();
  void unlock();

  /* Is semi-sync replication on? */
  bool is_on() { return (state_); }

  void set_master_enabled(bool enabled) { master_enabled_ = enabled; }

  /* Switch semi-sync off because of timeout in transaction waiting. */
  int switch_off();

  void force_switch_on();

  /* Switch semi-sync on when slaves catch up. */
  int try_switch_on(const char *log_file_name, my_off_t log_file_pos);

 public:
  ReplSemiSyncMaster();
  ~ReplSemiSyncMaster();
  
  void set_wait_no_slave_inline(char wait_no_slave) {
    wait_no_slave_ = wait_no_slave;
  }
  char get_wait_no_slave_inline() {
    return wait_no_slave_;
  }

  void set_wait_slave_count_inline(unsigned int wait_slave_count) {
    wait_for_slave_count_ = wait_slave_count;
  }
  unsigned int get_wait_slave_count_inline() {
    return wait_for_slave_count_;
  }

  bool getMasterEnabled() { return master_enabled_; }
  void setTraceLevel(unsigned long trace_level) {
    trace_level_ = trace_level;
    ack_container_.trace_level_ = trace_level;
    if (active_tranxs_) active_tranxs_->trace_level_ = trace_level;
    for(unsigned int i=0; i<MAX_RPL_GROUP_NUM; i++)
      ack_group_container_[i].trace_level_ = trace_level;
    lwm_master_group_ack_container_.trace_level_ = trace_level;
  }

  /* Set if the master has to wait for an ack from the salve or not. */
  void set_wait_no_slave(char val);

  /* Set the transaction wait timeout period, in milliseconds. */
  void setWaitTimeout(unsigned long wait_timeout) {
    wait_timeout_ = wait_timeout;
  }

  /* Initialize this class after MySQL parameters are initialized. this
   * function should be called once at bootstrap time.
   */
  int initObject();
  
  int deinit();

  /* Enable the object to enable semi-sync replication inside the master. */
  int enableMaster();

  /* Enable the object to enable semi-sync replication inside the master. */
  int disableMaster();

  /* Add a semi-sync replication slave */
  void add_slave(int server_id);

  /* Remove a semi-sync replication slave */
  void remove_slave(int server_id);

  /* Is the slave servered by the thread requested semi-sync */
  bool is_semi_sync_slave();

  /* It parses a reply packet and call reportReplyBinlog to handle it. */
  int reportReplyPacket(uint32 server_id, const uchar *packet,
                        ulong packet_len);

  /* In semi-sync replication, reports up to which binlog position we have
   * received replies from the slave indicating that it already get the events
   * or that was skipped in the master.
   *
   * Input:
   *  log_file_name - (IN)  binlog file name
   *  end_offset    - (IN)  the offset in the binlog file up to which we have
   *                        the replies from the slave or that was skipped
   */
  void reportReplyBinlog(const char *log_file_name, my_off_t end_offset);

  /* Commit a transaction in the final step.  This function is called from
   * InnoDB before returning from the low commit.  If semi-sync is switch on,
   * the function will wait to see whether binlog-dump thread get the reply for
   * the events of the transaction.  Remember that this is not a direct wait,
   * instead, it waits to see whether the binlog-dump thread has reached the
   * point.  If the wait times out, semi-sync status will be switched off and
   * all other transaction would not wait either.
   *
   * Input:  (the transaction events' ending binlog position)
   *  trx_wait_binlog_name - (IN)  ending position's file name
   *  trx_wait_binlog_pos  - (IN)  ending position's file offset
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int commitTrx(const char *trx_wait_binlog_name, my_off_t trx_wait_binlog_pos);
  int quick_sync_wait_commitTrx(THD *thd, const char* trx_wait_binlog_name,
                                my_off_t trx_wait_binlog_pos);
  int quick_sync_commitTrx(THD *thd, const char* trx_wait_binlog_name,
                           my_off_t trx_wait_binlog_pos);

  /* Reserve space in the replication event packet header:
   *  . slave semi-sync off: 1 byte - (0)
   *  . slave semi-sync on:  3 byte - (0, 0xef, 0/1}
   *
   * Input:
   *  header   - (IN)  the header buffer
   *  size     - (IN)  size of the header buffer
   *
   * Return:
   *  size of the bytes reserved for header
   */
  int reserveSyncHeader(unsigned char *header, unsigned long size);

  /* Update the sync bit in the packet header to indicate to the slave whether
   * the master will wait for the reply of the event.  If semi-sync is switched
   * off and we detect that the slave is catching up, we switch semi-sync on.
   *
   * Input:
   *  packet        - (IN)  the packet containing the replication event
   *  log_file_name - (IN)  the event ending position's file name
   *  log_file_pos  - (IN)  the event ending position's file offset
   *  server_id     - (IN)  master server id number
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int updateSyncHeader(unsigned char *packet, const char *log_file_name,
                       my_off_t log_file_pos, uint32 server_id);

  /* Called when a transaction finished writing binlog events.
   *  . update the 'largest' transactions' binlog event position
   *  . insert the ending position in the active transaction list if
   *    semi-sync is on
   *
   * Input:  (the transaction events' ending binlog position)
   *  log_file_name - (IN)  transaction ending position's file name
   *  log_file_pos  - (IN)  transaction ending position's file offset
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int writeTranxInBinlog(const char *log_file_name, my_off_t log_file_pos);

  /* Read the slave's reply so that we know how much progress the slave makes
   * on receive replication events.
   *
   * Input:
   *  net          - (IN)  the connection to master
   *  event_buf    - (IN)  pointer to the event packet
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int readSlaveReply(NET *net, const char *event_buf);

  /* In semi-sync replication, this method simulates the reception of
   * an reply and executes reportReplyBinlog directly when a transaction
   * is skipped in the master.
   *
   * Input:
   *  event_buf     - (IN)  pointer to the event packet
   *  server_id     - (IN)  master server id numbe
   *  log_file_name - (IN)  the event ending position's file name
   *  log_file_pos  - (IN)  the event ending position's file offset
   *
   * Return:
   *  0: success;  non-zero: error
   */
  int skipSlaveReply(const char *event_buf, uint32 server_id,
                     const char *log_file_name, my_off_t log_file_pos);

  /* Export internal statistics for semi-sync replication. */
  void setExportStats();

  /* 'reset master' command is issued from the user and semi-sync need to
   * go off for that.
   */
  int resetMaster();

  /*
    'SET rpl_semi_sync_master_wait_for_slave_count' command is issued from user
    and semi-sync need to update rpl_semi_sync_master_wait_for_slave_count and
    notify ack_container_ to resize itself.

    @param[in] new_value The value users want to set to.

    @return It returns 0 if succeeds, otherwise 1 is returned.
   */
  int setWaitSlaveCount(unsigned int new_value);
  
  int set_quick_sync_enabled(char quick_sync_enabled);

  bool init_wait_cond();

  void set_curr_stat_master_flag(char flag);

  bool set_wait_cond_hwm(char *strPos);
  
  bool set_wait_cond_lwm(char *strPos);
  
  bool parse_wait_cond(char *strPos, uint *wait_count, char *with_master);

  char *check_and_get_colon_pos(char *strPos);
  
  bool check_wait_cond(unsigned int wait_count_hwm,char with_master_hwm,
                               unsigned int wait_count_lwm, char with_master_lwm);
  
  int set_wait_cond_hwm_val(unsigned int wait_count,char with_master);
  
  int set_wait_cond_lwm_val(unsigned int wait_count,char with_master);

  unsigned int get_actually_wait_count(unsigned int wait_count);

  int set_current_wait_group_count(unsigned int new_value);
  
  int set_without_master_group_container(unsigned int group_id, unsigned int wait_num);

  int set_master_group_container();

  int set_hwm_master_group_container(unsigned int group_id, unsigned int wait_num);

  int set_lwm_master_group_container(unsigned int wait_num);

  int set_err_flag_and_stat_flag(bool is_init);
  
  int set_err_flag_below_hwm();
  
  
  /*
    'SET rpl_semi_sync_master_group*' command is issued from user
    and semi-sync need to update rpl_semi_sync_master_group* and
    notify to update the server_info_.

    @param[in] strPos the string of rpl_semi_sync_master_group* needed parsed.
    @param[in] group_id the group id of the strPos.
    @return returns true if succeeds, otherwise false is returned.
   */
  bool set_ack_group_info(char *strPos, unsigned int group_id);
  
  /*
    parse all the rpl_semi_sync_master_group* string, any one of them false,
    break and return false.
    
    @return returns true if succeeds, otherwise false is returned.
  */
  bool set_server_info();

  /*
    parse the rpl_semi_sync_master_group*, the format is:
    rpl_semi_sync_master_group* = id1,id2,id3:num
    
    @param[in] group_info the group info.
    @param[in] strPos     the string of rpl_semi_sync_master_group* needed parsed.
    @return returns true if succeeds, otherwise false is returned.
   */
  bool parse_one_group_info(GroupInfo *group_info, char *strPos);

  char* parse_server_id_list(GroupInfo *group_info, char *str_pos);

  bool parse_wait_ack_num(GroupInfo *group_info, char *str_pos);

  /*
    check the group_info is legal
    
    @param[in] group_info  the group info.
    @param[in] group_id    the group id of the strPos.
    @return returns true if legal, otherwise false is illegal.
   */
  bool check_one_group_info(GroupInfo *group_info, unsigned int group_id);

  int set_one_group_status(GroupInfo *group_info);
  
  /**
     Insert an ack's information into the ack_container_ and report the minimum
     ack to semisync master if it is full.

     @param[in] server_id  slave server_id of the ack
     @param[in] group_id   the group_id of the slave_id in cfg
     @param[in] log_file_name  binlog file name of the ack
     @param[in] log_file_pos   binlog file position of the ack
  */
  int insert_without_master_group(unsigned int server_id, unsigned int group_id,
                                              const char *log_file_name, my_off_t log_file_pos);
  
  int insert_hwm_master_group(unsigned int server_id, unsigned int group_id,
                                          const char *log_file_name, my_off_t log_file_pos);

  int insert_lwm_master_group(unsigned int server_id, unsigned int group_id,
                                         const char *log_file_name, my_off_t log_file_pos);

  int insert_group(unsigned int server_id, unsigned int group_id,
                        const char *log_file_name, my_off_t log_file_pos);

  /*
    the current_wait_groups may be changed 
    @return returns 0 if legal, otherwise is illegal.
  */
  int proc_active_groups_changed();

  /*
    judge the group timeout by the input server_id.
    case 1: if the err_flag is 0, ignore whether the group is timeout.
    @return returns true if timeout, otherwise is not timeout.
  */
  bool is_group_timeout(uint32 server_id);

  /*
    judge if quick-sync need to wait when there are not enough slaves.
    @return returns true is need wait, otherwise is false.
  */
  bool is_need_wait_no_slave();

  /*
    if has timeout groups, set these groups timeout flag.
    @param[in] node  the timeout transnode with thd.
    @return returns 0 if legal, otherwise is illegal.
  */
  int set_timeout_groups(TranxNode *node);

  int proc_timeout_group(unsigned int group_id);

  int print_timeout_serverid(unsigned int group_id, TranxNode *node);

  int switch_master_group_container_to_lwm();

  int switch_master_group_container_to_hwm();

  /**
     received the ack in timeout groups will compare with the commit_file_pos,
     if catch up the pos will switch the group from timeout to active.
     @param[in] group_id   the timeout group_id by the ack
     @param[in] log_file_name  binlog file name of the ack
     @param[in] log_file_pos   binlog file position of the ack
  */
  bool try_switch_group_active(unsigned int groupid,
                                       const char *log_file_name,
                                       my_off_t log_file_pos);

  bool try_switch_lwm_master_group_active(unsigned int groupid,
                                                          const char *log_file_name,
                                                          my_off_t log_file_pos);

  int  inspection_transactions();
  
  bool add_thd_to_tranx_node(TranxNode *trx, THD *leader_thd);

  /*
    Update ack_array after receiving an ack from a dump connection. If any
    binlog pos is already replied by rpl_semi_sync_master_wait_for_slave_count
    slaves, it will call reportReplyBinlog to increase received binlog
    position and wake up waiting transactions. It acquires LOCK_binlog_
    to protect the operation.
    there are 5 cases for quick semi_smyc
    case 1: the server is not in any groups, discard the received ack.
    case 2: the group need one ack and also need one ok group, use the received ack report.
    case 3: the group need one ack but need many ok groups, insert the ack_container_, and if full
            use the ackinfo of ack_container_ report.
    case 4: the group need many acks and alse need one ok group, insert the ack_group_container_,
            and if full use the ackinfo of ack_group_container_ report.
    case 5: the group need many acks and alse need many ok groups, insert the ack_group_container_,
            and if full then insert the ack_container_, if full of ack_container_, use the ackinfo 
            of ack_group_container_ report.

    @param[in] server_id  slave server_id of the ack
    @param[in] log_file_name  binlog file name of the ack
    @param[in] log_file_pos   binlog file position of the ack
  */
  void handleAck(int server_id, const char *log_file_name,
                 my_off_t log_file_pos) {
    const AckInfo *ackinfo= NULL;
    unsigned int group_id = 0;
    lock();
    if(rpl_semi_sync_master_quick_sync_enabled)
    {
      /* quick-sync mode */
      group_id = server_info_.get_group_id(server_id);
      if(0 == group_id)
        goto l_end;
      if(server_info_.is_master_group(group_id))
      {
        handle_ack_hwm_master_group(server_id, group_id, log_file_name, log_file_pos);
        handle_ack_lwm_master_group(server_id, group_id, log_file_name, log_file_pos);
      }
      else
      {
        handle_ack_without_master_group(server_id, group_id, log_file_name, log_file_pos);
      }
    }
    else
    {
      /* semi-sync mode */
      if (get_wait_slave_count_inline() == 1)
        reportReplyBinlog(log_file_name, log_file_pos);
      else
      {
        ackinfo= ack_container_.insert(server_id, log_file_name, log_file_pos);
        if (ackinfo != NULL)
          reportReplyBinlog(ackinfo->binlog_name, ackinfo->binlog_pos);
      }
    }
    
l_end:    
    unlock();
    return;
  }
  
  void handle_ack_without_master_group(unsigned int server_id, unsigned int group_id, 
                                       const char *log_file_name, my_off_t log_file_pos)
  {
    const AckInfo *ackinfo= NULL;

    if(0 == server_info_.get_curr_wait_ack_num(group_id))
      return;
    
    if(1 == server_info_.get_curr_wait_ack_num(group_id))
    {
      insert_without_master_group(server_id, group_id, log_file_name, log_file_pos);
    }
    else
    {
      ackinfo = ack_group_container_[group_id-1].insert_quick_sync(server_id, group_id, log_file_name, log_file_pos);
      if(ackinfo)
        insert_without_master_group(ackinfo->server_id, ackinfo->group_id, ackinfo->binlog_name, ackinfo->binlog_pos);
    }
 
    return;
  }

  void handle_ack_hwm_master_group(unsigned int server_id, unsigned int group_id, 
                                   const char *log_file_name, my_off_t log_file_pos)
  {
    const AckInfo *ackinfo= NULL;

    if(0 == server_info_.get_curr_wait_ack_num(group_id))
      return;

    if(rpl_semi_sync_master_err_flag &&
       GROUP_TIMEOUT != server_info_.group_info[group_id-1].active_status)
      return;
    
    if(1 == server_info_.get_curr_wait_ack_num(group_id))
    {
      insert_hwm_master_group(server_id, group_id, log_file_name, log_file_pos);
    }
    else
    {
      ackinfo = ack_group_container_[group_id-1].insert_quick_sync(server_id, group_id, log_file_name, log_file_pos);
      if(ackinfo)
        insert_hwm_master_group(ackinfo->server_id, ackinfo->group_id, ackinfo->binlog_name, ackinfo->binlog_pos);
    }
 
    return;
  }

  void handle_ack_lwm_master_group(unsigned int server_id, unsigned int group_id, 
                                   const char *log_file_name, my_off_t log_file_pos)
  {
    const AckInfo *ackinfo= NULL;

    if(0 == server_info_.get_lwm_curr_wait_ack_num())
      return;

    if(!rpl_semi_sync_master_err_flag &&
       GROUP_TIMEOUT != server_info_.lwm_master_group_info.active_status)
      return;
    
    if(1 == server_info_.get_lwm_curr_wait_ack_num())
    {
      insert_lwm_master_group(server_id, group_id, log_file_name, log_file_pos);
    }
    else
    {
      ackinfo = lwm_master_group_ack_container_.insert_quick_sync(server_id, group_id, log_file_name, log_file_pos);
      if(ackinfo)
        insert_lwm_master_group(ackinfo->server_id, ackinfo->group_id, ackinfo->binlog_name, ackinfo->binlog_pos);
    }
 
    return;
  }
    
};

/* System and status variables for the master component */
extern bool rpl_semi_sync_master_enabled;
extern char rpl_semi_sync_master_status;
extern unsigned long rpl_semi_sync_master_clients;
extern unsigned long rpl_semi_sync_master_timeout;
extern unsigned long rpl_semi_sync_master_trace_level;
extern unsigned long rpl_semi_sync_master_yes_transactions;
extern unsigned long rpl_semi_sync_master_no_transactions;
extern unsigned long rpl_semi_sync_master_off_times;
extern unsigned long rpl_semi_sync_master_wait_timeouts;
extern unsigned long rpl_semi_sync_master_timefunc_fails;
extern unsigned long rpl_semi_sync_master_num_timeouts;
extern unsigned long rpl_semi_sync_master_wait_sessions;
extern unsigned long rpl_semi_sync_master_wait_pos_backtraverse;
extern unsigned long rpl_semi_sync_master_avg_trx_wait_time;
extern unsigned long rpl_semi_sync_master_avg_net_wait_time;
extern unsigned long long rpl_semi_sync_master_net_wait_num;
extern unsigned long long rpl_semi_sync_master_trx_wait_num;
extern unsigned long long rpl_semi_sync_master_net_wait_time;
extern unsigned long long rpl_semi_sync_master_trx_wait_time;

/*
  This indicates whether we should keep waiting if no semi-sync slave
  is available.
     0           : stop waiting if detected no avaialable semi-sync slave.
     1 (default) : keep waiting until timeout even no available semi-sync slave.
*/
extern bool rpl_semi_sync_master_wait_no_slave;
#endif /* RPL_SEMISYNC_MASTER_H */
