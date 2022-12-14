/* Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
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

#include "../../components/mysql_server/component_status_var_service.h"

#include "../../components/mysql_server/server_component.h"
#include "mysql/components/service_implementation.h"

struct SHOW_VAR;

/**
  Its a dummy initialization function. And it will be called from
  server_component_init(). Else linker, is cutting out (as library
  optimization) the status variable service code because libsql code
  is not calling any functions of it.
*/
void mysql_comp_status_var_services_init() { return; }

/**
  Register status variable.

  @param  status_var fully constructed status variable object.
  @return Status of performed operation
  @retval false success
  @retval true failure

  Note: Please see the components/test/test_status_var_service.cc file,
  to know how to construct status varables for different variable types.
*/
DEFINE_BOOL_METHOD(mysql_status_variable_registration_imp::register_variable,
                   (SHOW_VAR * status_var)) {
  try {
    if (add_status_vars(status_var)) return true;

    return false;
  } catch (...) {
    mysql_components_handle_std_exception(__func__);
  }
  return true;
}

/**
  Unregister's status variable.
  @param  status_var SHOW_VAR object with only the name of the variable,
                     which has to be removed from the global list.
  @return Status of performed operation
  @retval false success
  @retval true failure
*/
DEFINE_BOOL_METHOD(mysql_status_variable_registration_imp::unregister_variable,
                   (SHOW_VAR * status_var)) {
  try {
    remove_status_vars(status_var);
    return false;
  } catch (...) {
    mysql_components_handle_std_exception(__func__);
  }
  return true;
}
