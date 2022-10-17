# Uninstall semisync master plugin
# Usage:
# --source include/uninstall_semisync_master.inc

--let $include_filename= uninstall_semisync_master.inc
--source include/begin_include_file.inc

--disable_query_log
set global rpl_semi_sync_master_enabled=OFF;
set global rpl_semi_sync_master_group1=NULL;
set global rpl_semi_sync_master_group2=NULL;
set global rpl_semi_sync_master_group3=NULL;
set global rpl_semi_sync_master_group4=NULL;
set global rpl_semi_sync_master_group5=NULL;
set global rpl_semi_sync_master_group6=NULL;
set global rpl_semi_sync_master_group7=NULL;
set global rpl_semi_sync_master_group8=NULL;
set global rpl_semi_sync_master_group9=NULL;
set global rpl_semi_sync_master_group10=NULL;
set global rpl_semi_sync_master_inspection_time=1000;
set global rpl_semi_sync_master_quick_sync_enabled=ON;
set global rpl_semi_sync_master_timeout=10000;
set global rpl_semi_sync_master_trace_level=32;
set global rpl_semi_sync_master_wait_cond_lwm=NULL;
set global rpl_semi_sync_master_wait_cond_hwm=NULL;
set global rpl_semi_sync_master_wait_for_slave_count=1;
set global rpl_semi_sync_master_wait_no_slave=ON;
set global rpl_semi_sync_master_wait_point=AFTER_SYNC;
--disable_warnings

# wait till semisync clients are '0' before uninstalling
# semisync master plugin.
--let $status_var= Rpl_semi_sync_master_clients
--let $status_var_value= 0
--source include/wait_for_status_var.inc

--source include/end_include_file.inc
