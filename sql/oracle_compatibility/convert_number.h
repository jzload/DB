#ifndef CONVERT_NUMBER_H
#define CONVERT_NUMBER_H

#include "lex_string.h"
#include "sql/my_decimal.h"

#define TO_NUM_MAX_STR_LEN 65535

extern bool parse_to_num_one_param_str(const char *pStr, size_t length);
extern bool process_to_num_two_params(String *value, String *fmt,
                                      char *out_value, size_t out_len);

#endif  // CONVERT_NUMBER_H
