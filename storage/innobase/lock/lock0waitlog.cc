/*****************************************************************************

Copyright (c) 1996, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file lock/lock0wait.cc
The transaction lock system

Created 25/5/2010 Sunny Bains
*******************************************************/

#include "zsql_features.h"

#ifdef HAVE_ZSQL_TSN_AND_LOCK_WAIT

#include <log.h>
#include <sql_class.h>
#include <mysql/service_thd_alloc.h>
#include <mysql/plugin.h>
//#include <atomic_class.h>

#define LOCK_MODULE_IMPLEMENTATION

#include <mysql/service_thd_engine_lock.h>
#include "ha_prototypes.h"
#include "lock0iter.h"
#include "lock0lock.h"
#include "lock0priv.h"

#ifdef UNIV_NONINL
#include "lock0lock.ic"
#include "lock0priv.ic"
#endif

#include "dict0mem.h"
#include "usr0sess.h"
#include "trx0purge.h"
#include "trx0sys.h"
#include "srv0mon.h"
#include "ut0vec.h"
#include "btr0btr.h"
#include "dict0boot.h"
#include "ut0new.h"
#include "row0sel.h"
#include "row0mysql.h"
#include "pars0pars.h"

#include "srv0start.h"


#include "ut0mem.h"

using std::min;
using std::max;

/*********************************************************************
* global variables                                                   *
**********************************************************************/

LOCK_WAIT_SLOT_CACHE *lw_slot_cache;
OSMutex *lw_log_mutex = NULL;

bool lock_wait_snapshot_thread_active = false;
bool lock_wait_cache_initialized = false;


/*********************************************************************
* local const and macro declare                                      *
**********************************************************************/

/* 26 for timestamp when using micro-seconds, plus 2 "[]" */
#define INNODB_ISO8601_SIZE 28

#define REQ_FIELD_NAME_LEN (sizeof "req_thd_id:" + \
                            sizeof ", req_trx_id:" + \
                            sizeof ", req_trx_seq:" + \
                            sizeof ", req_gtm_gtid:" + \
                            sizeof ", req_sql:[],")

#define BLK_FIELD_NAME_LEN (sizeof "blk_thd_id:" + \
                            sizeof ", blk_trx_id:" + \
                            sizeof ", blk_trx_seq:" + \
                            sizeof ", blk_gtm_gtid:" + \
                            sizeof ", blk_key_data:[]")

#define SEPARATOR_LEN (sizeof("||||"))

#define LONGLONG_LEN 21

#define MAX_LOCK_WAIT_TIME_LEN \
  (sizeof "#WARN:DESC=lock_wait_time:more than  ms,\n" + 4)


#define FIRST_LOCK_WAIT_LEN (INNODB_ISO8601_SIZE + SEPARATOR_LEN + \
                             MAX_TSN_LEN + SEPARATOR_LEN + \
                             MAX_LOCK_WAIT_TIME_LEN + 1)

#define SECOND_LOCK_WAIT_LEN (REQ_FIELD_NAME_LEN + LONGLONG_LEN + \
                              LONGLONG_LEN + MAX_TSN_LEN + \
                              MAX_GTID_LEN + MAX_REQ_LOCK_SQL_LEN + 1)

#define THIRD_LOCK_WAIT_LEN (BLK_FIELD_NAME_LEN + LONGLONG_LEN + \
                             LONGLONG_LEN + MAX_TSN_LEN + \
                             MAX_GTID_LEN + MAX_BLK_KEY_DATA_LEN +1 +1)

#define LOCK_WAIT_SLOT_BUF_LEN (FIRST_LOCK_WAIT_LEN + \
                                SECOND_LOCK_WAIT_LEN + \
                                THIRD_LOCK_WAIT_LEN + 1)


#define LOCK_WAIT_SLOT_COUNT   1024

#define TRX_INFO_COUNT_IN_SLOT 128

#define LOCK_WAIT_LOG_NAME_MAX_LENGTH 512

//#define LOCK_WAIT_COLLECT_TIMEOUT (min<uint>((2 * srv_lock_wait_collect_time),2000))
#define LOCK_WAIT_COLLECT_TIMEOUT (long unsigned int)2000

/*********************************************************************
* local variable declare                                             *
**********************************************************************/
File  lock_wait_log_fd = -1;

const char *lock_wait_log_name     = "innodb_lock_wait.log";
const char *bak_lock_wait_log_name = "innodb_lock_wait.log.bak";
char lock_wait_log_full_path[512];
char bak_lock_wait_log_full_path[512];


/*********************************************************************
* local function declare                                             *
**********************************************************************/
/**
  Make and return an ISO 8601 / RFC 3339 compliant timestamp.
  Heeds log_timestamps.

  @param buf       A buffer of at least 26 bytes to store the timestamp in
                   (19 + tzinfo tail + \0)
  @param seconds   Seconds since the epoch, or 0 for "now"

  @return          length of timestamp (excluding \0)
*/

static char *
innodb_make_iso8601_timestamp(char *buf, ulonglong utime= 0);


/** write lock_wait_slot to file innodb_lock_wait.log
@param[in] lock_wait_slot, slot to write

@return true  write slot  failed
        false write slot  success

@note 1.don't check whether params equal to null, who calls
        this func ensures lock_wait_slot not null
*/
static bool write_lock_wait_slot(LOCK_WAIT_SLOT *lock_wait_slot);


/**
  write innodb rec lock wait snapshot to innodb_lock_wait.log.
  traverse every lock on the page, record the blocking lock on the heap of
  request_lock

  @param[in] request_lock
  @param[in] lw_slot  resquest lock info and timestamp were set by the caller

  @note dont check input NULL, the caller ensure

  @return
*/
static bool make_rec_lock_wait_snapshot(const lock_t *request_lock,
                                        LOCK_WAIT_SLOT *lw_slot);



/** write table lock wait snapshot to innodb_lock_wait.log
@param request_lock  lock to request
@param lw_slot   resquest lock info and timestamp were set by the caller

@return true  failed
        false success

@note 1.don't check whether params equal to null
*/
static bool make_table_lock_wait_snapshot(const lock_t *request_lock,
                                          LOCK_WAIT_SLOT *lw_slot);


/** get first table lock from list src_lock->un_member.tab_lock.locks
@param src_lock must be table lock

@note 1.don't check whether params equal to null
*/
static const lock_t *get_first_table_lock(const lock_t *src_lock);


/** open or create innodb_lock_wait.log and lseek to the end
**/
static bool init_lock_wait_log(void);


/** write request trx info from lock_wait_slot to buf
@param[in] lock_wait_slot lock wait info stores request trx and block trx vector
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_req_trx_info(LOCK_WAIT_SLOT *lock_wait_slot,
                               char *buf, char *buf_end);


/** write block trxes info from lock_wait_slot to buf
@param[in] lock_wait_slot lock wait info stores request trx and block trx vector
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
     2.ensure don't write mem over buf_end
*/
static void make_blk_trx_infos(LOCK_WAIT_SLOT *lock_wait_slot, char *buf_start,
                               char *buf_free, char *buf_end);

/** write buffer to file innodb_lock_wait.log
@param[in] buf_start  the buffer start
@param[in] buf_pos    the buffer end

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static void write_lock_wait_log(char *buf_start, char *buf_pos);


/** write blk trx info from lock_wait_slot.block_trxex to buf
@param[in]  trx_info block trx info
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
     2.ensure don't write mem over buf_end
*/
static char *make_blk_trx_info(TRX_INFO *trx_info, char *buf, char *buf_end);


