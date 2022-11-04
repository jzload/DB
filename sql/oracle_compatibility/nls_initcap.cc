#include "sql/sql_class.h"
#include "sql/oracle_compatibility/nls_initcap.h"

#ifdef HAVE_ZSQL_INITCAP

String *Item_func_oracle_initcap::initcap(String *&str_cont) {
  const char *ptr = str_cont->ptr();
  const char *end = ptr + str_cont->length();
  bool flag_previous = true;
  size_t case_count = 0;

  while (ptr <= end) {
    if (('A' <= *ptr && 'Z' >= *ptr) || ('a' <= *ptr && 'z' >= *ptr)) {
      if (flag_previous == true) {
        String tem_up;
        tem_up.append(toupper(*ptr));
        str_cont->replace(case_count, size_t(1), tem_up);
        flag_previous = false;
        case_count++;
        ptr++;
      } else {
        String tem_down;
        tem_down.append(tolower(*ptr));
        str_cont->replace(case_count, size_t(1), tem_down);
        flag_previous = false;
        case_count++;
        ptr++;
      }
    } else if ('0' <= *ptr && '9' >= *ptr) {
      flag_previous = false;
      case_count++;
      ptr++;
    } else {
      flag_previous = true;
      case_count++;
      ptr++;
    }
  }
  return str_cont;
}

bool Item_func_oracle_initcap::resolve_type(THD *) {
  maybe_null = true;

  if (agg_arg_charsets_for_string_result(collation, args, 1)) return true;
  return false;
}

String *Item_func_oracle_initcap::val_str(String *con) {
  DBUG_ASSERT(arg_count == 1);
  String *res;
  String *str_content = args[0]->val_str(con);

  if(str_content == NULL){
    null_value = 1;
    return NULL;
  }
  std::string sstr(str_content->ptr());
  boost::trim(sstr);
  if (sstr.length() == 0) {
    res = str_content;
    return res;
  }

  res = initcap(str_content);
  return res;
}

#endif /* HAVE_ZSQL_INITCAP */
