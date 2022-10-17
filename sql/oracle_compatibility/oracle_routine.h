/* Copyright (c) 1985, 2020, ZTE and/or its affiliates. All rights reserved.
   Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

  This file contains definitions about oracle routine compatibility.
  For example, oracle procedure, declare block, and some basic utils.

*/

#ifndef ORACLE_ROUTINE_H
#define ORACLE_ROUTINE_H

#include "sql/sql_class.h"

// generate full name of ident
const char *construct_full_name(THD *thd, const char *db,
                                const char *table, const char *field);

#endif /* ORACLE_ROUTINE_H */
