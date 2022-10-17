/* Copyright (c) 2014, 2016, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef RPL_SEMISYNC_MASTER_INSPECTION_DEFINED
#define RPL_SEMISYNC_MASTER_INSPECTION_DEFINED

#include <sys/types.h>
#include <vector>

#include "my_inttypes.h"
#include "my_io.h"
#include "my_thread.h"
#include "sql/sql_class.h"


/**
  Time_inspection check whther transaction is timedout.
  
 */
class Time_inspection : public ReplSemiSyncBase
{
public:
  Time_inspection();
  ~Time_inspection();

  /**
    Start ack receive thread

    @return it return false if succeeds, otherwise true is returned.
  */
  bool start();

  /**
     Stop time inspection thread
  */
  void stop();

  /**
     The core of inspection thread thread.

     It monitors all transaction waiting to commit.
  */
  void run();

  bool init()
  {
    set_inspection_time(rpl_semi_sync_master_inspection_time);
    setTraceLevel(rpl_semi_sync_master_trace_level);
    if (rpl_semi_sync_master_enabled && rpl_semi_sync_master_quick_sync_enabled)
      return start();
    return false;
  }

  void setTraceLevel(unsigned long trace_level)
  {
    trace_level_= trace_level;
  }

  void set_inspection_time(unsigned long time);
  
private:
  enum status {ST_UP, ST_DOWN, ST_STOPPING};
  uint8 m_status;
  /* the value of rpl_semi_sync_master_inspection_time.*/
  unsigned long  inspection_time; 
  /*
    Protect m_status. Time thread and other session may access the variables at the same time.
  */
  mysql_mutex_t m_mutex;
  mysql_cond_t   m_cond;
  my_thread_handle m_pid;

/* Declare them private, so no one can copy the object. */
  Time_inspection(const Time_inspection &time_inspection);
  Time_inspection& operator=(const Time_inspection &time_inspection);

  void set_stage_info(const PSI_stage_info &stage);
};

extern Time_inspection time_inspections;
#endif /* RPL_SEMISYNC_MASTER_INSPECTION_DEFINED */
