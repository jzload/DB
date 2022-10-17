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

#ifndef SQL_EXCHANGE_INCLUDED
#define SQL_EXCHANGE_INCLUDED

struct CHARSET_INFO;
class String;

enum enum_filetype { FILETYPE_CSV, FILETYPE_XML };

/**
  Helper for the sql_exchange class
*/

struct Line_separators {
  const String *line_term;
  const String *line_start;

  void cleanup() { line_term = line_start = nullptr; }
  void merge_line_separators(const Line_separators &s) {
    if (s.line_term != nullptr) line_term = s.line_term;
    if (s.line_start != nullptr) line_start = s.line_start;
  }
};

/**
  Helper for the sql_exchange class
*/

struct Field_separators {
  const String *field_term;
  const String *escaped;
  const String *enclosed;
  bool opt_enclosed;

  void cleanup() {
    field_term = escaped = enclosed = nullptr;
    opt_enclosed = false;
  }
  void merge_field_separators(const Field_separators &s) {
    if (s.field_term != nullptr) field_term = s.field_term;
    if (s.escaped != nullptr) escaped = s.escaped;
    if (s.enclosed != nullptr) enclosed = s.enclosed;
    // TODO: a bug?
    // OPTIONALLY ENCLOSED BY x ENCLOSED BY y == OPTIONALLY ENCLOSED BY y
    if (s.opt_enclosed) opt_enclosed = s.opt_enclosed;
  }
};

/**
  Helper for the sql_exchange class
*/

enum Pump_type {
  PUMP_UNKNOWN = 0,
/*========================== EXPORT ==========================*/
  PUMP_EXPORT_ECSV,
  /* no append spaces while fields enclosed and terminated are '' (empty string) */
  PUMP_EXPORT_NOSPACE_CSV,
  /* used when fields enclosed is empty string, char is escaped only if it equals escape_char,
     and also no append spaces while fields enclosed and terminated are empty */
  PUMP_EXPORT_JCSV,

/*========================== IMPORT ==========================*/
  /* treat ignore escape char as data, instead of escapement */
  PUMP_IMPORT_NO_ESCAPES
};

struct Datapump_format {
  Pump_type pump_type;

  void cleanup() { pump_type = PUMP_UNKNOWN; }

  void merge_data_format(const Datapump_format &s) {
    pump_type = s.pump_type;
  }
};

/**
  Used to hold information about file and file structure in exchange
  via non-DB file (...INTO OUTFILE..., ...LOAD DATA...)
  XXX: We never call destructor for objects of this class.
*/

class sql_exchange final {
 public:
  Field_separators field;
  Line_separators line;
  enum enum_filetype filetype; /* load XML, Added by Arnold & Erik */
  const char *file_name;
  bool dumpfile;
  unsigned long skip_lines;
  const CHARSET_INFO *cs;
  Datapump_format datapump;
  sql_exchange(const char *name, bool dumpfile_flag,
               enum_filetype filetype_arg = FILETYPE_CSV);
  bool escaped_given(void);
};

#endif
