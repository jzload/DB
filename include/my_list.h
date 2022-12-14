/* Copyright (c) 2000, 2017, Oracle and/or its affiliates. All rights reserved.
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

   Without limiting anything contained in the foregoing, this file,
   which is part of C Driver for MySQL (Connector/C), is also subject to the
   Universal FOSS Exception, version 1.0, a copy of which can be found at
   http://oss.oracle.com/licenses/universal-foss-exception.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef _list_h_
#define _list_h_

/**
  @file include/my_list.h
*/

/*
  NOTE: This file should really include mysql/service_mysql_alloc.h
  (due to the my_free() call in list_pop), but that is not acceptable
  in client code, so it has been kept out.
*/

typedef struct LIST {
#if defined(__cplusplus) && __cplusplus >= 201103L
  struct LIST *prev{nullptr}, *next{nullptr};
  void *data{nullptr};
#else
  struct LIST *prev, *next;
  void *data;
#endif
} LIST;

typedef int (*list_walk_action)(void *, void *);

extern LIST *list_add(LIST *root, LIST *element);
extern LIST *list_delete(LIST *root, LIST *element);
extern LIST *list_cons(void *data, LIST *root);
extern LIST *list_reverse(LIST *root);
extern void list_free(LIST *root, unsigned int free_data);
extern unsigned int list_length(LIST *);
extern int list_walk(LIST *, list_walk_action action, unsigned char *argument);

#define list_rest(a) ((a)->next)
#define list_push(a, b) (a) = list_cons((b), (a))
#define list_pop(A)              \
  {                              \
    LIST *old = (A);             \
    (A) = list_delete(old, old); \
    my_free(old);                \
  }

#endif
