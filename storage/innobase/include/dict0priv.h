/*****************************************************************************

Copyright (c) 2010, 2018, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

/** @file include/dict0priv.h
 Data dictionary private functions

 Created  Fri 2 Jul 2010 13:30:38 EST - Sunny Bains
 *******************************************************/

#ifndef dict0priv_h
#define dict0priv_h

#include "univ.i"

/** Gets a table; loads it to the dictionary cache if necessary. A low-level
 function. Note: Not to be called from outside dict0*c functions.
 @return table, NULL if not found */
UNIV_INLINE
dict_table_t *dict_table_get_low(const char *table_name); /*!< in: table name */

/** Checks if a table is in the dictionary cache.
 @return table, NULL if not found */
UNIV_INLINE
dict_table_t *dict_table_check_if_in_cache_low(
    const char *table_name); /*!< in: table name */

#include "dict0priv.ic"

#endif /* dict0priv.h */