/** get innodb_lock_wait.log file size */
static ulonglong get_log_file_size();

/** if open innodb_lock_wait.log failed, reopen it */
static bool is_need_reopen_log_file();

/** if size of innodb_lock_wait.log is over 100M, move it to
 innodb_lock_wait.log.bak */
static bool is_need_clear_log_file();

/**
set trx info by lock
@param[in]  src_lock  innodb lock
@param[out] trx_info  trx_info to set
@param[in]  is_request TRUE means the lock is request lock, FLASE meas block lock
@param[in]  heap_no   heap_no for wait and waited locks
@return true  set trx info failed
        false set trx info success

@note 1.don't check whether params equal to null
      2.don't report error event if trans_serial_num is truncated
*/
static void set_trx_info(const lock_t *src_lock, TRX_INFO *trx_info,
                         bool is_request, ulint heap_no=ULINT_UNDEFINED);


/** get free trx info from slot->block_trxes
@param[in]  lw_slot lock wait slot to store lock wait info

@note 1.don't check whether params equal to null, the caller ensure
*/
static TRX_INFO *get_free_trx_info_frm_lw_slot(LOCK_WAIT_SLOT *lw_slot);


/** free memory of vecotor lock_wait_slots
@param[in]  lw_slots

@note 1.don't check whether params equal to null
*/
static void free_lock_wait_slots(lock_wait_slots *lw_slots);


/** free memory of vecotor lock_wait_slots
@param[in]  vec_trx_infos

@note 1.don't check whether params equal to null
*/
static void free_trx_infos(trx_infos *vec_trx_infos);


/**
@return true  failed
        false success
*/
static bool init_lock_wait_slot_cache();


/** init of vecotor lock_wait_slots
@param[in|out]  lw_slots

@note 1.don't check whether params equal to null
*/
static bool init_lock_wait_slots(lock_wait_slots *lw_slots);


/**
init of vecotor vec_trx_infos
@param[in|out]  vec_trx_infos

@note 1.don't check whether params equal to null
*/
static bool init_trx_infos(trx_infos* vec_trx_infos);


/** mv innodb_lock_wait.log to innodb_lock_wait.log.bak if need and
then reopen innodb_lock_wait.log.

@return false success;true error.
*/
static bool clear_log_file_if_need();


static void wraper_write_lock_wait_slots();


static LOCK_WAIT_SLOT *get_lock_wait_slot_from_cache();


/** write all lock wait info in lw_slots
@param[in] lw_slots vector to store lock wait trx info
@param[in] elements how many elements in lw_slots

@note don't check whether params equal to null, the caller ensure
*/
static bool write_lock_wait_slots(lock_wait_slots *lw_slots,
                                  uint element_count);


/**
@note  1. notice snapshot thread to write ls_slot infomation
       2. dont check param whther is NULL
*/
static void notify_snapshot_thread_to_write(LOCK_WAIT_SLOT *lw_slot);


/**
whether the slot info is ready by app thread. if so snapshot thread can
write the lw_slot info
@note dont check param wether is NULL
*/
static bool is_lock_wait_slot_ready(LOCK_WAIT_SLOT *lw_slot);


/**
@note 1.dont check return value wthether is NULL
      2.the caller must acquire lw_slot_cache.cache_mutex
*/
static lock_wait_slots *get_current_lw_slots(void);


/**
create a new LOCK_WAIT_SLOT and init LOCK_WAIT_SLOT::block_trxes
@return NULL create fail
*/
static LOCK_WAIT_SLOT *create_new_lw_slot();


static void set_trans_serial_num(THD *thd, char *buf, size_t buflen);

static void set_trans_gtm_gtid(THD *thd, char *buf, size_t buflen);

static void set_zero_string(char *buf);

static void reset_string(char *buf);

/** free memory of global lw_slot_cache */
static void free_lw_slot_cache(void);

static bool init_lw_log_mutex();

static bool init_lw_slot_cache();

static void wait_lw_log_snapshot_thread_quited(void);

static void destroy_lw_log_mutex();

static bool close_lw_log_file();

static bool enter_lw_log_mutex();

static bool exit_lw_log_mutex();

static bool switch_lock_wait_slots(void);

static bool is_lw_slot_cache_empty(void);

static uint get_current_slot_count(void);

static void finish_lock_wait_log_file(void);

static bool create_global_slot_cache();

static bool create_os_event_for_cache(LOCK_WAIT_SLOT_CACHE *slot_cache);

static bool create_lw_slots_for_cache(LOCK_WAIT_SLOT_CACHE *slot_cache);

static bool need_write_lock_wait(LOCK_WAIT_SLOT *lw_slot);

static bool is_lock_wait_too_long(LOCK_WAIT_SLOT *lw_slot, ulint microsecs);

static bool is_lock_wait_too_long(LOCK_WAIT_SLOT *lw_slot);

static ulint get_lock_wait_time_so_far(LOCK_WAIT_SLOT *lw_slot);

static void reset_lw_slot(LOCK_WAIT_SLOT *lw_slot);


/**
 return wait time in us
*/
static ulint get_lock_wait_time(LOCK_WAIT_SLOT *lw_slot);


/** write separator format to buf
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_separator_format(char *buf, char *buf_end);

/** write request trx serial number to buf
@param[in] lock_wait_slot lock wait info stores request trx
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_req_trx_serial_num(LOCK_WAIT_SLOT *lock_wait_slot,
                                     char *buf, char *buf_end);

/** write request trx wait time to buf
@param[in] lock_wait_slot lock wait info stores request trx and block trx vector
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_log_wait_time(LOCK_WAIT_SLOT *lock_wait_slot,
                                char *buf, char *buf_end);


/** Get the rec lock_data by the index.
@param lock_data  the index rec buffer
@param data_size  the buffer size
@param lock       lock used to find the data
@param heap_no    heap_no for wait and waited locks
@return void
*/
static void lock_rec_print_key_data(char* lock_data, size_t data_size,
                                    const lock_t* lock, ulint heap_no);

/**
This function is copy from put_nth_field in trx0i_s.cc.
Format the nth field of "rec" and put it in "buf". The result is always
NUL-terminated. Returns the number of bytes that were written to "buf"
(including the terminating NUL).
@return end of the result
*/
static ulint put_nth_field_for_keydata(
    char*               buf,       /*!< out: buffer */
    ulint               buf_size,  /*!< in: buffer size in bytes */
    ulint               n,         /*!< in: number of field */
    const dict_index_t* index,     /*!< in: index */
    const rec_t*        rec,       /*!< in: record */
    const ulint*        offsets);  /*!< in: record offsets, returned
                                    by rec_get_offsets() */


/***********************************************************************
* global function ion                                                  *
***********************************************************************/

/** open file nnodb_lock_wait.log
@retrun  false, init success
         true,  init error
@note    windows support open?
*/
bool lock_wait_snapshot_init(void)
{
  if (init_lock_wait_log())
  {
    return true;
  }

  if (init_lock_wait_slot_cache())
  {
    close_lw_log_file();
    return true;
  }

  lock_wait_cache_initialized = true;

  return false;
}


