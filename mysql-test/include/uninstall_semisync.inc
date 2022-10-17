--let $include_filename= uninstall_semisync.inc
--source include/begin_include_file.inc

--disable_query_log
--connection slave
--source include/stop_slave_io.inc

# Uninstall rpl_semi_sync_slave first
--disable_warnings
set global rpl_semi_sync_slave_enabled=OFF;

--connection master
# After BUG#17638477 fix, uninstallation of rpl_semi_sync_master
#  is not allowed when there are semi sync slaves. Hence kill
#  all dump threads before uninstalling it.
--source include/stop_dump_threads.inc
set global rpl_semi_sync_master_enabled=OFF;
--enable_warnings

--connection slave
--source include/start_slave_io.inc
--enable_query_log

--source include/end_include_file.inc
