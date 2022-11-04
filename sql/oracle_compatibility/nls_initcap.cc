#include "sql/sql_class.h"
#include "sql/oracle_compatibility/nls_initcap.h"

#if defined(HAVE_ZSQL_NLS_INITCAP) || defined(HAVE_ZSQL_INITCAP)

#ifdef HAVE_ZSQL_NLS_INITCAP
bool Item_func_oracle_nls_initcap::down_and_append(std::string *str_append) {
  const char *ptr = str_append->c_str();
  const char *end = ptr + str_append->length();
  m_temp_value.reserve(str_append->length());

  while (ptr <= end && *ptr != '\0') {
    if (!isspace(*ptr)) {
      if ('A' <= *ptr && 'Z' >= *ptr)
        m_temp_value.append(tolower(*ptr));
      else
        m_temp_value.append(*ptr);
    } else
      return false;
    ptr++;
  }
  return true;
}

bool Item_func_oracle_nls_initcap::format(String *str_format) {
  /* nls effective only when space apper on the left or right of equal sign
    and all the string*/
  char *str_split;
  String str_sort;
  String str_lang;

  str_split = strtok(str_format->ptr(), "=");
  str_sort = String(str_split, &my_charset_latin1);
  while (NULL != str_split) {
    str_split = (strtok(NULL, "="));
    if (NULL != str_split) str_lang = String(str_split, &my_charset_latin1);
  }

  std::string sstr_sort(str_sort.ptr());
  std::string sstr_lang(str_lang.ptr());

  boost::trim(sstr_sort);
  boost::trim(sstr_lang);
  if (!down_and_append(&sstr_sort)) return false;
  m_temp_value.append("=");
  if (!down_and_append(&sstr_lang)) return false;
  m_temp_value.append('\0');
  return true;
}

bool Item_func_oracle_nls_initcap::match_nls(String *str_nls) {
  const char *nls_zh = "nls_sort=chinese";
  const char *nls_en = "nls_sort=english";

  if (str_nls->is_empty()) return false;

  if (!format(str_nls)) return false;

  if (strcmp(m_temp_value.ptr(), nls_en) == 0 ||
      strcmp(m_temp_value.ptr(), nls_zh) == 0) {
    m_temp_value.mem_free();
    return true;
  }
  return false;
}
#endif

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
        str_cont->replace(case_count, size_t(1), tem_down);  // bug1
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

#ifdef HAVE_ZSQL_NLS_INITCAP
bool Item_func_oracle_nls_initcap::resolve_type(THD *) {
  maybe_null = true;

  if (agg_arg_charsets_for_string_result(collation, args, 1)) return true;
  DBUG_ASSERT(collation.collation != NULL);
  m_temp_value.set_charset(collation.collation);
  return false;
  }

String *Item_func_oracle_nls_initcap::val_str(String *str) {
  DBUG_ASSERT(arg_count >= 1);
  String nls;
  String *res;
  String *str_content = args[0]->val_str(str);

  if(str_content == NULL) {
    null_value = 1;
    return NULL;
  }
  std::string sstr(str_content->ptr());
  boost::trim(sstr);
  if (sstr.length() == 0) {
    res = str_content;
    return res;
  }


  if (arg_count == 2) {
    String *str_nls = args[1]->val_str(&nls);
    if (!match_nls(str_nls)) {
      ErrConvString err(str_nls);
      my_error(ER_INVALID_CHARACTER_STRING, MYF(0), "nls_inticap", err.ptr(),
               func_name());
      null_value = 1;
      return NULL;
    }
  }

  res = initcap(str_content);
  return res;
}
#endif /* HAVE_ZSQL_NLS_INITCAP */

#ifdef HAVE_ZSQL_INITCAP
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
#endif /* HAVE_ZSQL_NLS_INITCAP || HAVE_ZSQL_INITCAP  */