/**
@note   lock_wait_snapshort_write thread entry.
@return a dummy parameter
*/
void lock_wait_snapshort_write()
{
  if (!lock_wait_cache_initialized)
  {
    sql_print_error("lock_wait_slot_cache wasn't initialized, " \
                    "exit lock wait snapshot thread");
    //my_thread_exit(0);
    return;
  }

  lock_wait_snapshot_thread_active = true;

  do {
    os_event_wait_time(lw_slot_cache->start_collect, 1000000);

    clear_log_file_if_need();

    wraper_write_lock_wait_slots();

    /* actually, when only all threads which called os_event_set where executed,
       it should reset event. But We only wake it up when shutdown, dont do that
    */
    os_event_reset(lw_slot_cache->start_collect);
  } while (srv_shutdown_state.load() == SRV_SHUTDOWN_NONE);

  /** at this phase all connections were closed */
  finish_lock_wait_log_file();

  lock_wait_snapshot_thread_active = false;

  //my_thread_exit(0);
}


/** write lock wait snapshot to innodb_lock_wait.log
@param request_lock, lock to request
@param block_lock,   lock blocking request_lock
@param current_utime, the time when innodb produced lock conflict

@return true  set slot  failed
        false set slot  success

@note 1.don't check whether params equal to null
      2.don't report error event if trans_serial_num is truncated
      3.this func always return false, the caller doesn't process error
      4.snapshot thread must wait app thread set slot info finished, but
        app thread dosen't need wait snapshot write slot info finished, because
        if snapshot dosent' finish, the slot snapshot thread was writing was not
        obtained by app thread
*/
LOCK_WAIT_SLOT *make_lock_wait_snapshot(const lock_t *request_lock,
                                        ib_time_monotonic_us_t current_utime)
{
  LOCK_WAIT_SLOT *lw_slot = NULL;

  if (!lock_wait_cache_initialized)
    return NULL;

  if (!request_lock)
    return NULL;

  lw_slot = get_lock_wait_slot_from_cache();
  if (!lw_slot)
    return NULL;

  (void)reset_lw_slot(lw_slot);

  lw_slot->timestamp = current_utime;

  (void)set_trx_info(request_lock, lw_slot->request_trx, TRUE);

  if (LOCK_REC == lock_get_type_low(request_lock))
  {
    make_rec_lock_wait_snapshot(request_lock, lw_slot);
  }
  else
  {
    make_table_lock_wait_snapshot(request_lock, lw_slot);
  }

  (void)notify_snapshot_thread_to_write(lw_slot);

  return lw_slot;
}


void set_lock_wait_time(LOCK_WAIT_SLOT *lw_slot, ulint wait_time)
{
  if (!lw_slot)
    return;

  lw_slot->wait_time = (int64) wait_time;

  return;
}


/** called by main thread when shutdown */
void destroy_lw_log_cahe(void)
{
  if (!lock_wait_cache_initialized)
    return ;

  lock_wait_cache_initialized = false;

  enter_lw_log_mutex();

  wait_lw_log_snapshot_thread_quited();

  free_lw_slot_cache();

  exit_lw_log_mutex();

  destroy_lw_log_mutex();

  return;
}


/** local function definition */
static void destroy_lw_log_mutex()
{
  if (!lw_log_mutex)
    return;

  lw_log_mutex->destroy();
  delete lw_log_mutex;
  lw_log_mutex = NULL;
}


/** free memory of global lw_slot_cache */
static void free_lw_slot_cache(void)
{
  if (!lw_slot_cache)
    return;

  os_event_destroy(lw_slot_cache->start_collect);
  free_lock_wait_slots(lw_slot_cache->first_lock_wait_slots);
  free_lock_wait_slots(lw_slot_cache->second_lock_wait_slots);
  delete lw_slot_cache->first_lock_wait_slots;  //delete null is safe
  delete lw_slot_cache->second_lock_wait_slots; //delete null is safe
  delete lw_slot_cache;
  lw_slot_cache = NULL;

  return;
}


/**
free memory of vecotor lock_wait_slots
@param[in]  lw_slots

@note 1.don't check whether params equal to null
*/
static void free_lock_wait_slots(lock_wait_slots *lw_slots)
{
  if (!lw_slots)
    return ;

  for (uint i=0; i<lw_slots->size(); i++)
  {
    LOCK_WAIT_SLOT * lw_slot = lw_slots->at(i);

    if (!lw_slot) // after this pos , dosen't create LOCK_WAIT_SLOT
      break;

    free_trx_infos(lw_slot->block_trxes);

    delete lw_slot->block_trxes;
    delete lw_slot->request_trx;
    delete lw_slot;
  }

  lw_slots->clear();
  lock_wait_slots().swap(*lw_slots);
}


/**
free memory of vecotor lock_wait_slots
@param[in]  vec_trx_infos

@note 1.don't check whether params equal to null
*/
static void free_trx_infos(trx_infos *vec_trx_infos)
{
  if (!vec_trx_infos)
    return ;

  for (uint i=0; i<vec_trx_infos->size(); i++)
  {
    TRX_INFO * trx_info = vec_trx_infos->at(i);

    if (!trx_info)  // after this pos, dosen't create TRX_INFO
      break;

    delete trx_info;
  }
  vec_trx_infos->clear();
  trx_infos().swap(*vec_trx_infos);
}


/** called by snapshot thread when was closed */
static void finish_lock_wait_log_file(void)
{
  wraper_write_lock_wait_slots();
  close_lw_log_file();
}


/** open or create innodb_lock_wait.log and lseek to the end */
static bool init_lock_wait_log(void)
{
  int  dirnamelen = 0;
  if (NULL == srv_lock_wait_log_dir)
    return true;

  dirnamelen = strlen(srv_lock_wait_log_dir);
  if (dirnamelen + sizeof(bak_lock_wait_log_name) + 2 >
      LOCK_WAIT_LOG_NAME_MAX_LENGTH)
  {
    sql_print_error("innodb_lock_wait.log direcory is too long");
    return true;
  }

  memcpy(lock_wait_log_full_path, srv_lock_wait_log_dir, dirnamelen);
  /* Add a path separator if needed. */
  if (dirnamelen && lock_wait_log_full_path[dirnamelen - 1] != OS_PATH_SEPARATOR)
  {
    lock_wait_log_full_path[dirnamelen++] = OS_PATH_SEPARATOR;
  }
  memcpy(bak_lock_wait_log_full_path, lock_wait_log_full_path, dirnamelen);

  strcpy(lock_wait_log_full_path + dirnamelen, lock_wait_log_name);         // strcpy??
  strcpy(bak_lock_wait_log_full_path + dirnamelen, bak_lock_wait_log_name); // strcpy??

  lock_wait_log_fd = my_open(lock_wait_log_full_path, O_CREAT|O_RDWR, 0);
  if (-1 == lock_wait_log_fd)
  {
    ib::error() << "open innodb_lock_wait.log fail, "
                << "check file direcory and permission";
    return true;
  }

  if (lseek(lock_wait_log_fd, 0, SEEK_END) < 0)
  {
    ib::error() << "lseek innodb_lock_wait.log to end fail, "
                << "check file direcory and permission";
    close_lw_log_file();
    return true;
  }

  return false;
}


