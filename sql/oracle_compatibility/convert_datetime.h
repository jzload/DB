#ifndef CONVERT_DATETIME_H
#define CONVERT_DATETIME_H

#include "lex_string.h"
#include "mysql_time.h"

extern bool ora_extract_date_time(LEX_STRING *date_string,
                                  LEX_STRING *fmt_string,
                                  const char *func_name,
                                  MYSQL_TIME *ltime);

int get_max_days_of_month(unsigned int year, unsigned int month);
#endif // CONVERT_DATETIME_H
