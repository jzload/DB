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

#include "sql/rpl_semisync_master.h"
#include "sql/rpl_semisync_master_inspection.h"
#include "my_psi_config.h"
#include "mysql/psi/mysql_stage.h"

extern ReplSemiSyncMaster *repl_semisync_master;

#ifdef HAVE_PSI_INTERFACE
extern PSI_mutex_key key_ss_mutex_Time_inspection_mutex;
extern PSI_cond_key   key_ss_cond_Time_inspection_cond;
extern PSI_thread_key key_ss_thread_Time_inspection_thread;
#endif

/* Callback function of ack receive thread */
extern "C" void *time_inspection_handler(void *arg)
{
  my_thread_init();
  reinterpret_cast<Time_inspection*>(arg)->run();
  my_thread_end();
  my_thread_exit(0);
  return NULL;
}

Time_inspection::Time_inspection()
{
  const char *kWho = "Time_inspection::Time_inspection";
  function_enter(kWho);

  m_status= ST_DOWN;
  
  mysql_mutex_init(key_ss_mutex_Time_inspection_mutex, &m_mutex,
                            MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_ss_cond_Time_inspection_cond, &m_cond);
  
  function_exit(kWho);
}

Time_inspection::~Time_inspection()
{
  const char *kWho = "Time_inspection::~Time_inspection";
  function_enter(kWho);

  stop();
 
  mysql_mutex_destroy(&m_mutex);
  mysql_cond_destroy(&m_cond);
 
  function_exit(kWho);
}

bool Time_inspection::start()
{
  const char *kWho = "Time_inspection::start";
  function_enter(kWho);

  if(m_status == ST_DOWN)
  {
    my_thread_attr_t attr;

    m_status= ST_UP;

    if (DBUG_EVALUATE_IF("rpl_semisync_simulate_create_thread_failure", 1, 0) ||
        my_thread_attr_init(&attr) != 0 ||
        my_thread_attr_setdetachstate(&attr, MY_THREAD_CREATE_JOINABLE) != 0 ||
#ifndef _WIN32
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM) != 0 ||
#endif
    mysql_thread_create(key_ss_thread_Time_inspection_thread, &m_pid,
                            &attr, time_inspection_handler, this))
    {
      sql_print_error("Failed to start semi-sync time inspection thread, "
                            " could not create thread(errno:%d)", errno);

      m_status= ST_DOWN;
      return function_exit(kWho, true);
    }
    (void) my_thread_attr_destroy(&attr);
  }
  return function_exit(kWho, false);
}

void Time_inspection::stop()
{
  const char *kWho = "Time_inspection::stop";
  function_enter(kWho);
  int ret;

  if (m_status == ST_UP)
  {
    mysql_mutex_lock(&m_mutex);
    m_status= ST_STOPPING;
    mysql_cond_broadcast(&m_cond);

    while (m_status == ST_STOPPING)
      mysql_cond_wait(&m_cond, &m_mutex);
     
    mysql_mutex_unlock(&m_mutex);

    /*
      When arriving here, the ack thread already exists. Join failure has no
      side effect aganst semisync. So we don't return an error.
    */
    ret= my_thread_join(&m_pid, NULL);
    if (DBUG_EVALUATE_IF("rpl_semisync_simulate_thread_join_failure", -1, ret))
      sql_print_error("Failed to stop semi-sync time inspection thread on my_thread_join, "
                      "errno(%d)", errno);
  }
  function_exit(kWho);
}


void Time_inspection::run()
{
  struct timespec start_ts;
  struct timespec abstime;
  sql_print_information("Starting time_inspection thread");
  
  while (1)
  {
    mysql_mutex_lock(&m_mutex);
    
    set_timespec(&start_ts, 0);
    /* Calcuate the waiting period. */
    abstime.tv_sec = start_ts.tv_sec + inspection_time / TIME_THOUSAND;
    abstime.tv_nsec = start_ts.tv_nsec +
        (inspection_time % TIME_THOUSAND) * TIME_MILLION;
    if (abstime.tv_nsec >= TIME_BILLION)
    {
      abstime.tv_sec++;
      abstime.tv_nsec -= TIME_BILLION;
    }
    
    if (unlikely(m_status == ST_STOPPING))
      goto end;
    
    if (repl_semisync_master->inspection_transactions())
    {
      mysql_mutex_unlock(&m_mutex);
      my_sleep(1); /* Sleep 1us, so other threads can catch the m_mutex easily. */
      continue;
    }

    mysql_cond_timedwait(&m_cond, &m_mutex, &abstime);
    mysql_mutex_unlock(&m_mutex);
  }
  
end:
  sql_print_information("Stopping time inspection thread");
  m_status= ST_DOWN;
  mysql_cond_broadcast(&m_cond);
  mysql_mutex_unlock(&m_mutex);
}


inline void Time_inspection::set_stage_info(const PSI_stage_info &stage)
{
#ifdef HAVE_PSI_STAGE_INTERFACE
  MYSQL_SET_STAGE(stage.m_key, __FILE__, __LINE__);
#endif /* HAVE_PSI_STAGE_INTERFACE */
}


void Time_inspection::set_inspection_time(unsigned long time)
{
  mysql_mutex_lock(&m_mutex);
  inspection_time = time;
  mysql_mutex_unlock(&m_mutex);
  return;
}