/**
@return true  failed
        false success
*/
static bool init_lock_wait_slot_cache()
{
  if (init_lw_log_mutex())
  {
    return true;
  }

  if (init_lw_slot_cache())
  {
    destroy_lw_log_mutex();
    return true;
  }

  return false;
}


static bool init_lw_log_mutex()
{
  lw_log_mutex = new OSMutex();

  if (!lw_log_mutex)
  {
    ib::error() << "init lock wait log mutex fail, cant alloc memory";
    return true;
  }

  lw_log_mutex->init();

  return false;
}


static bool init_lw_slot_cache()
{
  if (create_global_slot_cache())
    goto ProcErr;

  if (create_os_event_for_cache(lw_slot_cache))
    goto ProcErr;

  if (create_lw_slots_for_cache(lw_slot_cache))
    goto ProcErr;

  if (init_lock_wait_slots(lw_slot_cache->first_lock_wait_slots))
    goto ProcErr;

  if (init_lock_wait_slots(lw_slot_cache->second_lock_wait_slots))
    goto ProcErr;

  return false;

ProcErr:

  ib::error() << "init_lw_slot_cache fail, can't alloc memory";

  free_lw_slot_cache();

  return true;
}


static bool create_global_slot_cache()
{
  lw_slot_cache = new LOCK_WAIT_SLOT_CACHE;

  if (!lw_slot_cache)
    return true;

  return false;
}


static bool create_os_event_for_cache(LOCK_WAIT_SLOT_CACHE *slot_cache)
{
  if (!slot_cache)
    return true;

  slot_cache->start_collect = os_event_create(0);
  if (!slot_cache->start_collect)
    return true;

  os_event_reset(slot_cache->start_collect);

  return false;
}


static bool create_lw_slots_for_cache(LOCK_WAIT_SLOT_CACHE *slot_cache)
{
  if (!slot_cache)
    return true;

  slot_cache->first_lock_wait_slots = new lock_wait_slots;
  if (!slot_cache->first_lock_wait_slots)
    return true;

  slot_cache->second_lock_wait_slots = new lock_wait_slots;
  if (!slot_cache->second_lock_wait_slots)
  {
    delete slot_cache->first_lock_wait_slots;
    slot_cache->first_lock_wait_slots = NULL;
    return true;
  }

  return false;
}


/**
init of vecotor lock_wait_slots
@param[in|out]  lw_slots

@note 1.don't check whether params equal to null
*/
static bool init_lock_wait_slots(lock_wait_slots *lw_slots)
{
  uint pos = 0;
  LOCK_WAIT_SLOT *lw_slot = NULL;

  lw_slots->resize(LOCK_WAIT_SLOT_COUNT);

  for (pos = 0; pos < lw_slots->size(); pos++)
  {
    lw_slot = new LOCK_WAIT_SLOT;

    (*lw_slots)[pos] = lw_slot;

    if (!lw_slot)
      goto ProcErr;

    lw_slot->request_trx = new TRX_INFO;
    lw_slot->block_trxes = new trx_infos;
    if (!lw_slot->request_trx || !lw_slot->block_trxes)
      goto ProcErr;

    if (init_trx_infos(lw_slot->block_trxes))
      goto ProcErr;
  }

  return false;

ProcErr:

  ib::error() << "init_lock_wait_slots fail, can't alloc memory";

  if (lw_slot)
  {
    delete lw_slot->request_trx;
    delete lw_slot->block_trxes;
    delete lw_slot;
  }

  (*lw_slots)[pos] = (LOCK_WAIT_SLOT*)NULL;

  free_lock_wait_slots(lw_slots);

  return true;
}


/**
init of vecotor vec_trx_infos
@param[in|out]  vec_trx_infos

@note 1.don't check whether params equal to null
*/
static bool init_trx_infos(trx_infos* vec_trx_infos)
{
  bool ret = false;
  vec_trx_infos->resize(TRX_INFO_COUNT_IN_SLOT);

  for (uint i=0; i<vec_trx_infos->size(); i++)
  {
    TRX_INFO * trx_info = new TRX_INFO;
    (*vec_trx_infos)[i] = trx_info;

    if (!trx_info)
    {
      ib::error() << "init_trx_infos fail, can't alloc memory";
      ret = true;
      break;
    }
  }

  if (ret)
    free_trx_infos(vec_trx_infos);

  return ret;
}


static void wraper_write_lock_wait_slots()
{
   uint  curr_count = 0;
   lock_wait_slots *lw_slots = NULL;

   if (enter_lw_log_mutex())
     return;

   if (is_lw_slot_cache_empty())
   {
     exit_lw_log_mutex();
     return ;
   }

   lw_slots = get_current_lw_slots();

   curr_count = get_current_slot_count();

   switch_lock_wait_slots();

   exit_lw_log_mutex();

   write_lock_wait_slots(lw_slots, curr_count);

   return;
}


/** write all lock wait info in lw_slots
@param[in] lw_slots vector to store lock wait trx info
@param[in] elements how many elements in lw_slots

@note don't check whether params equal to null, the caller ensure
*/
static bool write_lock_wait_slots(lock_wait_slots *lw_slots,
                                  uint element_count)
{
  uint slot_pos = 0;
  bool finished = false;
  LOCK_WAIT_SLOT *lw_slot = NULL;
  uint valid_count = min<uint>(element_count, lw_slots->size());

  while (!finished)
  {
    finished = true;
    for (; slot_pos < valid_count; slot_pos++)
    {
      lw_slot = lw_slots->at(slot_pos);

      if (!is_lock_wait_slot_ready(lw_slot))
      {
        finished = false;
        break;
      }

      if (!need_write_lock_wait(lw_slot))
      {
        reset_lw_slot(lw_slot);
        continue;
      }

      write_lock_wait_slot(lw_slot);
    }
  }

  return false;
}


/** write lock_wait_slot to file innodb_lock_wait.log.
The one lock_wait format example:
[timestamp]||||req_trx_seq||||#WARN:DESC=lock_wait_time:more than 2000ms,
req_thd_id:2, req_trx_id:47636483, req_trx_seq:0, req_gtm_gtid:0, req_sql:[?],
blk_thd_id:3, blk_trx_id:47636484, blk_trx_seq:0, blk_gtm_gtid:0, blk_key_data:[?]

@param[in] lock_wait_slot, slot to write
@return true  write slot  failed
        false write slot  success

@note 1.don't check whether params equal to null, who calls
        this func ensures lock_wait_slot not null
*/
static bool write_lock_wait_slot(LOCK_WAIT_SLOT *lock_wait_slot)
{
  char *buf_pos = NULL, *buf_end = NULL;
  char lock_wait_buf[LOCK_WAIT_SLOT_BUF_LEN];
  buf_end = lock_wait_buf + LOCK_WAIT_SLOT_BUF_LEN;

  /* "[?]" */
  buf_pos = innodb_make_iso8601_timestamp(lock_wait_buf, lock_wait_slot->timestamp);

  /* "||||" */
  buf_pos = make_separator_format(buf_pos, buf_end);

  /* "?" */
  buf_pos = make_req_trx_serial_num(lock_wait_slot, buf_pos, buf_end);

  /* "||||" */
  buf_pos = make_separator_format(buf_pos, buf_end);

  /* "#WARN:DESC=lock_wait_time:?ms,\n" */
  buf_pos = make_log_wait_time(lock_wait_slot, buf_pos, buf_end);

  /* "req_thd_id:?, req_trx_id:?, req_trx_seq:?, req_gtm_gtid:?, req_sql:[?],\n" */
  buf_pos = make_req_trx_info(lock_wait_slot, buf_pos, buf_end);

  /* "blk_thd_id:?, blk_trx_id:?, blk_trx_seq:?, blk_gtm_gtid:?, blk_key_data:[?]\n" */
  make_blk_trx_infos(lock_wait_slot, lock_wait_buf, buf_pos, buf_end);

  return false;
}


