#include "datetime_additive_op.h"
#include "sql/current_thd.h"  // current_thd
#include "sql/sql_time.h"     // Interval,date_add_interval_with_warn

static const int SECONDS_OF_DAY = 24 * 3600;

/**
  @brief Set the data value type calculated by these ITEMS.
         At least one of these ITEMS return types is a datetime type

  @param r0  args[0] return type.
  @param r1  args[1] return type.

  @retval false:   None of these ITEMS return type are datetime types.
  @retval true:  set data type successfully.
*/
bool Item_num_op::set_datetime_data_type(Item_result &r0, Item_result &r1) {
  switch (functype()) {
    case PLUS_FUNC:
      if (r0 == DATETIME_RESULT || r1 == DATETIME_RESULT) {
        set_data_type(MYSQL_TYPE_DATETIME);
        decimals = 0;
        max_length = MAX_DATETIME_WIDTH;
        hybrid_type = STRING_RESULT;

        return true;
      }
      break;

    default:
      break;
  }

  return false;
}

/**
  @brief Datetime calculation.

  @param ltime          [out] Final result.
  @param item_datetime  Datetime of participation in calculation.
  @param item_value     Real value of participation in calculation.
  @param neg            Sign bit, true: '-', false: '+'.

  @retval true:   error occurred.
  @retval false:  success.
*/
bool Item_func_additive_op::date_arithmetic(MYSQL_TIME *ltime,
                                            Item *item_datetime,
                                            Item *item_value, bool neg) {
  DBUG_ASSERT(ltime);
  DBUG_ASSERT(item_datetime);
  DBUG_ASSERT(item_value);

  if (item_datetime->get_date(ltime, TIME_NO_ZERO_DATE)) {
    null_value = true;
    return true;
  }

  Interval interval;
  memset(&interval, 0, sizeof(interval));

  double arg_value = item_value->val_real();

  if (item_value->null_value) {
    null_value = true;
    return true;
  }

  /*
   * dt + 1  neg = false ==> arg_value = 1 interval.neg = false
   * dt - 1  neg = true  ==> arg_value = 1 interval.neg = true
   * dt + -1 neg = false ==> arg_value = 1 interval.neg = true
   * dt - -1 neg = true  ==> arg_value = 1 interval.neg = false
   */
  interval.neg = neg;

  if (arg_value < 0.0) {
    arg_value = -arg_value;

    interval.neg = !interval.neg;
  }

  /*
   * get day and second.
   * Notice second should round. 32.49=>32 ; 21.84=>22
   */
  interval.day = static_cast<unsigned long>(arg_value);
  interval.second = static_cast<unsigned long>(
      rint((arg_value - interval.day) * SECONDS_OF_DAY));

  null_value = date_add_interval_with_warn(current_thd, ltime,
                                           INTERVAL_DAY_SECOND, interval);
  if (null_value) return true;
  return false;
}

/**
  @brief Prepare for datetime calculation.

  @param ltime          [out] Final result.

  @retval true:   error occurred.
  @retval false:  success.
*/
bool Item_func_additive_op::date_op(MYSQL_TIME *ltime,
                                    my_time_flags_t /*fuzzy_date*/) {
  DBUG_ASSERT(fixed);
  DBUG_ASSERT(ltime);
  DBUG_ASSERT(functype() == PLUS_FUNC);

  if (args[0]->type() == NULL_ITEM || args[1]->type() == NULL_ITEM) {
    null_value = true;
    return true;
  }

  Item_result r0 = args[0]->numeric_context_result_type();
  Item_result r1 = args[1]->numeric_context_result_type();
  DBUG_ASSERT(r0 == DATETIME_RESULT || r1 == DATETIME_RESULT);

  if (r0 == DATETIME_RESULT && r1 == DATETIME_RESULT) {
    my_error(ER_DISALLOWED_OPERATION, MYF(0), "Plus",
             "multiple to_date/to_timestamp functions");
    return true;
  }

  Item *args0;
  Item *args1;

  if (r0 == DATETIME_RESULT) {
    args0 = args[0];
    args1 = args[1];
  } else {
    args0 = args[1];
    args1 = args[0];
  }

  return date_arithmetic(ltime, args0, args1, false);
}

/**
  @brief return numeric result type.
*/
enum Item_result Item_func_additive_op::numeric_context_result_type() const {
  if (data_type() == MYSQL_TYPE_DATETIME) return DATETIME_RESULT;

  return Item::numeric_context_result_type();
}
