/* Copyright (c) 2018, 2019, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is also distributed with certain software (including
but not limited to OpenSSL) that is licensed under separate terms,
as designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have included with MySQL.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef MYSQL_RUNTIME_ERROR_H
#define MYSQL_RUNTIME_ERROR_H

#include <mysql/components/service.h>
//! @cond Doxygen_Suppress
#include <stdarg.h>
//! @endcond

/**
  This service defines the error report function api.
*/
BEGIN_SERVICE_DEFINITION(mysql_runtime_error)
/**
  It calls the server SQL error generation function and adds the error
  into the THD's error context.

  @param error_id mysql server error number, used to get the error discription.
  @param flags this will tell, whether the error is a fatal statement error or
               write the error to error log file.
  @param args  variable argument list which has the error message details.
*/
DECLARE_METHOD(void, emit, (int error_id, int flags, va_list args));

END_SERVICE_DEFINITION(mysql_runtime_error)

#endif /* MYSQL_RUNTIME_ERROR_H */
