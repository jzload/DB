# Please add in ASCII order

IF(WIN32)
  MESSAGE(FATAL_ERROR "No windows supported!!")
  RETURN()
ENDIF()

# ZSQL feature: VARCHAR2
ADD_DEFINITIONS(-DHAVE_ORACLE_VARCHAR2)

# ZSQL feature: ASYMMETRIC_ENCRYPT
ADD_DEFINITIONS(-DHAVE_ZSQL_ASYMMETRIC_ENCRYPT)

# ZSQL feature: connect engine
ADD_DEFINITIONS(-DHAVE_ZSQL_CONNECT_ENGINE)

# ZSQL feature: copy table
ADD_DEFINITIONS(-DHAVE_ZSQL_COPY_TABLE)

# ZSQL feature: single table delete supports force index
ADD_DEFINITIONS(-DHAVE_ZSQL_DELETE_FORCE_INDEX)

# ZSQL feature: derived table without alias
ADD_DEFINITIONS(-DHAVE_ZSQL_DERIVED_TABLE_WITHOUT_ALIAS)

# ZSQL feature: disable full table scan for single table deletion
ADD_DEFINITIONS(-DHAVE_ZSQL_DISABLE_FULL_TABLE_SCAN)

# ZSQL feature: disable tcp connection
ADD_DEFINITIONS(-DHAVE_ZSQL_DISABLE_TCP_CONNECTION)

# ZSQL feature: distrubte mvcc on DB
ADD_DEFINITIONS(-DHAVE_ZSQL_DISTRIBUTE_MVCC)

# ZSQL feature: export null as space
ADD_DEFINITIONS(-DHAVE_ZSQL_EXPORT_NULL_AS_SPACE)

# ZSQL feature: fix mysql bugs
ADD_DEFINITIONS(-DHAVE_ZSQL_FIX_ORIGINAL_BUGS)

# ZSQL feature: flashback
ADD_DEFINITIONS(-DHAVE_ZSQL_FLASHBACK)

# ZSQL feature: full join
ADD_DEFINITIONS(-DHAVE_ZSQL_FULL_JOIN)

# ZSQL feature: gdb_format
ADD_DEFINITIONS(-DHAVE_ZSQL_GDB_FORMAT)

# ZSQL feature: insert all
ADD_DEFINITIONS(-DHAVE_ZSQL_INSERT_ALL)

# ZSQL feature: instr_multiparam
ADD_DEFINITIONS(-DHAVE_ZSQL_INSTR_MULTIPARM)

# ZSQL feature: list columns partition support DEFAULT
ADD_DEFINITIONS(-DHAVE_ZSQL_LIST_COLUMNS_DEFAULT)

# ZSQL feature: lock wait depth limitation
ADD_DEFINITIONS(-DHAVE_ZSQL_LOCK_DEPTH_LIMIT)

# ZSQL feature: general slow log rotate and purge
ADD_DEFINITIONS(-DHAVE_ZSQL_LOG_ROTATE_PURGE)

# ZSQL feature: lock wait log shows blocked sql
ADD_DEFINITIONS(-DHAVE_ZSQL_LWL_SHOWS_BLOCKED_SQL)

# ZSQL feature: merge into
ADD_DEFINITIONS(-DHAVE_ZSQL_MERGE_INTO)

# ZSQL feature: MINUS
ADD_DEFINITIONS(-DHAVE_ZSQL_MINUS)

# ZSQL feature: compatible with Oracle
ADD_DEFINITIONS(-DHAVE_ZSQL_ORACLE_COMPATIBILITY)

# ZSQL feature: range (substr())
ADD_DEFINITIONS(-DHAVE_ZSQL_RANGE_STR)

# ZSQL feature: Remove the pk(uk) limitation to partion key of partition table.
ADD_DEFINITIONS(-DHAVE_ZSQL_REMOVE_PARTITION_KEY_LIMITATION)

# ZSQL feature: change row-size limit, configured by max_table_record_size
ADD_DEFINITIONS(-DHAVE_ZSQL_ROWSIZE_LIMIT)

# ZSQL feature: select into outsocket
ADD_DEFINITIONS(-DHAVE_ZSQL_SELECT_INTO_OUTSOCKET)

# ZSQL feature: slave improve
ADD_DEFINITIONS(-DHAVE_ZSQL_SLAVE_IMPROVE)

# ZSQL feature: start with connect by
ADD_DEFINITIONS(-DHAVE_ZSQL_START_WITH_CONNECT_BY)

# ZSQL feature: support JDBC 5.1.40 connect to GoldenDB dbproxy
ADD_DEFINITIONS(-DHAVE_ZSQL_SUPPORT_JDBC_5_1_40_THROUGH_DBPROXY)

# ZSQL feature: sysdate
ADD_DEFINITIONS(-DHAVE_ZSQL_SYSDATE)

# ZSQL feature: error report on big temp-table
ADD_DEFINITIONS(-DHAVE_ZSQL_TEMPTABLE)

# ZSQL feature: to_date
ADD_DEFINITIONS(-DHAVE_ZSQL_TO_DATE)

# ZSQL feature: transaction_max_binlog_size
ADD_DEFINITIONS(-DHAVE_ZSQL_TRANS_MAX_BINLOG_SIZE)

# ZSQL feature: transaction tsn, and lock wait snapshot
ADD_DEFINITIONS(-DHAVE_ZSQL_TSN_AND_LOCK_WAIT)