/**
  Make and return an ISO 8601 / RFC 3339 compliant timestamp.
  Heeds log_timestamps.

  @param buf       A buffer of at least 26 bytes to store the timestamp in
                   (19 + tzinfo tail + \0)
  @param seconds   Seconds since the epoch, or 0 for "now"
                   if it's minus ?

  @return          length of timestamp (excluding \0)
*/
static char* innodb_make_iso8601_timestamp(char *buf, ulonglong utime)
{
  struct tm  my_tm;
  size_t     len;
  time_t     seconds;

  if (utime == 0)
  {
    utime= my_micro_time();
  }
  else
  {
    // the input utime is the elasped time after system start, not from 1970.
    ib_time_monotonic_us_t elasped_time = ut_time_monotonic_us() - utime;
    utime = my_micro_time() - elasped_time;
  }

  seconds= utime / 1000000;
  utime = utime % 1000000;

  localtime_r(&seconds, &my_tm);

  len= snprintf(buf, INNODB_ISO8601_SIZE+1,
                   "[%04d-%02d-%02dT%02d:%02d:%02d.%06lu]",
                   my_tm.tm_year + 1900,
                   my_tm.tm_mon  + 1,
                   my_tm.tm_mday,
                   my_tm.tm_hour,
                   my_tm.tm_min,
                   my_tm.tm_sec,
                   (unsigned long) utime);

  return (buf + len);
}


/** write request trx info from lock_wait_slot to buf
@param[in] lock_wait_slot lock wait info stores request trx and block trx vector
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_req_trx_info(LOCK_WAIT_SLOT *lock_wait_slot,
                               char *buf, char *buf_end)
{

  char *buf_pos = buf;
  TRX_INFO *req_trx = lock_wait_slot->request_trx;

  // write requeset trx: thread_id trx_id trx_serial_num
  int req_thread_id_len = snprintf(buf_pos, buf_end - buf_pos,
                                "req_thd_id:%lu", req_trx->thread_id);
  buf_pos += req_thread_id_len;

  int req_trx_id_len = snprintf(buf_pos, buf_end - buf_pos,
                                ", req_trx_id:%lu", req_trx->trx_id);
  buf_pos += req_trx_id_len;

  int req_trx_serial_len = snprintf(buf_pos, buf_end - buf_pos,
                                ", req_trx_seq:%s", req_trx->trx_serial_num);
  buf_pos += req_trx_serial_len;

  int req_trx_gtm_gtid_len = snprintf(buf_pos, buf_end - buf_pos,
                                ", req_gtm_gtid:%s", req_trx->trx_gtm_gtid);
  buf_pos += req_trx_gtm_gtid_len;

  int req_sql_len = snprintf(buf_pos, buf_end - buf_pos,
                                ", req_sql:[%s],", req_trx->sql_string);
  buf_pos += req_sql_len;

  if (buf_pos >= buf_end)
    *(buf_end-1) = '\n';
  else
    *buf_pos++ = '\n';

  return buf_pos;
}


/** write request trx info from lock_wait_slot to buf
@param[in] lock_wait_slot lock wait info stores request trx and block trx vector
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static void make_blk_trx_infos(LOCK_WAIT_SLOT *lock_wait_slot, char *buf_start,
                               char *buf_free, char *buf_end)
{
  uint trx_pos = 0;
  char *buf_pos = buf_free;
  TRX_INFO *trx_info = NULL;
  uint  trx_count = lock_wait_slot->blk_trx_count;
  trx_infos *trx_info_vector = lock_wait_slot->block_trxes;

  /* write the first two line */
  write_lock_wait_log(buf_start, buf_free);

  for (; trx_pos < trx_count; trx_pos++)
  {
    trx_info = trx_info_vector->at(trx_pos);

    buf_pos = make_blk_trx_info(trx_info, buf_free, buf_end);

    if(trx_pos != trx_count-1)
    {
      if (buf_pos < buf_end)
        *buf_pos++ = ',';
    }

    if (buf_pos >= buf_end)
      *(buf_end-1) = '\n';
    else
      *buf_pos++ = '\n';

    /* write the third line or multi-block lines */
    write_lock_wait_log(buf_free, buf_pos);
  }
  lock_wait_slot->blk_trx_count = 0;
}

/** write buffer to file innodb_lock_wait.log
@param[in] buf_start  the buffer start
@param[in] buf_pos    the buffer end

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static void write_lock_wait_log(char *buf_start, char *buf_pos)
{
  if (my_seek(lock_wait_log_fd, 0, MY_SEEK_END, 0) == MY_FILEPOS_ERROR)
  {
    sql_print_error("seek lock_wait log end fail [%d]",errno);
    return;
  }

  if (my_write(lock_wait_log_fd, (const uchar*)buf_start,
               (size_t)(buf_pos - buf_start), 0)
      == MY_FILEPOS_ERROR)
  {
    sql_print_error("seek lock_wait log end fail [%d]", errno);
  }
  return;
}


/** write blk trx info from lock_wait_slot.block_trxes to buf
@param[in]  trx_info block trx info
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
    2.ensure don't write mem over buf_end
*/
static char* make_blk_trx_info(TRX_INFO *trx_info, char *buf, char *buf_end)
{
   char *buf_pos = buf;

   // write block trx: thread_id trx_id trx_serial_num
   int thread_id_len = snprintf(buf_pos, buf_end - buf_pos,
                                 "blk_thd_id:%lu", trx_info->thread_id);
   buf_pos += thread_id_len;

   int trx_id_len = snprintf(buf_pos, buf_end - buf_pos,
                                 ", blk_trx_id:%lu", trx_info->trx_id);
   buf_pos += trx_id_len;

   int trx_serial_len = snprintf(buf_pos, buf_end - buf_pos,
                                 ", blk_trx_seq:%s", trx_info->trx_serial_num);
   buf_pos += trx_serial_len;

   int trx_gtid_len = snprintf(buf_pos, buf_end - buf_pos,
                                 ", blk_gtm_gtid:%s", trx_info->trx_gtm_gtid);
   buf_pos += trx_gtid_len;

   int key_data_len = snprintf(buf_pos, buf_end - buf_pos,
                                 ", blk_key_data:[%s]", trx_info->key_data);
   buf_pos += key_data_len;

   return buf_pos;
}


