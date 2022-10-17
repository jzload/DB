/* Copyright (C) 2012 Monty Program Ab
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
   USA */

#include "my_thread_local.h"
#include "sql/sql_class.h"
#include "mysql/psi/mysql_idle.h"
#include "mysql/psi/mysql_socket.h"
#include "mysql/thread_pool_priv.h"
#include "sql/conn_handler/channel_info.h"
#include "sql/conn_handler/connection_handler_manager.h"
#include "sql/debug_sync.h"
#include "sql/mysqld.h"
#include "sql/mysqld_thd_manager.h"
#include "sql/sql_audit.h"
#include "sql/sql_connect.h"
#include "sql/protocol_classic.h"
#include "sql/sql_parse.h"
#include "sql/threadpool.h"
#include "violite.h"
#include "sql/sql_lex.h"
#include "sql/log.h"

/* Threadpool parameters */

uint threadpool_min_threads;
uint threadpool_idle_timeout;
uint threadpool_size;
uint threadpool_stall_limit;
uint threadpool_max_threads;
uint threadpool_oversubscribe;

/* Stats */
TP_STATISTICS tp_stats;


extern bool do_command(THD*);

extern int commit_group_threads(THD *thd);


/*
  Worker threads contexts, and THD contexts.
  =========================================

  Both worker threads and connections have their sets of thread local variables
  At the moment it is mysys_var (this has specific data for dbug, my_error and
  similar goodies), and PSI per-client structure.

  Whenever query is executed following needs to be done:

  1. Save worker thread context.
  2. Change TLS variables to connection specific ones using thread_attach(THD*).
     This function does some additional work.
  3. Process query
  4. Restore worker thread context.

  Connection login and termination follows similar schema w.r.t saving and
  restoring contexts.

  For both worker thread, and for the connection, mysys variables are created
  using my_thread_init() and freed with my_thread_end().

*/
class Worker_thread_context {
#ifdef HAVE_PSI_THREAD_INTERFACE
  PSI_thread *const psi_thread;
#endif
#ifndef DBUG_OFF
  const my_thread_id thread_id;
#endif
 public:
  Worker_thread_context() noexcept
#ifdef HAVE_PSI_THREAD_INTERFACE
      :
        psi_thread(PSI_THREAD_CALL(get_thread)())
#endif
#ifndef DBUG_OFF
#ifdef HAVE_PSI_THREAD_INTERFACE
        ,
#else
      :
#endif
        thread_id(my_thread_var_id())
#endif
  {
  }

  ~Worker_thread_context() noexcept {
#ifdef HAVE_PSI_THREAD_INTERFACE
    PSI_THREAD_CALL(set_thread)(psi_thread);
#endif
#ifndef DBUG_OFF
    set_my_thread_var_id(thread_id);
#endif
    THR_MALLOC = nullptr;
  }
};

/*
  Attach/associate the connection with the OS thread,
*/
static bool thread_attach(THD *thd) {
#ifndef DBUG_OFF
  set_my_thread_var_id(thd->thread_id());
#endif
  thd->thread_stack = (char *)&thd;
  thd->store_globals();
#ifdef HAVE_PSI_THREAD_INTERFACE
  PSI_THREAD_CALL(set_thread)(thd->get_psi());
#endif
  mysql_socket_set_thread_owner(
      thd->get_protocol_classic()->get_vio()->mysql_socket);
  return 0;
}

#ifdef HAVE_PSI_STATEMENT_INTERFACE
extern PSI_statement_info stmt_info_new_packet;
#endif

static void threadpool_net_before_header_psi_noop(NET * /* net */,
                                                  void * /* user_data */,
                                                  size_t /* count */) {}

static void threadpool_init_net_server_extension(THD *thd) {
#ifdef HAVE_PSI_INTERFACE
  // socket_connection.cc:init_net_server_extension should have been called
  // already for us. We only need to overwrite the "before" callback
  DBUG_ASSERT(thd->m_net_server_extension.m_user_data == thd);
  thd->m_net_server_extension.m_before_header =
      threadpool_net_before_header_psi_noop;
#else
  DBUG_ASSERT(thd->get_protocol_classic()->get_net()->extension == NULL);
#endif
}

int threadpool_add_connection(THD *thd) {
  int retval = 1;
  Worker_thread_context worker_context;

  my_thread_init();

  /* Create new PSI thread for use with the THD. */
#ifdef HAVE_PSI_THREAD_INTERFACE
  thd->set_psi(PSI_THREAD_CALL(new_thread)(key_thread_one_connection, thd,
                                           thd->thread_id()));
#endif

  /* Login. */
  thread_attach(thd);
  thd->start_utime = my_micro_time();
  thd->store_globals();

  if (thd_prepare_connection(thd)) {
    goto end;
  }

  /*
    Check if THD is ok, as prepare_new_connection_state()
    can fail, for example if init command failed.
  */
  if (thd_connection_alive(thd)) {
    retval = 0;
    thd_set_net_read_write(thd, 1);
    thd->skip_wait_timeout = true;
    MYSQL_SOCKET_SET_STATE(thd->get_protocol_classic()->get_vio()->mysql_socket,
                           PSI_SOCKET_STATE_IDLE);
    thd->m_server_idle = true;
    threadpool_init_net_server_extension(thd);
  }

end:
  if (retval) {
    Connection_handler_manager *handler_manager =
        Connection_handler_manager::get_instance();
    handler_manager->inc_aborted_connects();
  }
  return retval;
}

