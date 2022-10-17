
#include "sql/set_var.h"
#include "sql/sql_class.h"   // set_var.h: THD
#include "sql/sql_locale.h"  // MY_LOCALE my_locale_en_US
#include "sql/sql_time.h"
#include "sql/strfunc.h"  // check_word

#include "m_ctype.h"
#include "m_string.h"  // strend

#include "my_dbug.h"
#include "sql/current_thd.h"
#include "sql/derror.h"
#include "sql/oracle_compatibility/convert_number.h"
#include "sql/oracle_compatibility/to_number.h"

using std::max;
using std::min;

String *Item_func_to_number::val_str(String *str) {
  my_decimal tmp_buf, *tmp = val_decimal(&tmp_buf);
  if (null_value || NULL == tmp) return NULL;
  uint8 fraction = get_actual_fraction_dec_val(tmp);
  if (fraction != decimals)
    my_decimal_round(E_DEC_FATAL_ERROR, tmp, fraction, false, tmp);
  my_decimal2string(E_DEC_FATAL_ERROR, tmp, 0, 0, 0, str);
  return str;
}

double Item_func_to_number::val_real() {
  my_decimal tmp_buf, *tmp = val_decimal(&tmp_buf);
  double res;
  if (null_value || NULL == tmp) return 0.0;
  my_decimal2double(E_DEC_FATAL_ERROR, tmp, &res);
  return res;
}

longlong Item_func_to_number::val_int() {
  my_decimal tmp_buf, *tmp = val_decimal(&tmp_buf);
  longlong res;
  if (null_value || NULL == tmp) return 0;
  my_decimal2int(E_DEC_FATAL_ERROR, tmp, unsigned_flag, &res);
  return res;
}

my_decimal *Item_func_to_number::val_decimal(my_decimal *dec) {
  DBUG_ASSERT(arg_count == 1 || arg_count == 2);

  reset_item();

  if (arg_count == 1)
    return val_dec_one_param(dec);
  else
    return val_dec_two_params(dec);
}

bool Item_func_to_number::is_decimal_value_null(my_decimal *tmp) {
  // Process the value NULL, 2020/0, will ouput NULL
  if ((NULL == tmp) || (args[0]->null_value)) {
    null_value = true;
    return true;
  }

  return false;
}

bool Item_func_to_number::is_str_value_null(String *pStr) {
  // Process the value "", will ouput NULL
  if ((NULL == pStr) || (0 == pStr->length())) {
    null_value = true;
    return true;
  }

  return false;
}

bool Item_func_to_number::check_two_params_null(String *fmt_str) {
  if (null_value) return true;

  if (NULL == fmt_str) {
    null_value = true;
    return true;
  }
  return false;
}

my_decimal *Item_func_to_number::get_decimal_value(my_decimal *tmp,
                                                   my_decimal *dec) {
  uint precision;
  uint8 tmp_decimals;
  uint8 tmp_precision;

  // Reset the decimal data type precision and decimals.
  tmp_decimals = static_cast<uint8>(decimal_actual_fraction(tmp));
  tmp_precision = static_cast<uint8>(my_decimal_intg(tmp) + tmp_decimals);

  if (tmp_precision > DECIMAL_MAX_PRECISION) {
    my_error(ER_TO_NUM_OUT_OF_RANGE, MYF(0),
             "my_decimal overflow: bigger precision");
    goto err;
  }
  if (tmp_decimals > DECIMAL_MAX_SCALE) {
    my_error(ER_TO_NUM_OUT_OF_RANGE, MYF(0),
             "my_decimal overflow: bigger scale");
    goto err;
  }

  if (get_reset_decimal_flag() == NEED_RESET_DEC)
    set_data_type_decimal(tmp_precision, tmp_decimals);

  // This function may push warning or error.
  my_decimal_round(E_DEC_FATAL_ERROR, tmp, decimals, false, dec);

  precision =
      my_decimal_length_to_precision(max_length, decimals, unsigned_flag);

  if (precision - decimals < (uint)my_decimal_intg(dec)) {
    my_error(ER_TO_NUM_OUT_OF_RANGE, MYF(0), "smaller precision-decimals");
    goto err;
  }

  return dec;

err:
  my_decimal_set_zero(dec);
  return dec;
}