/**
  write innodb rec lock wait snapshot to innodb_lock_wait.log.
  traverse every lock on the page, record the blocking lock on the heap of request_lock

  @param[in] request_lock  must be rec lock
  @param[in] lw_slot  resquest lock info and timestamp were set by the caller

  @note dont check input NULL, the caller ensure

  @return
*/
static bool make_rec_lock_wait_snapshot(const lock_t *request_lock,
                                        LOCK_WAIT_SLOT *lw_slot)
{
  ulint    heap_no;
  ulint    page_no;
  ulint    space;

  ulint   bit_mask;
  ulint   bit_offset;
  hash_table_t*  hash;
  const lock_t   *curr_lock;
  TRX_INFO *trx_info = NULL;

  heap_no   = lock_rec_find_set_bit(request_lock);
  space = request_lock->space_id();
  page_no = request_lock->page_no();

  bit_offset = heap_no / 8;
  bit_mask = static_cast<ulint>(1 << (heap_no % 8));

  hash = lock_hash_get(request_lock->type_mode);
  curr_lock = (const lock_t*)lock_rec_get_first_on_page_addr(hash, space, page_no);

  for (;curr_lock && curr_lock != request_lock;
       curr_lock = lock_rec_get_next_on_page_const(curr_lock))
  {
    const byte* p = (const byte*) &curr_lock[1];
    if (heap_no < lock_rec_get_n_bits(curr_lock)
        && (p[bit_offset] & bit_mask)  // this condition is important, filter the other locks
        && lock_has_to_wait(request_lock, curr_lock))
    {
      trx_info = get_free_trx_info_frm_lw_slot(lw_slot);

      if (!trx_info)
        return true;                        // conitue maybe better?

      /* heap_no is the same for the wait and waited locks */
      set_trx_info(curr_lock, trx_info, FALSE, heap_no); // conitue maybe better?
    }
  }

  return false;
}


/** write table lock wait snapshot to innodb_lock_wait.log
@param request_lock  lock to request
@param lw_slot   resquest lock info and timestamp were set by the caller

@return true  failed
        false success

@note 1.don't check whether params equal to null
*/
static bool make_table_lock_wait_snapshot(const lock_t *request_lock,
                                          LOCK_WAIT_SLOT *lw_slot)
{
  TRX_INFO *trx_info = NULL;
  const lock_t *curr_lock;
  curr_lock = get_first_table_lock(request_lock);

  for (; curr_lock && curr_lock != request_lock;
       curr_lock = (const lock_t *)UT_LIST_GET_NEXT(tab_lock.locks,
                                                    curr_lock))
  {
    trx_info = get_free_trx_info_frm_lw_slot(lw_slot);

    if (!trx_info)
      return true;

    set_trx_info(curr_lock, trx_info, FALSE);
  }

  return false;
}


/** get first table lock from list src_lock->tab_lock.locks
@param src_lock must be table lock

@note 1.don't check whether params equal to null
*/
static const lock_t *get_first_table_lock(const lock_t *src_lock)
{
  lock_t *cur_lock = (lock_t *)src_lock;
  lock_t *first_lock = (lock_t *)src_lock;

  while (cur_lock)
  {
    first_lock = cur_lock;
    cur_lock = UT_LIST_GET_PREV(tab_lock.locks, cur_lock);
  }

  return (const lock_t *)first_lock;
}


/**
@note
lw_slot_cache is set to NULL when snapshot thread had been quited.
if app threads hasen't ended andh go here, judge whether lw_slot_cache
is NULL

snapshot thread doesn't judge lw_slot_cache when running
*/
static LOCK_WAIT_SLOT *get_lock_wait_slot_from_cache()
{
  LOCK_WAIT_SLOT  *lw_slot = NULL;
  lock_wait_slots *lw_slots = NULL;
  uint  curr_count = 0;
  bool use_first  = true;

  if (enter_lw_log_mutex())
  {
    return NULL;
  }

  if (!lw_slot_cache)
  {
    exit_lw_log_mutex();
    return NULL;
  }

  curr_count = lw_slot_cache->use_count;
  use_first  = lw_slot_cache->first_in_use;

  if (use_first)
    lw_slots = lw_slot_cache->first_lock_wait_slots;
  else
    lw_slots = lw_slot_cache->second_lock_wait_slots;

  if (curr_count >= lw_slots->size())
  {
    lw_slot = create_new_lw_slot();
    if (!lw_slot)
    {
      exit_lw_log_mutex();
      return NULL;
    }
    lw_slot_cache->use_count++;
    lw_slots->push_back(lw_slot);
  }
  else
  {
    lw_slot = lw_slots->at(lw_slot_cache->use_count++);
  }

  // reset flag, snapshot thread must wait this flag to be 1
  lw_slot->completed = 0;

  exit_lw_log_mutex();

  return lw_slot;
}


/**
get free trx info from slot->block_trxes
@param[in]  lw_slot lock wait slot to store lock wait info

@note 1.don't check whether params equal to null, the caller ensure
*/
static TRX_INFO *get_free_trx_info_frm_lw_slot(LOCK_WAIT_SLOT *lw_slot)
{
  TRX_INFO *trx_info = NULL;
  trx_infos *block_trxes = lw_slot->block_trxes;
  if (lw_slot->blk_trx_count >= block_trxes->size())
  {
    trx_info = new TRX_INFO;
    if (!trx_info)
    {
      sql_print_error("get free trx_info error, memmory was not enough");
      return NULL;
    }

    block_trxes->push_back(trx_info);
    lw_slot->blk_trx_count++;
  }
  else
  {
    trx_info = block_trxes->at(lw_slot->blk_trx_count++);
  }
  return trx_info;
}


/** get innodb_lock_wait.log file size */
static ulonglong get_log_file_size()
{
  struct stat file_stat;

  if (-1 == my_fstat(lock_wait_log_fd, &file_stat))
  {
    sql_print_error("get innodb log file size error [%d]",errno);
    return 0;
  }

  return file_stat.st_size;

}


/** if open innodb_lock_wait.log failed, reopen it */
static bool is_need_reopen_log_file()
{
  if (-1 == lock_wait_log_fd)
    return true;

  return false;
}


/** if size of innodb_lock_wait.log is over 100M, move it to
 innodb_lock_wait.log.bak
*/
static bool is_need_clear_log_file()
{
  if (get_log_file_size() > srv_lock_wait_log_size * 1024 *1024)
    return true;

  return false;
}


/**
mv innodb_lock_wait.log to innodb_lock_wait.log.bak if need and then reopen
innodb_lock_wait.log.

@return false success;true error.
*/
static bool clear_log_file_if_need()
{
  if (is_need_reopen_log_file())
    goto OpenLog;

  if (!is_need_clear_log_file())
    return false;

  // remove innodb_lock_wait.log.bak if exsits
  if (0 == my_access(bak_lock_wait_log_full_path, F_OK))
  {
    if (unlink(bak_lock_wait_log_full_path) < 0)
    {
      sql_print_error("unlink innodb_lock_wait.log.bak fail");
      return true;
    }
  }

  if (my_rename(lock_wait_log_full_path, bak_lock_wait_log_full_path, 0) < 0)
  {
    sql_print_error("unlink innodb_lock_wait.log.bak fail");
    return true;
  }

  if (my_close(lock_wait_log_fd,0)<0)
    return true;

OpenLog:
  lock_wait_log_fd = my_open(lock_wait_log_full_path, O_CREAT|O_RDWR, 0);
  if (-1 == lock_wait_log_fd)
    return true;

  return false;
}


