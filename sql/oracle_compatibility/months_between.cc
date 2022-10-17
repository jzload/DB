#include "sql/oracle_compatibility/months_between.h"
#include "sql/oracle_compatibility/convert_datetime.h"
#include <cmath>

/*
 * Refine the difference of months between two time to 10 decimal places.
 * Non-refined value has the error caused by double calculation.
 * 1 second is about 3e-7 months.
 * 10 decimal places is enough for months diff.
 *
 * input: the value to be refined
 *
 * return: refined value
 */
static double refine_months_diff_precise(double oldval) {
  longlong keep_place = 10000000000;
  return static_cast<double>(round(oldval * keep_place)) / keep_place;
}

/*
 * Calculate the difference of months between two time, accurate to sencond.
 *
 * input: ltime1, ltime2
 *
 * return: the difference of months
 */
static double cal_months_diff(const MYSQL_TIME &ltime1, const MYSQL_TIME &ltime2) {
  double month1 = ltime1.year * 12 + ltime1.month + ltime1.day / 31.0 +
                  (ltime1.hour + ltime1.minute / 60.0 + ltime1.second / 3600.0) / 744.0;
  double month2 = ltime2.year * 12 + ltime2.month + ltime2.day / 31.0 +
                  (ltime2.hour + ltime2.minute / 60.0 + ltime2.second / 3600.0) / 744.0;

  return refine_months_diff_precise(month1 - month2);
}

/*
 * Calculate the difference of months between two time,
 * discard the message of day, hour, minute and sencond.
 *
 * input: ltime1, ltime2
 *
 * return: the difference of months
 */
static int cal_months_diff_no_days(const MYSQL_TIME &ltime1, const MYSQL_TIME &ltime2) {
  int month1 = ltime1.year * 12 + ltime1.month;
  int month2 = ltime2.year * 12 + ltime2.month;
  return (month1 - month2);
}

/*
 * Check whether the ltime is the last day of month.
 *
 * input: ltime
 *
 * return:
 * true: is the last day of month
 * false: not the last day of month
 */
inline bool is_last_day_of_month(const MYSQL_TIME &ltime) {
  return ltime.day == static_cast<uint>(get_max_days_of_month(ltime.year,ltime.month));
}

/*
 * Check whether the two time are either the same days of the month
 * or both last days of months.
 *
 * input: ltime1, ltime2
 *
 * return:
 * true: the same day in months or both last days of months
 * false: not the same
 */
static bool same_day_in_month(const MYSQL_TIME &ltime1, const MYSQL_TIME &ltime2)
{
  if (ltime1.day == ltime2.day)
    return true;

  if (is_last_day_of_month(ltime1) && is_last_day_of_month(ltime2))
    return true;

  return false;
}

/*
 * Check whether the two MySQL time is the right format of date.
 *
 * In function get_max_days_of_month which is called in next step,
 * the year in MySQL time is Constrained to 1~9999 and month to 1~12.
 *
 * The year 0000 can be resolved to date but should not be calculated in months_between func.
 * The month not in 1~12 cannot convert to a MySQL time normally,
 * but the judgement of month also done, just in case.
 *
 * input: ltime1, ltime2
 *
 * return:
 * true: wrong format
 * false: right format
 */
static bool check_date_format(const MYSQL_TIME &ltime1, const MYSQL_TIME &ltime2)
{
  if (ltime1.year < 1 || ltime1.year > 9999 ||
      ltime2.year < 1 || ltime2.year > 9999)
  {
    my_error(ER_CAL_MONTHS_BETWEEN_FAIL, MYF(0),
               "year must be between 1 and 9999");
    return true;
  }

  if (ltime1.month < 1 || ltime1.month > 12 ||
      ltime2.month < 1 || ltime2.month > 12)
  {
    my_error(ER_CAL_MONTHS_BETWEEN_FAIL, MYF(0),
               "month must be between 1 and 12");
    return true;
  }

  return false;
}

double Item_func_months_between::val_real() {
  MYSQL_TIME ltime1, ltime2;
  double res = 0;

  if (args[0]->get_date(&ltime1, TIME_NO_ZERO_DATE) ||
      args[1]->get_date(&ltime2, TIME_NO_ZERO_DATE))
  {
    my_error(ER_CAL_MONTHS_BETWEEN_FAIL, MYF(0),
               "cannot convert parameters to date");
    return 0.0;
  }

  if (check_date_format(ltime1,ltime2))
    return 0.0;

  if (same_day_in_month(ltime1,ltime2))
  {
      res = static_cast<double>(cal_months_diff_no_days(ltime1,ltime2));
  }
  else {
      res = cal_months_diff(ltime1,ltime2);
  }
  return check_float_overflow(res);
}