my_decimal *Item_func_to_number::val_dec_one_param(my_decimal *dec) {
  // check the value string valid
  if (false == is_numeric_type(args[0]->data_type())) {
    char buf[64];
    String tmpStr0(buf, sizeof(buf), &my_charset_bin);
    String *pStr0 = args[0]->val_str(&tmpStr0);

    if (is_str_value_null(pStr0)) return NULL;

    if (parse_to_num_one_param_str(pStr0->c_ptr_safe(), pStr0->length())) {
      my_decimal_set_zero(dec);
      return dec;
    }
  }

  my_decimal tmp_buf, *tmp = args[0]->val_decimal(&tmp_buf);
  if (is_decimal_value_null(tmp)) return NULL;

  return get_decimal_value(tmp, dec);
}

String *Item_func_to_number::get_value_param_str(String *arg_str) {
  my_decimal tmp_buf;
  String *pStr0 = NULL;

  if (is_numeric_type(args[0]->data_type())) {
    my_decimal *tmp = args[0]->val_decimal(&tmp_buf);
    if (is_decimal_value_null(tmp)) return NULL;

    my_decimal2string(E_DEC_FATAL_ERROR, tmp, 0, 0, 0, arg_str);
    pStr0 = arg_str;
  } else {
    pStr0 = args[0]->val_str(arg_str);
  }

  if (is_str_value_null(pStr0)) return NULL;

  return pStr0;
}

String *Item_func_to_number::get_format_param_str(String *arg_str) {
  String *pStr1 = args[1]->val_str(arg_str);
  return pStr1;
}

my_decimal *Item_func_to_number::val_dec_two_params(my_decimal *dec) {
  my_decimal tmp_buf1;
  char *out_value = NULL;

  /* Avoid dynamic malloc mem for small string. */
  char buf1[64];
  char buf2[64];
  String args0_str(buf1, sizeof(buf1), &my_charset_bin);
  String args1_str(buf2, sizeof(buf2), &my_charset_bin);

  String *pstr0, *pstr1;

  pstr0 = get_value_param_str(&args0_str);
  pstr1 = get_format_param_str(&args1_str);
  if (check_two_params_null(pstr1) || NULL == pstr0) {
    my_decimal_set_zero(dec);
    return NULL;
  }

  if (NULL == current_thd || NULL == current_thd->mem_root) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0),
             "to_number get current_thd->mem_root failed");
    my_decimal_set_zero(dec);
    return dec;
  }

  /* thd->mem_root mem will be released by thd.
  We need at least not more than double lenth of pstr0 bytes to save the
  output. When the fmt is X, there are some sitiation that we need double lenth.
  And we extra three position: one for sign, one for decimal point, and the last
  for terminal null('\0').
  */
  size_t out_len = pstr0->length() * 2 + 1 + 1 + 1;
  out_value = (char *)current_thd->mem_root->Alloc(out_len);
  if (NULL == out_value) {
    my_error(ER_TO_NUM_VALUE_INVALID, MYF(0), "to_number value malloc error");
    my_decimal_set_zero(dec);
    return dec;
  }

  if (process_to_num_two_params(pstr0, pstr1, out_value, out_len)) {
    my_decimal_set_zero(dec);
    return dec;
  }

  // This function may push warning or error.
  str2my_decimal(E_DEC_FATAL_ERROR, out_value,
                 my_strnlen(out_value, TO_NUM_MAX_STR_LEN - 1),
                 collation.collation, &tmp_buf1);

  return get_decimal_value(&tmp_buf1, dec);
}