void threadpool_remove_connection(THD *thd) {
  Worker_thread_context worker_context;

  thread_attach(thd);
  thd_set_net_read_write(thd, 0);

  end_connection(thd);
  close_connection(thd, 0);

  thd->release_resources();

#ifdef HAVE_PSI_THREAD_INTERFACE
  PSI_THREAD_CALL(delete_thread)(thd->get_psi());
#endif

  Global_THD_manager::get_instance()->remove_thd(thd);
  Connection_handler_manager::dec_connection_count();
  delete thd;
}

/**
 Process a single client request or a single batch.
*/
int threadpool_process_request(THD *thd, int *quicksync_refcount)
{
  int retval= 0, tmp_refcount = 0;
  Worker_thread_context  worker_context;

  thread_attach(thd);
  
  wait_quick_sync_old_thread_exit(thd);      

  tmp_refcount = thd->get_quick_sync_refcount();
  
  if (tmp_refcount)
  {
    if(0 != thd->commit_queue_wait_time)
    {
      thd->commit_queue_wait_time = thd->current_utime() - thd->commit_queue_wait_time;
    }
  }
  else if(NULL == thd->m_quick_parser_state->m_lip.found_semicolon)/*not multi-statements*/
  {
    thd->pre_dispatch_time = thd->current_utime();
  }

  if (thd->killed == THD::KILL_CONNECTION) {
    /*
      killed flag was set by timeout handler
      or KILL command. Return error.
    */
    retval = 1;
    if (tmp_refcount || thd->m_quick_parser_state->m_lip.found_semicolon != NULL)
    {
      commit_group_threads(thd);
      *quicksync_refcount = thd->dec_refcount_for_quicksync();
    }
    
    goto end;
  }

  /*
    In the loop below, the flow is essentially the copy of thead-per-connections
    logic, see do_handle_one_connection() in sql_connect.c

    The goal is to execute a single query, thus the loop is normally executed
    only once. However for SSL connections, it can be executed multiple times
    (SSL can preread and cache incoming data, and vio->has_data() checks if it
    was the case).

    This loop below for quick_sync has no problem, but cannot test this case.
  */
  for(;;)
  {
    int is_commit_event = 0;
    Vio *vio;
    thd_set_net_read_write(thd, 0);

    my_sleep(2 * 1000);
    
    if (thd->is_quick_sync() || thd->m_quick_parser_state->m_lip.found_semicolon != NULL)
    {
      //todo: commit for innodb
      commit_group_threads(thd);
      is_commit_event = 1;
    }
    else
    {
      if ((retval = do_command(thd)) != 0)
        goto end;
    }
    
    if (!thd_connection_alive(thd))
    {
      retval = 1;
      *quicksync_refcount = thd->dec_refcount_for_quicksync();
      goto end;
    }

    DBUG_EXECUTE_IF("quick_sync_old_thread_sleep", {
         if (2 == thd->get_quick_sync_refcount())
           my_sleep(1000*1000);
    };);
    
    *quicksync_refcount = thd->dec_refcount_for_quicksync();
    if (*quicksync_refcount > 0)
      goto end;

    vio= thd->get_protocol_classic()->get_vio();
    if (!vio->has_data(vio) && thd->m_quick_parser_state->m_lip.found_semicolon == NULL)
    { 
      /* More info on this debug sync is in sql_parse.cc*/
      DEBUG_SYNC(thd, "before_do_command_net_read");
      thd_set_net_read_write(thd, 1);
      goto end;
    }
    if (!thd->m_server_idle && thd->m_quick_parser_state->m_lip.found_semicolon == NULL) 
    {
      MYSQL_SOCKET_SET_STATE(vio->mysql_socket, PSI_SOCKET_STATE_IDLE);
      MYSQL_START_IDLE_WAIT(thd->m_idle_psi, &thd->m_idle_state);
      thd->m_server_idle= true;
    }

    /**
      * Only for query block with quick sync query and non quick sync query. 
      * Example: delimiter //
      *          insert...; create table...; insert...; drop table...; insert...; //
      * 1. Must exit, otherwise will cause bug in mutli_statement.
      * 2. We should check the found_semicolon!=NULL for the multi-block query
      */ 
    if (is_commit_event && thd->m_quick_parser_state->m_lip.found_semicolon != NULL)
    {
      append_event_to_high_prio_queue(thd);
      goto exit;
    }
    
  }

end:
  if (!(*quicksync_refcount) && !retval && !thd->m_server_idle) {
    MYSQL_SOCKET_SET_STATE(thd->get_protocol_classic()->get_vio()->mysql_socket,
                           PSI_SOCKET_STATE_IDLE);
    MYSQL_START_IDLE_WAIT(thd->m_idle_psi, &thd->m_idle_state);
    thd->m_server_idle = true;
  }

  /**
   * Quick sync with query block can not do start io until all query finished. 
   * The start_io will add the fd to the epoll listen and then thread_pool can
   * listen this fd events by epoll_wait.
   */
  if (!(*quicksync_refcount) && !retval)
  {
    retval = thd_start_io(thd);
  }

  DBUG_EXECUTE_IF("quick_sync_killed_connection", {
         if (*quicksync_refcount)
         {
           thd->killed = THD::KILL_CONNECTION;
           my_sleep(1000*1000);
           retval = 1;
         }
    };);

exit:
  return retval;
}

THD_event_functions tp_event_functions = {tp_wait_begin, tp_wait_end,
                                          tp_post_kill_notification};
