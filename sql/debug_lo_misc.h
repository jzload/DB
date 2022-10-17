
/* Copyright (c) 2018, 2019, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <string>

class LO_graph;
struct MEM_ROOT;

typedef void *yyscan_t;

struct LO_parser_param {
  yyscan_t m_scanner;
  MEM_ROOT *m_memroot;
  FILE *m_data;
  const char *m_filename;
  LO_graph *m_graph;
};
