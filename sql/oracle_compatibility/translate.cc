/**
  @file sql/oracle_compatibility/translate.cc

  @brief
  This file defines translate function.
*/
#include <stdio.h>

#include "sql/sql_class.h"  // THD
#include "sql/strfunc.h"
#include "sql/oracle_compatibility/translate.h"


uint Item_func_translate::byte_len_per_char(const String *str,
                                              const char *ptr,
                                              const char *ptr_end) {
  if (use_mb(str->charset())) {
    uint len = my_ismbchar(str->charset(), ptr, ptr_end);
    if (len == 0) len ++;
    return len;
  }
  else
    return 1;
}

int32 Item_func_translate::find_in_from(const char *source_ptr,
                                         uint byte_len,
                                        const String *from_str) {
    const char *from_ptr = from_str->ptr();
    size_t from_len = from_str->length();
    const char *from_end = from_ptr + from_len;
    size_t from_index = 0;
    uint from_item_len = 0;

    for (size_t i = 0; i < from_len; i += from_item_len) {
      from_item_len = byte_len_per_char(from_str, &from_ptr[i], from_end);
      if (byte_len == from_item_len && memcmp(source_ptr, &from_ptr[i], from_item_len) == 0)
        return from_index;
      else
        from_index ++;
    }

    return -1;
}

bool Item_func_translate::resolve_type(THD *thd) {
  if (agg_arg_charsets_for_string_result_with_comparison(collation, args, 3))
    return true;

  ulonglong char_length = args[0]->max_char_length();
  set_data_type_string(char_length);
  maybe_null = (maybe_null || max_length > thd->variables.max_allowed_packet);
  return false;
}

inline bool Item_func_translate::args_contain_null_value(void) {
      String tmp_value_0, tmp_value_1, tmp_value_2;
      args[0]->val_str(&tmp_value_0); // update the value of 'args[0]->null_value'
      args[1]->val_str(&tmp_value_1); // update the value of 'args[01]->null_value'
      args[2]->val_str(&tmp_value_2); // update the value of 'args[2]->null_value'

    if ( (null_value = args[0]->null_value)
        || (null_value = args[1]->null_value)
        || (null_value = args[2]->null_value))
        return true;
  return false;
}

inline bool Item_func_translate::args_contain_null_str(const String *source_str,
                                                const String *from_str,
                                                const String *to_str) {
    if (source_str == NULL
        || from_str == NULL
        || to_str == NULL)
        return false;

    if (source_str->length() == 0
        || from_str->length() == 0
        || to_str->length() == 0)
        return true;
  return false;
}

String *Item_func_translate::val_str(String *str) {
  DBUG_ASSERT(fixed == 1);

  if (args_contain_null_value()) return nullptr;

  String tmp_value, tmp_value2;
  String *source_str = args[0]->val_str(str);
  String *from_str = args[1]->val_str(&tmp_value);
  String *to_str = args[2]->val_str(&tmp_value2);

  if (args_contain_null_str(source_str, from_str, to_str))
      return make_empty_result();

  source_str->set_charset(collation.collation);
  tmp_value_res.length(0);
  tmp_value_res.set_charset(collation.collation);
  String *result = &tmp_value_res;

  const char *source_ptr = source_str->ptr();
  const size_t source_len = source_str->length();
  const char *source_end = source_ptr + source_len;
  const char *to_ptr = to_str->ptr();
  const char *to_end = to_ptr + to_str->length();

  while (source_ptr < source_end) {
    if (result->length() > current_thd->variables.max_allowed_packet) {
      return push_packet_overflow_warning(current_thd, func_name());
    }

    uint32 byte_len = byte_len_per_char(source_str, source_ptr, source_end);
    int from_index = find_in_from(source_ptr, byte_len, from_str);
    if (from_index == -1)
    {
      bool err = result->append(source_ptr, byte_len);
      if (err) return error_str();
      source_ptr += byte_len;
      continue;
    }

    const char *p = to_ptr;
    for (int i = 0; i < from_index; i++)
    {
      p += byte_len_per_char(to_str, p, to_end);
      if (p >= to_end) break;
    }

    if (p < to_end)
    {
      uint32 copy_len = byte_len_per_char(to_str, p, to_end);
      bool err = result->append(p, copy_len);
      if (err) return error_str();
    }

    source_ptr += byte_len;
  } // end of while()

  return result;
}
