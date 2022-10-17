#ifndef SQL_DISTRIBUTED_MVCC_INCLUDED
#define SQL_DISTRIBUTED_MVCC_INCLUDED

#include "sql/sql_class.h"
#include "mysqld_error.h"

/**
  create the active gtmgtids array, and check the base64 code from set var hints
   @param thd Thread handler
   @return true if error ,false is success */
bool make_active_gtmgtids(THD *thd);

/**
  check the next gtmgtid is correct
   @param thd Thread handler
   @return true if error ,false is success */
bool check_gtmgtids(const THD *thd);
#endif