/**
set trx info by lock
@param[in]  src_lock  innodb lock
@param[out] trx_info  trx_info to set
@param[in]  is_request TRUE means the lock is request lock, FLASE meas block lock
@param[in]  heap_no   heap_no for wait and waited locks
@return true  set trx info failed
        false set trx info success

@note 1.don't check whether params equal to null
      2.don't report error event if trans_serial_num is truncated
*/
static void set_trx_info(const lock_t *src_lock, TRX_INFO *trx_info,
                         bool is_request, ulint heap_no)
{
  THD *thd = NULL;

  /** check request_lock->trx null ? */
  if (!src_lock->trx)
  {
    sql_print_information("set set_trx_info fail, trx in lock is null");
    goto ResetData;
  }

  thd = src_lock->trx->mysql_thd;
  if (!thd)
  {
    sql_print_information("set set_trx_info fail, mysql_thd in lock->trx is null");
    goto ResetData;
  }

  // set thread_id and trx_id of src_lock->trx
  trx_info->thread_id = thd_get_thread_id(thd);
  trx_info->trx_id    = lock_get_trx_id(src_lock);

  /* trx_serial_num is always NULL-terminated */
  set_trans_serial_num(thd, trx_info->trx_serial_num, sizeof(trx_info->trx_serial_num));

  /* trx_gtm_gtid is always NULL-terminated */
  set_trans_gtm_gtid(thd, trx_info->trx_gtm_gtid, sizeof(trx_info->trx_gtm_gtid));

  /* sql_string is always NULL-terminated */
  reset_string(trx_info->sql_string);
  if(TRUE == is_request)
      innobase_get_stmt_safe(thd, trx_info->sql_string, sizeof(trx_info->sql_string));

  /* key_data is always NULL-terminated */
  reset_string(trx_info->key_data);
  if(FALSE == is_request)
      lock_rec_print_key_data(trx_info->key_data, sizeof(trx_info->key_data), src_lock, heap_no);

  return ;

ResetData:
  trx_info->thread_id = static_cast<ulong>(0);
  trx_info->trx_id = static_cast<trx_id_t>(0);
  set_zero_string(trx_info->trx_serial_num);
  set_zero_string(trx_info->trx_gtm_gtid);
  reset_string(trx_info->sql_string);
  reset_string(trx_info->key_data);
  return ;
}


/**
@note  1. notice snapshot thread to write ls_slot infomation
       2. dont check param whther is NULL
*/
static void notify_snapshot_thread_to_write(LOCK_WAIT_SLOT *lw_slot)
{
  lw_slot->completed = 1;
}


/**
whether the slot info is ready by app thread. if so snapshot thread can
write the lw_slot info
@note dont check param wether is NULL
*/
static bool is_lock_wait_slot_ready(LOCK_WAIT_SLOT *lw_slot)
{
  if (1 == lw_slot->completed)
    return true;

  return false;
}


/**
@note 1.dont check return value wthether is NULL
      2.the caller must acquire lw_log_mutex
*/
static lock_wait_slots *get_current_lw_slots(void)
{
  lock_wait_slots *lw_slots;

  if (lw_slot_cache->first_in_use)
    lw_slots = lw_slot_cache->first_lock_wait_slots;
  else
    lw_slots = lw_slot_cache->second_lock_wait_slots;

  return lw_slots;
}


/**
@note 1.dont check return value wthether is NULL
      2.the caller must acquire lw_log_mutex
*/
static uint get_current_slot_count(void)
{
  return lw_slot_cache->use_count;
}


/**
create a new LOCK_WAIT_SLOT and init LOCK_WAIT_SLOT::block_trxes
@return NULL create fail
*/
static LOCK_WAIT_SLOT *create_new_lw_slot()
{
  LOCK_WAIT_SLOT* lw_slot = new LOCK_WAIT_SLOT;
  if (!lw_slot)
  {
    sql_print_error("create_new_lw_slot fail,memory was not enough");
    return NULL;
  }

  lw_slot->request_trx = new TRX_INFO;
  lw_slot->block_trxes = new trx_infos;
  if (!lw_slot->request_trx || !lw_slot->block_trxes)
    goto ProcErr;

  if (init_trx_infos(lw_slot->block_trxes))
    goto ProcErr;

  return lw_slot;

ProcErr:
  if (lw_slot->request_trx)
    delete lw_slot->request_trx;

  if (lw_slot->block_trxes)
    delete lw_slot->block_trxes;

  delete lw_slot;

  return NULL;
}


static void set_trans_serial_num(THD *thd, char *buf, size_t buflen)
{
  if (0 == thd_get_serial_num_safe(thd, buf, buflen))
    set_zero_string(buf);

  return;
}


static void set_trans_gtm_gtid(THD *thd, char *buf, size_t buflen)
{
  if (0 == thd_get_gtm_gtid_safe(thd, buf, buflen))
    set_zero_string(buf);

  return;
}


static void set_zero_string(char *buf)
{
  buf[0] = 48; // '0'
  buf[1] = 0;
  return;
}

static void reset_string(char *buf)
{
  buf[0] = '\0';
  return;
}

static void wait_lw_log_snapshot_thread_quited(void)
{
  while (lock_wait_snapshot_thread_active)
  {
    os_thread_sleep(100);
  }

  return;
}


static bool close_lw_log_file()
{
  my_close(lock_wait_log_fd, 0);
  return false;
}


static bool enter_lw_log_mutex()
{
  if (!lw_log_mutex)
    return true;

  lw_log_mutex->enter();

  return false;
}


static bool exit_lw_log_mutex()
{
  if (!lw_log_mutex)
    return true;

  lw_log_mutex->exit();

  return false;
}


/**
the caller must hold lw_log_mutex
*/
static bool switch_lock_wait_slots(void)
{
  // switch lock_wait_slots and reset counter
  lw_slot_cache->use_count = 0;
  lw_slot_cache->first_in_use = !(lw_slot_cache->first_in_use);
  return false;
}


/** the caller must hold lw_log_mutex */
static bool is_lw_slot_cache_empty(void)
{
  if (!lw_slot_cache || 0 == lw_slot_cache->use_count)
    return true;

  return false;
}


static bool need_write_lock_wait(LOCK_WAIT_SLOT *lw_slot)
{
  if (!lw_slot)
    return false;

  while (1)
  {
    ulint wait_time = get_lock_wait_time(lw_slot);
    if (wait_time)
      return is_lock_wait_too_long(lw_slot,wait_time);

    if (is_lock_wait_too_long(lw_slot))
      return true;

    os_thread_sleep(100); //sleep 0.1ms
  }

  return true;
}


/**
@millsecs[in] microseconds
*/
static bool is_lock_wait_too_long(LOCK_WAIT_SLOT *lw_slot, ulint microsecs)
{
  if (!lw_slot)
    return false;

  if (microsecs >= srv_lock_wait_collect_time*1000)
  {
    if (microsecs > LOCK_WAIT_COLLECT_TIMEOUT*1000)
      lw_slot->time_out = true;
    return true;
  }

  return false;
}


static bool is_lock_wait_too_long(LOCK_WAIT_SLOT *lw_slot)
{
  if (!lw_slot)
    return false;

  ulint dif_time = get_lock_wait_time_so_far(lw_slot);

  if (dif_time > LOCK_WAIT_COLLECT_TIMEOUT*1000)
  {
    lw_slot->time_out = true;
    return true;
  }

  return false;
}



