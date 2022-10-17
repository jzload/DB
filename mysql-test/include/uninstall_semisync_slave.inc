# Uninstall semisync slave plugin
# Usage:
# --source include/uninstall_semisync_slave.inc

--let $include_filename= uninstall_semisync_slave.inc
--source include/begin_include_file.inc

--disable_query_log

--source include/stop_slave_io.inc

--disable_warnings
set global rpl_semi_sync_slave_enabled=OFF;
set global rpl_semi_sync_slave_trace_level=32;
--enable_warnings

--source include/start_slave_io.inc

--source include/end_include_file.inc