/**
return: microseconds
*/
static ulint get_lock_wait_time_so_far(LOCK_WAIT_SLOT *lw_slot)
{
  ib_time_monotonic_us_t start_time = 0;
  ulint   diff_time;

  start_time  = lw_slot->timestamp;

  const auto finish_time = ut_time_monotonic_us();


  diff_time = (finish_time > start_time) ? (finish_time - start_time) : 0;

  return diff_time;
}


static void reset_lw_slot(LOCK_WAIT_SLOT *lw_slot)
{
  if (!lw_slot)
    return;

  lw_slot->completed = 0;
  lw_slot->wait_time = 0;
  lw_slot->blk_trx_count = 0;
  lw_slot->time_out = false;

  return;
}


/**
 return wait time in us
*/
static ulint get_lock_wait_time(LOCK_WAIT_SLOT *lw_slot)
{
  if (!lw_slot)
    return 0;

  return (ulint)(lw_slot->wait_time);
}

/** write separator format to buf
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_separator_format(char *buf, char *buf_end)
{
  char *buf_pos = buf;

  int separator_len = snprintf(buf_pos, buf_end - buf_pos, "||||");
  buf_pos += separator_len;

  return buf_pos;
}

/** write request trx serial number to buf
@param[in] lock_wait_slot lock wait info stores request trx
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_req_trx_serial_num(LOCK_WAIT_SLOT *lock_wait_slot,
                                     char *buf, char *buf_end)
{

  char *buf_pos = buf;
  TRX_INFO *req_trx = lock_wait_slot->request_trx;

  int trx_serial_num_len = snprintf(buf_pos, buf_end - buf_pos,
                                       "%s", req_trx->trx_serial_num);
  buf_pos += trx_serial_num_len;

  return buf_pos;
}

/** write request trx wait time to buf
@param[in] lock_wait_slot lock wait info stores request trx and block trx vector
@param[out] buf to  store lock wait info
@param[in]  buf_end the end of buf

@note 1.don't check whether params equal to null, the caller ensure
      2.ensure don't write mem over buf_end
*/
static char *make_log_wait_time(LOCK_WAIT_SLOT *lock_wait_slot,
                                char *buf, char *buf_end)
{
  char *buf_pos = buf;

  int lock_time_len = 0;
  if (lock_wait_slot->time_out)
  {
    lock_time_len = snprintf(buf_pos, buf_end - buf_pos,
                             "#WARN:DESC=lock_wait_time:more than %lums,",
                             LOCK_WAIT_COLLECT_TIMEOUT);
  }
  else
  {
    lock_time_len = snprintf(buf_pos, buf_end - buf_pos,
                             "#WARN:DESC=lock_wait_time:%lums,",
                             get_lock_wait_time(lock_wait_slot)/1000);
  }
  buf_pos += lock_time_len;

  if (buf_pos >= buf_end)
    *(buf_end-1) = '\n';
  else
    *buf_pos++ = '\n';

  return buf_pos;
}


/**
This function is copy from fill_lock_data in trx0i_s.cc.
Get the rec lock_data by the index.
@param lock_data  the index rec buffer
@param data_size  the buffer size
@param lock       lock used to find the data
@param heap_no    heap_no for wait and waited locks
@return void */
static void lock_rec_print_key_data(char* lock_data, size_t data_size,
                                    const lock_t* lock, ulint heap_no)
{
    mtr_t               mtr;
    const buf_block_t*  block = NULL;
    const page_t*       page  = NULL;
    const rec_t*        rec   = NULL;
    const dict_index_t* index = NULL;
    mem_heap_t*         heap  = NULL;
    ulint           offsets_onstack[REC_OFFS_NORMAL_SIZE];
    ulint*          offsets   = NULL;
    ulint           n_fields  = 0;
    ulint           buf_used  = 0;
    ulint           heap_no_new = 0;
    ulint           i         = 0;

    lock_data[0] = '\0';

    if (LOCK_REC != lock_get_type_low(lock))
      return;

    /* ULINT_UNDEFINED means need get the heap_no by lock. */
    if (heap_no == ULINT_UNDEFINED)
      heap_no_new = lock_rec_find_set_bit(lock);
    else
      heap_no_new = heap_no;

    switch (heap_no_new)
    {
      case PAGE_HEAP_NO_INFIMUM:
        snprintf(lock_data, data_size, "infimum pseudo-record");
        return;
      case PAGE_HEAP_NO_SUPREMUM:
        snprintf(lock_data, data_size, "supremum pseudo-record");
        return;
    }

    rec_offs_init(offsets_onstack);
    offsets = offsets_onstack;

    mtr_start(&mtr);

    block = buf_page_try_get(page_id_t(lock_rec_get_space_id(lock),
                                       lock_rec_get_page_no(lock)), &mtr);
    if (block == NULL)
    {
      mtr_commit(&mtr);
      return;
    }

    page = reinterpret_cast<const page_t*>(buf_block_get_frame(block));

    rec = page_find_rec_with_heap_no(page, heap_no_new);
    if (NULL == rec)
    {
      // not possible, just for klocwork
      mtr_commit(&mtr);
      return;
    }

    index = lock_rec_get_index(lock);

    n_fields = dict_index_get_n_unique(index);

    ut_a(n_fields > 0);

    offsets = rec_get_offsets(rec, index, offsets, n_fields, &heap);

    for (i = static_cast<ulint>(0); i < n_fields; i++)
    {
      buf_used += put_nth_field_for_keydata(lock_data + buf_used,
                 data_size - buf_used, i, index, rec, offsets) - 1;
    }

    if (heap != NULL)
    {
      /* this means that rec_get_offsets() has created a new
      heap and has stored offsets in it; check that this is
      really the case and free the heap */
      ut_a(offsets != offsets_onstack);
      mem_heap_free(heap);
    }

    mtr_commit(&mtr);
    return;
}


/**
This function is copy from put_nth_field in trx0i_s.cc.

Format the nth field of "rec" and put it in "buf". The result is always
 NUL-terminated. Returns the number of bytes that were written to "buf"
 (including the terminating NUL).
 @return end of the result */
static ulint put_nth_field_for_keydata(
    char *buf,                 /*!< out: buffer */
    ulint buf_size,            /*!< in: buffer size in bytes */
    ulint n,                   /*!< in: number of field */
    const dict_index_t *index, /*!< in: index */
    const rec_t *rec,          /*!< in: record */
    const ulint *offsets)      /*!< in: record offsets, returned
                               by rec_get_offsets() */
{
  const byte *data;
  ulint data_len;
  dict_field_t *dict_field;
  ulint ret;

  ut_ad(rec_offs_validate(rec, NULL, offsets));

  if (buf_size == 0) {
    return (0);
  }

  ret = 0;

  if (n > 0) {
    /* we must append ", " before the actual data */

    if (buf_size < 3) {
      buf[0] = '\0';
      return (1);
    }

    memcpy(buf, ", ", 3);

    buf += 2;
    buf_size -= 2;
    ret += 2;
  }

  /* now buf_size >= 1 */

  /* Here any field must be part of index key, which should not be
  added instantly, so no default value */
  ut_ad(!rec_offs_nth_default(offsets, n));

  data = rec_get_nth_field(rec, offsets, n, &data_len);

  dict_field = index->get_field(n);

  ret +=
      row_raw_format((const char *)data, data_len, dict_field, buf, buf_size);

  return (ret);
}


#endif // HAVE_ZSQL_TSN_AND_LOCK_WAIT

