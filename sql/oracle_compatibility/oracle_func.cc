
#include "sql/sql_class.h"
#include "sql/set_var.h"
#include "sql/sp_head.h"
#include "sql/item_inetfunc.h"
#include "sql/sql_time.h"
#include "sql/oracle_compatibility/to_char.h"
#include "sql/oracle_compatibility/oracle_func.h"
#include "sql/parse_tree_items.h"
#include <string>
#include <stdlib.h>
#include <math.h>


/**
 Trim the corresponding left space

'    123456.123456' --> '123456.123456'

*/
static void trim_left_space(String *str) {
  if (nullptr == str)
    return;

  char *pstr = str->ptr();
  size_t length = str->length();
  size_t pos = 0;

  if (nullptr == pstr || 0 == length)
    return;

  while (length > 0 && ' ' == pstr[pos]) {
    --length;
    ++pos;
  }

  if (pos > 0) {
    memmove(pstr, pstr+pos, length);
    str->length(length);
  }

  return;
}

/**
Delete the corresponding right space

'123456.123456    ' --> '123456.123456'

*/
static void trim_right_space(String *str) {
  if (nullptr == str)
    return;

  char *pstr = str->ptr();
  size_t length = str->length();

  if (nullptr == pstr || 0 == length)
    return;

  pstr += length -1;
  while (length > 0 && ' ' == *pstr) {
    --length;
    --pstr;
  }

  str->length(length);
  return;
}

static void trim_space(String *str) {
  trim_left_space(str);
  trim_right_space(str);
}

/**
 Delete the right characters

String must be a number string with spaces removed

@param trim_len  (input param)
  The user guarantees that this parameter is correct

'123456.123000' --> '123456.12300'  trim_len=1
'123456.123000' --> '123456.1230'   trim_len=2
'123456.123000' --> '123456.123'    trim_len=3

*/
static bool trim_right_chars(String *str, size_t trim_len) {
  if (nullptr == str)
    return false;

  if (0 == trim_len)
    return true;

  char *pstr = str->ptr();
  size_t length = str->length();

  if (nullptr == pstr || trim_len > length)
    return false;

  length -= trim_len;
  str->length(length);

  return true;
}

/**
  @brief  fill convert format string

  @param[out]  pFormatBuf    format string buffer
  @param[in]   format        format character
  @param[in]   pFormatPos    the new convert format character position

  @retval  void

*/

static void strToCharSetFormat(char *pFormatBuf, char format, uint32 *pFormatPos)
{
  DBUG_ASSERT(NULL != pFormatPos);

  /*2 characters are added, and the end symbol is retained*/
  if (*pFormatPos > MAX_TOCHAR_CONVERT_FMT_LEN-2-1)
    return;

  pFormatBuf[*pFormatPos] = '%';
  *pFormatPos += 1;
  pFormatBuf[*pFormatPos] = format;
  *pFormatPos += 1;
  return;
}


/**
  @brief  Judge the validity of fractional seconds
          and calculate the length of fractional seconds

  @param[in out]  iCurrLen    current position of format string
  @param[in]      iTotalLen   total length of format string
  @param[in]      ptr         String pointer
  @param[in]      msLen       length of fractional seconds

  @retval  false  Error.
  @retval  true   success.

*/

static bool strToCharffLen(uint32 *iCurrLen, uint32 iTotalLen, char *ptr, uint32 *msLen)
{
  char ff_char_1 = '\0', ff_char_2 = '\0';

  if (*iCurrLen + 2 < iTotalLen)
    ff_char_1 = ptr[*iCurrLen + 2];
  if (*iCurrLen + 3 < iTotalLen)
    ff_char_2 = ptr[*iCurrLen + 3];

  /*
    Fractional second, maximum support 9 bits, other illegal input
    illegal input: ff0 ff10 ff19 and so on
  */

  if ((ff_char_1 >= '0') && (ff_char_1 <= '9'))
  {
    if (('0' == ff_char_1) || ((ff_char_2 >= '0') && (ff_char_2 <= '9')))
      return false;

    *msLen = static_cast<unsigned int>(ff_char_1 - '0') ; // trans digit char to number
    *iCurrLen += 3;
  }
  else
  {
    *iCurrLen += 2;
    *msLen = 0;
  }

  return true;
}

bool strToCharMonth(T_TStrOracleToMysql *pStrOracleToMysql)
{
   if(NULL == pStrOracleToMysql)
     return false;

   uint32 iCurrLenTemp = *(pStrOracleToMysql->iCurrLen);
   uint32 iFormatTemp = *(pStrOracleToMysql->iFormatLen);
   uint32 iTotalLen = pStrOracleToMysql->iTotalLen;
   char   *ptr = pStrOracleToMysql->ptr;
   char   *ptoStr = pStrOracleToMysql->ptoStr;


   if ('M' == ptr[iCurrLenTemp + 1])
   {
      strToCharSetFormat(ptoStr, 'm', &iFormatTemp);
      iCurrLenTemp += 2;
      *(pStrOracleToMysql->iCurrLen) = iCurrLenTemp;
      *(pStrOracleToMysql->iFormatLen) = iFormatTemp;
      return true;
   }
   else if ('O' == ptr[iCurrLenTemp+1])
   {
      if (iCurrLenTemp+2 < iTotalLen &&  'N' == ptr[iCurrLenTemp + 2])
      {
         if (iCurrLenTemp+3 < iTotalLen && 'T' == ptr[iCurrLenTemp + 3])
         {
           if(iCurrLenTemp+4 < iTotalLen &&  'H' == ptr[iCurrLenTemp + 4])
           {
              strToCharSetFormat(ptoStr, 'M', &iFormatTemp);
              iCurrLenTemp += 5;
            }
          }
          else
          {
             iCurrLenTemp += 3;
             strToCharSetFormat(ptoStr, 'b', &iFormatTemp);
          }
      }
      *(pStrOracleToMysql->iCurrLen) = iCurrLenTemp;
      *(pStrOracleToMysql->iFormatLen) = iFormatTemp;
      return true;
   }

   return false;
}

/**
  set FM format

  @param[in out]  pStrOracleToMysql

  @retval  void

*/
static void set_fm_format(T_TStrOracleToMysql *pStrOracleToMysql) {

  *(pStrOracleToMysql->iCurrLen) = *(pStrOracleToMysql->iCurrLen) + 2;
  strToCharSetFormat(pStrOracleToMysql->ptoStr, 'F',
                     pStrOracleToMysql->iFormatLen);

  return;
}

/**
  set FF format

  @param[in out]  pStrOracleToMysql

  @retval  false  Error.
  @retval  true   success.

*/
static bool set_ff_format(T_TStrOracleToMysql *pStrOracleToMysql){

  if(!strToCharffLen(pStrOracleToMysql->iCurrLen,
                     pStrOracleToMysql->iTotalLen,
                     pStrOracleToMysql->ptr,
                     pStrOracleToMysql->msLen))
     return false;

  strToCharSetFormat(pStrOracleToMysql->ptoStr, 'f',
                     pStrOracleToMysql->iFormatLen);

  return true;
}

/**
  processing format string with F, like FF/FM

  @param[in out]  pStrOracleToMysql

  @retval  false  Error.
  @retval  true   success.

*/
static bool strToCharWithF(T_TStrOracleToMysql *pStrOracleToMysql)
{
  DBUG_ASSERT(NULL != pStrOracleToMysql);

  uint32 iCurrLenTemp = *(pStrOracleToMysql->iCurrLen);
  uint32 iTotalLen = pStrOracleToMysql->iTotalLen;
  char   *ptr = pStrOracleToMysql->ptr;

  if (iCurrLenTemp > iTotalLen - 2)
    return false;

  if ('F' == ptr[iCurrLenTemp + 1])
    return set_ff_format(pStrOracleToMysql);
  else if ('M' == ptr[iCurrLenTemp + 1])
    set_fm_format(pStrOracleToMysql);
  else
    return false;

  return true;
}

char *StrToUpper(char *ptr) {
  char *res = ptr;
  if (NULL == ptr) {
    return NULL;
  }

  while ('\0' != *ptr) {
    if (*ptr >= 'a' && *ptr <= 'z') {
      *ptr = (char)((int )(*ptr)-32);
    }
    ptr++;
  }
  return res;
}

bool subYearString(uint32 *pCurrLen, uint32 *pTotalLen, uint32 *piformat, char *ptr,
                   char *ptoStr, uint *pYearFlag) {
  if (*pCurrLen + 1 < *pTotalLen && 'Y' == ptr[*pCurrLen + 1]) {  // 2 y
    if ((*pCurrLen + 2 < *pTotalLen && 'Y' != ptr[*pCurrLen + 2]) ||
        (*pCurrLen + 2 >= *pTotalLen)) {  // 2 y
       strToCharSetFormat(ptoStr, 'y', piformat);
      *pCurrLen += 2;
      *pYearFlag = 2;
      return true;
    } else {
      if ((*pCurrLen + 3 < *pTotalLen && 'Y' != ptr[*pCurrLen + 3]) ||
          (*pCurrLen + 3 >= *pTotalLen)) {  // 3y  yyy
         strToCharSetFormat(ptoStr, 'Y', piformat);
        *pCurrLen += 3;
        *pYearFlag = 3;
        return true;
      } else {
        if (*pCurrLen+4 < *pTotalLen && 'Y' == ptr[*pCurrLen + 4])  // 5个 yyyyy
          return false;

         strToCharSetFormat(ptoStr, 'Y', piformat);

        *pCurrLen += 4;
        *pYearFlag = 4;
        return true;
      }
    }
  } else {  /* to_date('1', 'Y')转换成4位年 */
     strToCharSetFormat(ptoStr, 'Y', piformat);
    *pCurrLen += 1;
    *pYearFlag = 1;
    return true;
  }
  return false;
}

bool subHourString(uint32 *pCurrLen, uint32 *pTotalLen, uint32 *piformat,
                   char *ptr, char *ptoStr) {
  if (*pCurrLen + 1 < *pTotalLen && 'H' == ptr[*pCurrLen + 1]) {
    if (*pCurrLen + 2 < *pTotalLen && '2' == ptr[*pCurrLen + 2]) {
      if (*pCurrLen + 3 < *pTotalLen && '4' == ptr[*pCurrLen + 3]) {
         strToCharSetFormat(ptoStr, 'H', piformat);
        *pCurrLen += 4;
      } else
        return false;
    } else {
       strToCharSetFormat(ptoStr, 'h', piformat);
      *pCurrLen += 2;
    }
    return true;
  }
  return false;
}


/**
initial strOracleToMysql struct variable

@param[out]  strOracleToMysql
@param[in]   iTotalLen    total length of format string
@param[in]   ptr          String pointer
@param[in]   formatStr    new format string pointer
@param[in]   msLen        length of fractional seconds

@retval  void.

*/
static void init_oracle_to_mysql(
  T_TStrOracleToMysql &strOracleToMysql,
  uint32 iTotalLen,
  char *ptr,
  char *formatStr,
  uint32 *msLen) {

  strOracleToMysql.iTotalLen  = iTotalLen;
  strOracleToMysql.ptr        = ptr;
  strOracleToMysql.ptoStr     = formatStr;
  strOracleToMysql.msLen      = msLen;
  strOracleToMysql.iCurrLen   = 0;
  strOracleToMysql.iFormatLen = 0;

  return;
}

/**
set strOracleToMysql struct variable

@param[out]  strOracleToMysql

@param[in]    iCurrLen    current position of format string
@param[in]    iformat     position of new format string

@retval  void.

*/
static void set_oracle_to_mysql(
  T_TStrOracleToMysql &strOracleToMysql,
  uint32 *iCurrLen,
  uint32 *iformat) {

  strOracleToMysql.iCurrLen   = iCurrLen;
  strOracleToMysql.iFormatLen = iformat;
  return;
}


/**
  Main process to_char(datetime)

  @param[out]    pfromStr       string
  @param[in]     ptoStr         new String pointer
  @param[in]     pYearFlag      not use now
  @param[in]     pwForMatType   not use now
  @param[in]     msLen          length of fractional seconds

  @retval  false  Error.
  @retval  true   success.

*/

bool OracleStrToMysqlStr(LEX_STRING &pfromStr, String *ptoStr, uint *pYearFlag,
                         uint32 *pwForMatType, uint32 *msLen) {

  if (0 == pfromStr.length || NULL == ptoStr) {
    return false;
  }
  uint32 iTotalLen = (uint32)pfromStr.length;
  uint32 iCurrLen  = 0;
  char *ptr        = pfromStr.str;
  char formatStr[MAX_TOCHAR_CONVERT_FMT_LEN] = {0};
  uint32 iformat = 0;
  T_TStrOracleToMysql  strOracleToMysql;

  if (iTotalLen > MAX_TOCHAR_FORMAT_LEN)
    return false;

  ptr = StrToUpper(ptr);
  if (NULL == ptr) {
    return false;
  }

  init_oracle_to_mysql(strOracleToMysql, iTotalLen, ptr, formatStr, msLen);

  while (iCurrLen < iTotalLen && '\0' != ptr[iCurrLen]) {
    switch (ptr[iCurrLen]) {
      case '/':
      case '-':
      case ':':
      case ' ':
      case ',':
      case '.':
      case ';':
      case '~':
      case '!':
      case '@':
      case '#':
      case '$':
      case '^':
      case '&':
      case '*':
      case '_':
      case '+': {
        formatStr[iformat] = ptr[iCurrLen];
        iformat++;
        iCurrLen++;
        break;
      }
      case 'Y': {
        if (subYearString(&iCurrLen, &iTotalLen, &iformat, ptr, formatStr, pYearFlag)) {
          *pwForMatType |= (uint32)1;
          break;
        }
        return false;
      }
      case 'D': {
        if (iCurrLen+1 > iTotalLen)
          return false;

        if ('D' == ptr[iCurrLen + 1]) {
          if (iCurrLen + 2 < iTotalLen && 'D' == ptr[iCurrLen + 2]) {  // ddd
            return false;
          } else {
            strToCharSetFormat(formatStr, 'd', &iformat);
            iCurrLen += 2;
          }

          *pwForMatType |= (uint32)4;
          break;
        } else {
          strToCharSetFormat(formatStr, 'w', &iformat);
          iCurrLen += 1;
          break;
        }
        return false;
      }
      case 'H': {
        if (subHourString(&iCurrLen, &iTotalLen, &iformat, ptr, formatStr)) {
          break;
        }
        return false;
      }
      case 'M': {
        if (iCurrLen+1 >= iTotalLen)
          return false;
        if ('I' == ptr[iCurrLen + 1]) {
          strToCharSetFormat(formatStr, 'i', &iformat);
          iCurrLen  += 2;
          break;
        }

        set_oracle_to_mysql(strOracleToMysql, &iCurrLen, &iformat);
        if(strToCharMonth(&strOracleToMysql)) {
            iCurrLen = *(strOracleToMysql.iCurrLen);
            iformat  = *(strOracleToMysql.iFormatLen);
            *pwForMatType |= (uint32)2;
            break;
        }
        return false;
      }
      case 'S': {
        if( iCurrLen + 1 < iTotalLen && 'S' == ptr[iCurrLen + 1]) {
          strToCharSetFormat(formatStr, 's', &iformat);
          iCurrLen += 2;
          break;
        }
        return false;
      }
      case 'F': {
        set_oracle_to_mysql(strOracleToMysql, &iCurrLen, &iformat);
        if(strToCharWithF(&strOracleToMysql)) {
            iCurrLen = *(strOracleToMysql.iCurrLen);
            iformat  = *(strOracleToMysql.iFormatLen);
            break;
        }
        return false;
      }
      case 'Q': {
        strToCharSetFormat(formatStr, 'q', &iformat);
        iCurrLen += 1;
        break;
      }
      default: {
        return false;
      }
    }
  }

  formatStr[MAX_TOCHAR_CONVERT_FMT_LEN-1] = '\0';
  if(0 == strnlen(formatStr, MAX_TOCHAR_CONVERT_FMT_LEN-1))
    return false;

  ptoStr->append(formatStr);

  return true;
}


bool handleEEEE(T_TSCICAL &tSciCal) {
  int fir_point_front_len = tSciCal.fir_point_front_len;
  int fir_point_back_len = tSciCal.fir_point_back_len;
  int fir_zero_back_len = tSciCal.fir_zero_back_len;
  char *pstr_fir = tSciCal.fir_str;
  // int sec_point_front_len =  tSciCal.sec_point_front_len;
  int sec_point_back_len = tSciCal.sec_point_back_len;
  String *ret = tSciCal.ret;

  int i = 0;

  //0.XXXXX
  if (pstr_fir[i] == '0' && fir_point_front_len == 1) {
    i = 2;
    int rightzeronum = fir_point_back_len-fir_zero_back_len;

    //TO_CHAR(0.0000,'9.999EEEE')
    if (pstr_fir[i+rightzeronum] == '\0') {
      ret->append(' ');
      ret->append('.');

      while (sec_point_back_len != 0) {
        ret->append('0');
        sec_point_back_len--;
      }
      ret->append("E+00");
    } else {
      /* 处理位数相等 不进位的场景 */
      if(fir_zero_back_len == sec_point_back_len+1) {  // 0.0999,'9.99EEE'
        //int rightzeronum = fir_point_back_len-fir_zero_back_len;
        i = i + rightzeronum;

        if (sec_point_back_len == 0) {
            ret->append(pstr_fir[i]);
            ret->append("E-");
            char a[4]={0};
            ret->append(AppendSciCalTail(rightzeronum+2,a));
            return true;
        }

        ret->append(pstr_fir[i]);
        ret->append('.');
        i++;
        fir_zero_back_len--;
        while (fir_zero_back_len != 0) {
            ret->append(pstr_fir[i]);
            fir_zero_back_len--;
            i++;
        }
        ret->append("E-");
        char a[4] = {0};
        ret->append(AppendSciCalTail(rightzeronum + 2, a));
      } else if (fir_zero_back_len < sec_point_back_len + 1) {
        /**
          前面位数小 TO_CHAR(0.0999,'9.9999EEEE') TO_CHAR(0.09,'9.99EEEE')
          TO_CHAR(0.099,'9.999EEEE')
        */
        i = i + rightzeronum;

        ret->append(pstr_fir[i]);
        ret->append('.');
        i++;
        fir_zero_back_len--;
        while (fir_zero_back_len != 0) {
            ret->append(pstr_fir[i]);
            fir_zero_back_len--;
            sec_point_back_len--;
            i++;
        }

        while (sec_point_back_len != 0) {
            ret->append('0');
            sec_point_back_len--;
        }
        ret->append("E-");
        char a[4]={0};
        ret->append(AppendSciCalTail(rightzeronum + 2, a));
      } else {
        /* 前面位数大 TO_CHAR(0.09999,'9.99EEEE') */
        int pos =
            -(fir_point_back_len - fir_zero_back_len+1 + sec_point_back_len + 1);
        char d_new[MAX_TOCHAR_SRC_LEN] = {0};
        RoundingNum(pstr_fir,d_new,pos);

        int omitpos = 0;
        if (pos == -2 && d_new[0] == '1') {
          d_new[1] = '\0';
        } else {
          if (pstr_fir[2 + rightzeronum] == '9' && d_new[1+rightzeronum] == '1') {
            // to_char(0.0999,'9.9')
            omitpos = (-pos);
          } else {
            // 0.00005 2+5  to_char(0.006645,'9.99')
            omitpos = 1 + (-pos);
          }

          d_new[omitpos] ='\0';
        }

        T_TSCICAL tAnotherSciCal;
        tAnotherSciCal.sec_point_front_len = tSciCal.sec_point_front_len;
        tAnotherSciCal.sec_point_back_len = tSciCal.sec_point_back_len;
        tAnotherSciCal.ret = tSciCal.ret;
        if (pos >= 0)
          tAnotherSciCal.omit_len = pos + 1;
        else
          tAnotherSciCal.omit_len = 0;

        T_TSrcAttr tSrcAttr;
        if (!MarkSrcAttribute(d_new, tSrcAttr)) {
          return false;
        }
        tAnotherSciCal.fir_str = d_new;
        tAnotherSciCal.fir_point_front_len = tSrcAttr.iSrc_point_front_len;
        tAnotherSciCal.fir_point_back_len = tSrcAttr.iSrc_point_back_len;
        tAnotherSciCal.fir_zero_back_len = tSrcAttr.iSrc_zero_back_len;
        // InitSciCalInfo(NULL,d_new,true,tAnotherSciCal);
        handleEEEE(tAnotherSciCal);
      }
    }
  } else {
    // 1.xxxx
    // TO_CHAR(5,'9.999EEEE')  TO_CHAR(5,'9EEEE') + omit_len = 2
    if (pstr_fir[i + 1] == '\0') {
      if (tSciCal.omit_len == 0) {
        ret->append(pstr_fir[i]);
        ret->append('.');
        while (sec_point_back_len != 0) {
          ret->append('0');
          sec_point_back_len--;
        }
        ret->append("E+00");
        return true;
      } else {
          ret->append(pstr_fir[i]);
          ret->append("E+");
          char a[3]={0};
          ret->append(AppendSciCalTail(
                          tSciCal.fir_point_front_len + tSciCal.omit_len, a));
          return true;
      }
    }

    /* 处理位数相等 不进位的场景 */
    if (fir_point_front_len + fir_point_back_len == sec_point_back_len + 1) {
      // 0.0999,'9.99EEE'
      ret->append(pstr_fir[i]);
      ret->append('.');
      i++;

      fir_point_front_len--;
      while (fir_point_front_len != 0) {
        ret->append(pstr_fir[i]);
        fir_point_front_len--;
        i++;
      }
      i++;
      while (fir_point_back_len != 0) {
        ret->append(pstr_fir[i]);
        fir_point_back_len--;
        i++;
      }

      ret->append("E+");
      char a[3] = {0};
      ret->append(AppendSciCalTail(
                      tSciCal.fir_point_front_len + tSciCal.omit_len, a));
    } else if (fir_point_front_len + fir_point_back_len <
               sec_point_back_len + 1) {
      /* 前面位数小  TO_CHAR(388.99,'9.99999EEEE') */
      ret->append(pstr_fir[i]);
      ret->append('.');
      i++;

      fir_point_front_len--;
      while (fir_point_front_len != 0) {
        ret->append(pstr_fir[i]);
        fir_point_front_len--;
        sec_point_back_len--;
        i++;
      }
      i++;
      while (fir_point_back_len != 0) {
        ret->append(pstr_fir[i]);
        fir_point_back_len--;
        sec_point_back_len--;
        i++;
      }
      while (sec_point_back_len != 0) {
        ret->append('0');
        sec_point_back_len--;
      }
      ret->append("E+");
      char a[3] = {0};
      ret->append(AppendSciCalTail(tSciCal.fir_point_front_len, a));
    } else {
      /* 前面位数大  TO_CHAR(123.9995,'9.999EEEE') */
      int pos = fir_point_front_len - 1 - sec_point_back_len - 1;
      char d_new[MAX_TOCHAR_SRC_LEN] = {0};
      RoundingNum(pstr_fir, d_new, pos);

      int omitpos = 0;
      if (pos >= 0) {
        //12555,'9.9'
        omitpos = fir_point_front_len - pos - 1;
        d_new[omitpos] = '\0';
      } else {
        //0.000005 2+5  to_char(123.006645,'9.9999999')
        omitpos = fir_point_front_len-pos;
        d_new[omitpos] ='\0';
      }

      T_TSCICAL tAnotherSciCal;
      tAnotherSciCal.sec_point_front_len = tSciCal.sec_point_front_len;
      tAnotherSciCal.sec_point_back_len = tSciCal.sec_point_back_len;
      tAnotherSciCal.ret = tSciCal.ret;
      if (pos >= 0) {
        if (pstr_fir[0] == '9' && d_new[0] == '1') {
            tAnotherSciCal.omit_len = pos + 2;
        } else {
          tAnotherSciCal.omit_len = pos + 1;
        }
      } else {
        tAnotherSciCal.omit_len = 0;
      }

      T_TSrcAttr tSrcAttr;
      if (!MarkSrcAttribute(d_new, tSrcAttr)) {
        return false;
      }
      tAnotherSciCal.fir_str = d_new;
      tAnotherSciCal.fir_point_front_len = tSrcAttr.iSrc_point_front_len;
      tAnotherSciCal.fir_point_back_len = tSrcAttr.iSrc_point_back_len;
      tAnotherSciCal.fir_zero_back_len = tSrcAttr.iSrc_zero_back_len;
      //InitSciCalInfo(NULL,d_new,true,tAnotherSciCal);
      handleEEEE(tAnotherSciCal);
    }
  }

  return true;
}

char *AppendSciCalTail(int num_len, char *retstr) {
  if (num_len > 10) {
    sprintf(retstr, "%d", num_len - 1);
  } else {
    sprintf(retstr, "0%d", num_len - 1);
  }

  return retstr;
}

void RoundingNum(char *pInputStr, char *pOutStr, int pos)
{
  char *psrcstr = pInputStr;
  double d = atof(psrcstr);
  double d2 = pow((double)10, (double)pos);
  if (d >= 0)
    d = d + 5 * d2;
  else
    d = d - 5 * d2;
  sprintf(pOutStr, "%.9f", d);

  return;
}

void AppendStrWhenExistsInteger(char *pSrcStr, String *pDestStr,
                                T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  if (tSrcAttr.bFirstCharZero == true && tRuleAttr.iPointBackLen !=0) {
    /**
      src 0.*** /0开头  rule point后不为0
      0.**开头
    */
    if(tRuleAttr.iZerostartpos == 0 && tRuleAttr.iPointFrontLen == 1) {
        ;
    } else if (tRuleAttr.iNeedappendzerolen == 0) {
      if (tRuleAttr.iPointFrontLen > 0 && (tRuleAttr.iZerostartpos == -1
          || tRuleAttr.iZerostartpos >= tRuleAttr.iPointFrontLen))
        pDestStr->append(' '); /* 例：src 0.***, fmt '99.***' */
      /**
        不需要增加 0 时  输出格式直接 .***
      */
      return ;
    }
  }

  if (tRuleAttr.commanum != 0) {
    AppendStrWhenExistsComma(pSrcStr,pDestStr,tRuleAttr,tSrcAttr);
  } else {
    AppendStrWhenNoComma(pSrcStr,pDestStr,tRuleAttr,tSrcAttr);
  }
}

void AppendStrWhenNoComma(char *psrcstr, String *pDestStr,
                          T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  if (tRuleAttr.iNeedappendzerolen > 0) {
    AppendZero(pDestStr,tRuleAttr.iNeedappendzerolen);
  }

  for (int i = 0; i < tSrcAttr.iSrc_point_front_len; i++) {
    pDestStr->append(*psrcstr);
    psrcstr++;
  }

}

void AppendStrWhenExistsCommaForZero(char *pSrcStr, String *pDestStr,
                                     T_TRuleAttr &tRuleAttr,
                                     T_TSrcAttr &tSrcAttr) {
  int startpos = 0;
  int i = 0;
  for (; i <= tRuleAttr.commanum; i++) {
    startpos += tRuleAttr.comma[i];
    if (startpos > tRuleAttr.iZerostartpos) {
      break;
    } else {
      startpos++;
    }
  }

  int zerofrontcommapos = startpos-tRuleAttr.comma[i] - 1;
  int newrulelen = 0;
  int begincommanum = i;
  for (; i <= tRuleAttr.commanum; i++) {
    newrulelen+= tRuleAttr.comma[i];
  }

  int zerofrontnumlen = tRuleAttr.iZerostartpos - zerofrontcommapos- 1;
  newrulelen = newrulelen - zerofrontnumlen;
  int zerolen = newrulelen - tSrcAttr.iSrc_point_front_len;
  int firstnewcommalen = tRuleAttr.comma[begincommanum] - zerofrontnumlen;

  if (newrulelen == firstnewcommalen) {
    AppendZero(pDestStr, zerolen);
    int strlength = tSrcAttr.iSrc_point_front_len;
    char *pstr = pSrcStr;
    while (strlength != 0) {
      pDestStr->append(*pstr);
      pstr++;
      strlength--;
    }
    return ;
  }

  tRuleAttr.comma[begincommanum] = firstnewcommalen;
  tRuleAttr.comma[tRuleAttr.commanum] = tRuleAttr.commalastbacklen;
  for (i = begincommanum; i <= tRuleAttr.commanum; i++) {
    if (tRuleAttr.comma[i] <= zerolen) {
      AppendZero(pDestStr,tRuleAttr.comma[i]);
      if (i != tRuleAttr.commanum) {
        pDestStr->append(',');
        tSrcAttr.commanum++;
      }
      zerolen -= tRuleAttr.comma[i];
    } else if (zerolen > 0) {
      int simplestrlen = tRuleAttr.comma[i] - zerolen;
      AppendZero(pDestStr, zerolen);
      pSrcStr=AppendStr(pDestStr, simplestrlen, pSrcStr);
      if (i != tRuleAttr.commanum) {
        pDestStr->append(',');
        tSrcAttr.commanum++;
      }
      zerolen = 0;
    } else {
      int simplestrlen = tRuleAttr.comma[i];
      pSrcStr=AppendStr(pDestStr, simplestrlen, pSrcStr);
      if (i != tRuleAttr.commanum) {
        pDestStr->append(',');
        tSrcAttr.commanum++;
      }
    }
  }

  return;
}

void AppendStrWhenExistsComma(char *psrcstr, String *pDestStr,
                              T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  T_TCommaFormate  tSrcStrComma;

  int allsrclen = tSrcAttr.iSrc_point_front_len;
  int ruleCommaNum = tRuleAttr.commanum;
  int srccommanum = 0;
  int rulecommasimplelen = 0;

  int commalastbacklen = tRuleAttr.commalastbacklen;
  if (tRuleAttr.iNeedappendzerolen > 0) {
    AppendStrWhenExistsCommaForZero(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
    return;
  }

  if (allsrclen <= commalastbacklen) {
    tSrcStrComma.comma[srccommanum] = allsrclen;
    srccommanum++;
  } else {
    tSrcStrComma.comma[srccommanum] = commalastbacklen;
    srccommanum++;
    allsrclen -= commalastbacklen;

    while (allsrclen > 0 && ruleCommaNum > 0) {
      rulecommasimplelen = tRuleAttr.comma[ruleCommaNum - 1];

      if (allsrclen < rulecommasimplelen) {
        tSrcStrComma.comma[srccommanum] = allsrclen;
        srccommanum++;
        break;
      }

      tSrcStrComma.comma[srccommanum] = rulecommasimplelen;
      srccommanum++;
      ruleCommaNum--;
      allsrclen -= rulecommasimplelen;
    }
  }

  tSrcStrComma.commanum = srccommanum;
  int len ;
  for (int i = tSrcStrComma.commanum; i > 0; i--) {
    len = tSrcStrComma.comma[i - 1];
    while (len != 0) {
        pDestStr->append(*psrcstr);
        len--;
        psrcstr++;
    }

    if (i != 1) {
      pDestStr->append(',');
      tSrcAttr.commanum++;
    }
  }
  return ;
}

void AppendStrWhenExistsS(char *pSrcStr, String *pDestStr,
                          T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr,
                          bool bMarkSFront) {
  if (tSrcAttr.bExistsMinus) {
    if (bMarkSFront) {
      pDestStr->append('-');
      pSrcStr++;
      AppendStrWhenExistsInteger(pSrcStr, pDestStr, tRuleAttr, tSrcAttr);
    } else {
      pSrcStr++;
      AppendStrWhenExistsInteger(pSrcStr, pDestStr, tRuleAttr, tSrcAttr);
      pDestStr->append('-');
    }

  } else {
    if (bMarkSFront) {
      pDestStr->append('+');
      AppendStrWhenExistsInteger(pSrcStr, pDestStr, tRuleAttr, tSrcAttr);
    } else {
      AppendStrWhenExistsInteger(pSrcStr, pDestStr, tRuleAttr, tSrcAttr);
      pDestStr->append('+');
    }
  }
}


void AppendStrWhenExistsMoney(String *pDestStr, T_TRuleAttr tRuleAttr) {
  if (tRuleAttr.bExistsDollar) {
    pDestStr->append('$');
  } else if (tRuleAttr.bExistsRMB) {
    pDestStr->append('R');
  }
}

void AppendRightBracketByPRFlag(char *pSrcStr, String *pDestStr,
                                  T_TRuleAttr tRuleAttr) {
  if (tRuleAttr.bExistsPR && (*pSrcStr) == '-') {
    pDestStr->append('>');
  }
}

char* AppendMinusByFlag(char *pSrcStr, String *pDestStr,
                        T_TRuleAttr tRuleAttr) {
  if (tRuleAttr.bExistsS_Front
      || tRuleAttr.bExistsS_Back
      || tRuleAttr.bExistsMI) {
    return pSrcStr;
  }

  if (tRuleAttr.bExistsPR && (*pSrcStr) == '-') {
    pDestStr->append('<');
    pSrcStr++;
  } else {
    pSrcStr = AppendMinusStr(pSrcStr, pDestStr);
  }

  return pSrcStr;
}


void AppendStrWhenExistsMI(char *pSrcStr, String *pDestStr,
                           T_TRuleAttr tRuleAttr) {
  if (tRuleAttr.bExistsMI) {
    if ((*pSrcStr) == '-')
      pDestStr->append('-');
    else
      pDestStr->append(' ');
  }
}

char *RemoveMinusWhenExistsMI(char *pSrcStr,
                              String *pDestStr MY_ATTRIBUTE((unused)),
                              T_TRuleAttr tRuleAttr) {
  if (tRuleAttr.bExistsMI && ((*pSrcStr) == '-')) {
    pSrcStr++;
  }

  return pSrcStr;
}

char *AppendMinusStr(char *pSrcStr, String *pDestStr) {
  if (*pSrcStr == '-') {
    pSrcStr++;
    pDestStr->append('-');
  } else {
    pDestStr->append(' ');
  }

  return pSrcStr;
}

void AppendStrOnlyInteger(char *pSrcStr, String *pDestStr,
                          T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  char *psrcstr = pSrcStr;

  if (tRuleAttr.bExistsS_Front) {
    AppendStrWhenExistsS(psrcstr, pDestStr, tRuleAttr, tSrcAttr, true);
  } else if (tRuleAttr.bExistsS_Back) {
    AppendStrWhenExistsS(psrcstr, pDestStr, tRuleAttr, tSrcAttr, false);
  } else {
    AppendStrWhenExistsInteger(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
  }
}

void AppendStrIntegerWithDecimal(char *pSrcStr, String *pDestStr,
                                 T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  char *psrcstr = pSrcStr;

  if (tRuleAttr.bExistsS_Front) {
    AppendStrWhenExistsS(psrcstr, pDestStr, tRuleAttr, tSrcAttr, true);
    AppendStrWhenExistsDecimal(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
  } else if (tRuleAttr.bExistsS_Back) {
    if ((*psrcstr) == '-') {
      psrcstr++;
    }
    AppendStrWhenExistsInteger(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
    AppendStrWhenExistsDecimal(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
    if (tSrcAttr.bExistsMinus)
      pDestStr->append('-');
    else
      pDestStr->append('+');
  } else {
    AppendStrWhenExistsInteger(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
    AppendStrWhenExistsDecimal(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
  }
}

void AppendStrWhenExistsDecimal(char *pSrcStr, String *pDestStr,
                                T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  pDestStr->append('.');
  if (tSrcAttr.iSrc_point_back_len == 0) {
    if (*pSrcStr == '-') {
      pSrcStr = pSrcStr + tSrcAttr.iSrc_point_front_len + 1;
    } else {
      pSrcStr = pSrcStr + tSrcAttr.iSrc_point_front_len;
    }
  } else {
    if (*pSrcStr == '-') {
      pSrcStr = pSrcStr + tSrcAttr.iSrc_point_front_len + 1 + 1;
    } else {
      pSrcStr = pSrcStr + tSrcAttr.iSrc_point_front_len + 1;
    }
  }

  if (tSrcAttr.iSrc_point_back_len < tRuleAttr.iPointBackLen) {
    AppendDecimalLenLt(pSrcStr,pDestStr,tRuleAttr,tSrcAttr);
  } else if (tSrcAttr.iSrc_point_back_len == tRuleAttr.iPointBackLen) {
    AppendDecimalLenEq(pSrcStr, pDestStr, tRuleAttr, tSrcAttr);
  } else {
    AppendDecimalLenGt(pSrcStr, pDestStr, tRuleAttr, tSrcAttr);
  }
}

void AppendDecimalLenEq(char *pSrcStr, String *pDestStr,
                        T_TRuleAttr &tRuleAttr MY_ATTRIBUTE((unused)),
                        T_TSrcAttr &tSrcAttr) {
  char *psrcstr = pSrcStr;
  int iPointBackLen = tSrcAttr.iSrc_point_back_len;
  while (iPointBackLen != 0) {
    pDestStr->append(*psrcstr);
    iPointBackLen--;
    psrcstr++;
  }
}

void AppendDecimalLenGt(char *pSrcStr, String *pDestStr,
                        T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr MY_ATTRIBUTE((unused))) {
  char *psrcstr = pSrcStr;
  int iPointBackLen = tRuleAttr.iPointBackLen;
  while (iPointBackLen != 0) {
    pDestStr->append(*psrcstr);
    iPointBackLen--;
    psrcstr++;
  }
}

void AppendDecimalLenLt(char *pSrcStr, String *pDestStr,
                        T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  AppendDecimalLenEq(pSrcStr, pDestStr, tRuleAttr, tSrcAttr);

  int destzerolen = tRuleAttr.iPointBackLen - tSrcAttr.iSrc_point_back_len;
  while (destzerolen != 0) {
    pDestStr->append('0');
    destzerolen--;
  }
}

bool bHandleNine(char *prulestr, char *psrcstr, String *pdestsrc,
                 T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  if (NULL == prulestr || NULL == psrcstr || NULL == pdestsrc) {
    return false;
  }

  //TO_CHAR(1234,'99')  ###
  if (tSrcAttr.iSrc_point_front_len > tRuleAttr.iPointFrontLen) {
    //TO_CHAR(0,'.99')
    if (tSrcAttr.bFirstCharZero == true && tRuleAttr.iPointFrontLen == 0) {
      HandleIntegerLenEq(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);
    } else {
      HandleIntegerLenGt(prulestr, pdestsrc, tRuleAttr, tSrcAttr);
    }
  } else if (tSrcAttr.iSrc_point_front_len == tRuleAttr.iPointFrontLen) {
    //TO_CHAR(1234,'9999')
    HandleIntegerLenEq(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);
  } else {
    //TO_CHAR(1234,'99999')
    HandleIntegerLenLt(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);
  }

  return true;
}

bool bHandleZero(char *prulestr, char *psrcstr, String *pdestsrc,
                 T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  if (NULL == prulestr || NULL == psrcstr || NULL == pdestsrc) {
    return false;
  }

  if (tSrcAttr.iSrc_point_front_len > tRuleAttr.iPointFrontLen) {
    if (tSrcAttr.bFirstCharZero == true && tRuleAttr.iPointFrontLen == 0) {
      HandleIntegerLenEq(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);
    } else {
      HandleIntegerLenGt(prulestr, pdestsrc, tRuleAttr, tSrcAttr);
    }
  } else if (tSrcAttr.iSrc_point_front_len == tRuleAttr.iPointFrontLen) {
      HandleIntegerLenEq(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);
  } else {
    HandleIntegerLenLtForZero(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);
  }

  return true;
}

 /**
 Count the number of 0 to delete

 @param pdestsrc  (input and output param)
 @param consective_nines_in_tail   (input param)
   9 consecutive numbers at the end of decimal point

 TO_CHAR(123456.123456,'999999999.999999999')
 +----------------------------------------------+
 | TO_CHAR(123456.123456,'999999999.999999999') |
 +----------------------------------------------+
 |     123456.123456000                         |
 +----------------------------------------------+

 TO_CHAR(123456.123456,'FM999999999.999999999')
 +------------------------------------------------+
 | TO_CHAR(123456.123456,'FM999999999.999999999') |
 +------------------------------------------------+
 | 123456.123456                                  |
 +------------------------------------------------+
 consective_nines_in_tail = 9;

 TO_CHAR(123456.123456,'999999999.999990099')
 +----------------------------------------------+
 | TO_CHAR(123456.123456,'999999999.999990099') |
 +----------------------------------------------+
 |     123456.123456000                         |
 +----------------------------------------------+

 TO_CHAR(123456.123456,'FM999999999.999990099')
 +------------------------------------------------+
 | TO_CHAR(123456.123456,'FM999999999.999990099') |
 +------------------------------------------------+
 | 123456.1234560                                 |
 +------------------------------------------------+
 consective_nines_in_tail = 2;

 */

static size_t get_fm_zero_len(String *pdestsrc, size_t consective_nines_in_tail)
{
   if ((NULL == pdestsrc) || (0 == consective_nines_in_tail))
     return 0;

   char *original_str = pdestsrc->ptr();
   size_t length = pdestsrc->length();
   size_t zero_len = 0;

   if (NULL == original_str) return 0;

   char *pstr = original_str + length - 1; /*Position of last character*/

   while (('0' == *pstr) && (pstr != original_str))
   {
      zero_len++;
      pstr--;
   }

   return consective_nines_in_tail < zero_len ? consective_nines_in_tail : zero_len;
}



 /**
 handle FM
 remove the space and zer0 corresponding to 9

 @param pdestsrc  (input and output param) Output results
 @param tRuleAttr   (input param)

 TO_CHAR(123456.123456,'999999999.999999999')
+----------------------------------------------+
| TO_CHAR(123456.123456,'999999999.999999999') |
+----------------------------------------------+
|     123456.123456000                         |
+----------------------------------------------+

TO_CHAR(123456.123456,'FM999999999.999999999')
+------------------------------------------------+
| TO_CHAR(123456.123456,'FM999999999.999999999') |
+------------------------------------------------+
| 123456.123456                                  |
+------------------------------------------------+

 */

static bool bHandleFM(String *pdestsrc, T_TRuleAttr &tRuleAttr)
{
  if (NULL == pdestsrc) return false;

  size_t  fm_zero_len = 0;

  trim_left_space(pdestsrc);

  fm_zero_len = get_fm_zero_len(pdestsrc, tRuleAttr.iTailNineLen);
  if (!trim_right_chars(pdestsrc, fm_zero_len)) return false;
  return true;
}

void AppendZero(String *pdestsrc, int len)
{
  while (len != 0) {
    pdestsrc->append('0');
    len--;
  }
}

char *AppendStr(String *pdestsrc, int len, char *pSrcStr) {
  while (len != 0) {
    pdestsrc->append(*pSrcStr);
    len--;
    pSrcStr++;
  }

  return pSrcStr;
}

void HandleIntegerLenLtForZero(char *pSrcStr, String *pdestsrc,
                               T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  char *psrcstr = pSrcStr;
  int zerobacklen =
      tRuleAttr.iPointFrontLen + tRuleAttr.commanum - tRuleAttr.iZerostartpos;

  // to_char(234,'9900')
  if (zerobacklen <= tSrcAttr.iSrc_point_front_len) {
    HandleIntegerLenLt(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);
    return;
  }

  int spacelen = tRuleAttr.iPointFrontLen + tRuleAttr.commanum - zerobacklen;
  while (spacelen != 0) {
      pdestsrc->append(' ');
      spacelen--;
  }

  tRuleAttr.iNeedappendzerolen = zerobacklen - tSrcAttr.iSrc_point_front_len;
  HandleIntegerLenEq(psrcstr, pdestsrc, tRuleAttr, tSrcAttr);

  return;
}

void HandleIntegerLenEq(char *pSrcStr, String *pDestStr,
                        T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  char *psrcstr = pSrcStr;

  psrcstr = RemoveMinusWhenExistsMI(psrcstr, pDestStr, tRuleAttr);
  psrcstr = AppendMinusByFlag(psrcstr, pDestStr, tRuleAttr);

  AppendStrWhenExistsMoney(pDestStr, tRuleAttr);

  if (tRuleAttr.iPointBackLen == 0) {
    AppendStrOnlyInteger(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
  } else {
    AppendStrIntegerWithDecimal(psrcstr, pDestStr, tRuleAttr, tSrcAttr);
  }

  AppendStrWhenExistsMI(pSrcStr, pDestStr, tRuleAttr);
  AppendRightBracketByPRFlag(pSrcStr, pDestStr, tRuleAttr);
}

void HandleIntegerLenGt(char *prulestr, String *pDestStr,
                        T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr MY_ATTRIBUTE((unused))) {
  pDestStr->length(0);
  int rulelength = strlen(prulestr);

  if (tRuleAttr.bExistsMI)
    --rulelength;
  else if (!tRuleAttr.bExistsS_Front
           && !tRuleAttr.bExistsS_Back
           && !tRuleAttr.bExistsPR)
    ++rulelength;

  if (tRuleAttr.bExistsFM)
    rulelength -= 2;

  for (int i = 0 ; i < rulelength; i++) {
    pDestStr->append('#');
  }

  return;
}

void HandleIntegerLenLt(char *pSrcStr, String *pDestStr,
                        T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  String tmpString;
  HandleIntegerLenEq(pSrcStr, &tmpString, tRuleAttr, tSrcAttr);

  longlong spacelen = tRuleAttr.iPointFrontLen
                      + tRuleAttr.commanum
                      - tSrcAttr.iSrc_point_front_len
                      - tSrcAttr.commanum;
  while (spacelen != 0) {
    pDestStr->append(' ');
    spacelen--;
  }

  pDestStr->append(tmpString);
}

/**
  Calculate the number of consecutive zeros after the decimal point

  EEEE use

  @param[in] pRuleStr
  @param[out] tSrcAttr
*/
static void get_eeee_zero_len(char *pstr, size_t pos, bool has_point,
                              bool &eeee_zero, int &eeee_zero_length) {
  if (has_point && eeee_zero) {
    if (pstr[pos] == '0')
      eeee_zero_length++;
    else if (pstr[pos] != '.')  /*maybe the current position is point character*/
      eeee_zero = false;
  }
  return;
}

bool MarkSrcAttribute(char *psrcstr, T_TSrcAttr &tSrcAttr) {
  size_t pos = 0;
  bool zero_back = false;
  bool has_point = false;

  while (psrcstr[pos] != '\0') {
    if ((psrcstr[pos]) == '.') {
      has_point = true;
      pos++;
      continue;
    }

    if (has_point) {
      tSrcAttr.iSrc_point_back_len++;
      if ('0' != psrcstr[pos])
        zero_back = true;
      if (zero_back)
        tSrcAttr.iSrc_zero_back_len++;
    }
    else
      tSrcAttr.iSrc_point_front_len++;

    pos++;
  }

  if (psrcstr[0] == '-') {
    tSrcAttr.bExistsMinus = true;
    tSrcAttr.iSrc_point_front_len--;
    if (psrcstr[1] == '0') {
      tSrcAttr.bFirstCharZero = true;
    }
  } else if (psrcstr[0] == '0') {
    tSrcAttr.bFirstCharZero = true;
  }

  return true;
}

/**
 set rule MI
 MI Must be at the end

 @param pdestsrc  (output param)
 @param tRuleAttr   (input param)

 */

static bool MarkRuleAttributeMI(char *prulestr, T_TRuleAttr &tRuleAttr)
{
   DBUG_ASSERT(nullptr != prulestr);

   if (tRuleAttr.bExistsMI == true)
      return false;

    prulestr++;
    if (*prulestr == 'I') {
      tRuleAttr.bExistsMI = true;
    } else {
      return false;
    }

    char *pstr = prulestr;
    if ((*++pstr) != '\0')  //MI Must be at the end
      return false;

    return true;
}

/**
 set rule PR
 PR Must be at the end

 @param pdestsrc  (output param)
 @param tRuleAttr   (input param)

 */

static bool MarkRuleAttributePR(char *prulestr, T_TRuleAttr &tRuleAttr)
{
    DBUG_ASSERT(nullptr != prulestr);

    if (tRuleAttr.bExistsPR == true)
       return false;

    prulestr++;
    if ((*prulestr) == 'R') {
      tRuleAttr.bExistsPR = true;
    } else {
      return false;
    }

    char *pstr = prulestr;
    if ((*++pstr) != '\0')   //PR Must be at the end
      return false;

    return true;
}

/**
 set rule FM
 FM Must be at the start

 @param pdestsrc  (output param)
 @param tRuleAttr   (input param)

 */

static bool MarkRuleAttributeFM(char *pSrcRuleStr, char *prulestr, T_TRuleAttr &tRuleAttr)
{
    DBUG_ASSERT((nullptr != pSrcRuleStr) && (nullptr != prulestr));

    if (tRuleAttr.bExistsFM == true)
       return false;

    prulestr++;
    //FM Must be at the start (pSrcRuleStr+1 == prulestr)
    if ((*prulestr == 'M') && (pSrcRuleStr+1 == prulestr)) {
      tRuleAttr.bExistsFM = true;
    } else {
      return false;
    }

    return true;
}

bool MarkRuleAttribute(char *pRuleStr, T_TRuleAttr &tRuleAttr) {
  int iPointFrontLen = 0;
  int iPointBackLen = 0;
  char *prulestr = pRuleStr;
  //char *pfirststr = pRuleStr;
  bool bExistsS = false;
  int commanum = 0;
  int simple_len = 0;
  //int iMarkDpos = 0;
  int rulelen = 0;

  while (*prulestr != '\0') {
    switch (*prulestr) {
      case '9': {
        tRuleAttr.bExistsNine = true;
        if (!tRuleAttr.bExistsPoint) {
          iPointFrontLen++;
        } else {
          iPointBackLen++;
          /*Calculate the number of 9 after the decimal point.
          When 0 is encountered, the initial value is 0*/
          tRuleAttr.iTailNineLen++;
        }
        simple_len++;
        rulelen++;
        break;
      }
      case '0': {
        if (tRuleAttr.bExistsZero == false)
          tRuleAttr.iZerostartpos = rulelen;
        tRuleAttr.bExistsZero = true;
        if (!tRuleAttr.bExistsPoint) {
          iPointFrontLen++;
        } else {
          iPointBackLen++;
          /*Calculate the number of 9 after the decimal point.
          When 0 is encountered, the initial value is 0*/
          tRuleAttr.iTailNineLen = 0;
        }
        simple_len++;
        rulelen++;
        break;
      }
      case '$': {
        if (tRuleAttr.bExistsDollar == true) {
          return false;
        }
        tRuleAttr.bExistsDollar = true;
        break;
      }
      case 'R': {
        if (tRuleAttr.bExistsRMB == true) {
          return false;
        }
        tRuleAttr.bExistsRMB = true;
        break;
      }
      case 'S': {
        if (bExistsS == true) {
          return false;
        }
        bExistsS = true;
        break;
      }
      case 'F': {
        if (!MarkRuleAttributeFM(pRuleStr, prulestr, tRuleAttr))
          return false;

        prulestr++;
        break;
      }
      case 'M': {
        if (!MarkRuleAttributeMI(prulestr, tRuleAttr))
          return false;

        prulestr++;
        break;
      }
      case 'B': {
        if (tRuleAttr.bExistsB == true) {
          return false;
        }

        tRuleAttr.bExistsB = true;
        break;
      }
      case 'P': {
          if (!MarkRuleAttributePR(prulestr, tRuleAttr))
            return false;

          prulestr++;
          break;
      }
      case '.':
      case 'D':
        if (tRuleAttr.bExistsPoint == true) {
           return false;
        }
        tRuleAttr.bExistsPoint = true;
        break;
      case 'E': {
        if (tRuleAttr.bExistsEEEE == true) {
          return false;
        }
        if (*(++prulestr) != 'E' ||
            *(++prulestr) != 'E' ||
            *(++prulestr) != 'E') {
          return false;
        }
        tRuleAttr.bExistsEEEE = true;
        break;
      }
      case ',':
      case 'G': {
        if (commanum >= MAX_TOCHAR_COMMA_NUM) {
          return false;
        }
        if (tRuleAttr.bExistsPoint == true) {
           return false;
        }
        tRuleAttr.bExistsComma = true;
        tRuleAttr.comma[commanum] = simple_len;
        simple_len = 0;
        commanum++;
        rulelen++;
        break;
      }

      default:
        return false;  //Unsupported format error
    }

    prulestr++;
  }

  tRuleAttr.commanum = commanum;

  if (tRuleAttr.bExistsComma == true) {
    int commalastbacklen = iPointFrontLen;
    for (int i = 0; i < commanum ; i++) {
      commalastbacklen -= tRuleAttr.comma[i];
    }
    tRuleAttr.commalastbacklen = commalastbacklen;
    tRuleAttr.comma[commanum] = commalastbacklen;
  } else {
    tRuleAttr.commalastbacklen = iPointFrontLen;
  }

  tRuleAttr.iPointFrontLen = iPointFrontLen;
  tRuleAttr.iPointBackLen = iPointBackLen;

  if (bExistsS) {
    if (*pRuleStr == 'S') {
      tRuleAttr.bExistsS_Front = true;
    } else if (*(--prulestr) == 'S') {
      tRuleAttr.bExistsS_Back = true;
    } else {
      return false;
    }

    if (tRuleAttr.bExistsPR == true) {
      return false;
    }

    if (tRuleAttr.bExistsMI == true) {
      return false;
    }
  }

  if (tRuleAttr.bExistsMI == true) {
    if (tRuleAttr.bExistsPR)
      return false;
  }

  if (tRuleAttr.bExistsDollar == true) {
    if (tRuleAttr.bExistsRMB)
      return false;
    if (tRuleAttr.bExistsS_Front)
      return false;
    }

  if (tRuleAttr.bExistsEEEE == true) {
    if (tRuleAttr.bExistsDollar == true ||
        tRuleAttr.bExistsRMB == true ||
        tRuleAttr.bExistsS_Front == true ||
        tRuleAttr.bExistsS_Back == true ||
        tRuleAttr.bExistsB == true ||
        tRuleAttr.bExistsMI == true ||
        tRuleAttr.bExistsPR == true) {
      return false;
    }
  }

  return true;
}

static bool handle_number_to_char(String *pSrcStr, String *pRuleStr,
              String *pDestStr, T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr) {
  bool ret;
  char *psrcstr = pSrcStr->ptr();
  char *prulestr = pRuleStr->ptr();

  if((NULL == psrcstr) || (NULL == prulestr))
      return false;

  if (tRuleAttr.bExistsEEEE) {
    T_TSCICAL tSciCal;
    tSciCal.fir_point_back_len = tSrcAttr.iSrc_point_back_len;
    tSciCal.fir_point_front_len = tSrcAttr.iSrc_point_front_len;
    tSciCal.fir_zero_back_len = tSrcAttr.iSrc_zero_back_len;
    tSciCal.sec_point_front_len = 1;
    tSciCal.sec_point_back_len = tRuleAttr.iPointBackLen;
    tSciCal.omit_len = 0;
    tSciCal.ret = pDestStr;
    if (tSrcAttr.bExistsMinus) {
      tSciCal.ret->append('-');
      tSciCal.fir_str = psrcstr + 1;
    } else {
      tSciCal.ret->append(' ');
      tSciCal.fir_str = psrcstr;
    }

    ret = handleEEEE(tSciCal);
    if (tRuleAttr.bExistsFM)
      ret = bHandleFM(pDestStr, tRuleAttr);
    return ret;
  }

  if (tRuleAttr.bExistsZero) {
    ret = bHandleZero(prulestr, psrcstr, pDestStr, tRuleAttr, tSrcAttr);
  } else if (tRuleAttr.bExistsNine) {
    ret = bHandleNine(prulestr, psrcstr, pDestStr, tRuleAttr, tSrcAttr);
  } else {
    return false;
  }

  if (!ret)
    return false;

  if (tRuleAttr.bExistsFM)
    ret = bHandleFM(pDestStr, tRuleAttr);

  return ret;
}

/**
  processing rules string

  @param[in]    pRuleStr
  @param[out]   tRuleAttr

  @retval  false  Error.
  @retval  true   success.

*/
static bool handle_rule_attribute(String *pRuleStr, T_TRuleAttr &tRuleAttr) {
   pRuleStr->c_ptr_quick();
   char *pstr=(char *)pRuleStr->ptr();

   pstr = StrToUpper(pstr);
   if (NULL == pstr) return false;

   if (!MarkRuleAttribute(pstr, tRuleAttr)) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), "to_char");
    return false;
   }

   return true;
}

/**
  judge the character is decimal

  @param[in]    ch    input character

  @retval  false  Error.
  @retval  true   success.

*/
static bool is_decimal_char(char ch) {
  switch (ch) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '.':
      case '+':
      case '-':
        return true;
      default:
        break;
  }

  return false;
}

/**
  check decimal character and set some status

  @param[in]    pstr       character pointer
  @param[in]    pos        character position
  @param[out]   has_point  decimal point
  @param[out]   minus      negative

  @retval  false  Error.
  @retval  true   success.

*/
static bool check_decimal_char(char *pstr, size_t pos, bool &has_point,
                               bool &minus) {
    if (NULL == pstr) return false;

    if (!is_decimal_char(pstr[pos])) return false;

    if ((0 != pos) && ((pstr[pos] == '+') || (pstr[pos] == '-')))
      return false;

    if (has_point && (pstr[pos] == '.'))  return false;
    if (pstr[pos] == '.')  has_point = true;
    if (pstr[pos] == '-')  minus = true;

    return true;
}

/**
  delete invalid 0 before the vaild number

  @param[in]   pstr       character pointer
  @param[out]  length     the length of modify string
  @param[out]  tSrcAttr   source string status

  @retval  false  Error.
  @retval  true   success.

*/
static void trim_positive_zero(char *pstr, size_t &length,
                               T_TSrcAttr &tSrcAttr) {
  if ((NULL == pstr) || (length == 0)) return;

  size_t pos = 0;

  while (pos < length) {
    if ((pstr[pos] != '+') && (pstr[pos] != '-') && (pstr[pos] != '0'))
      break;
    pos++;
    tSrcAttr.iSrc_point_front_len--;
  }

  if (pos != 0) {
    if (tSrcAttr.bExistsMinus) {
      memmove(pstr + 1, pstr + pos, length - pos);
      length = length - pos + 1;
    } else {
      memmove(pstr, pstr + pos, length - pos);
      length = length - pos;
    }
  }

  if(!tSrcAttr.bExistsPoint) return;
  tSrcAttr.iSrc_point_back_len--;

  pos = length - 1;
  while (pos > 0) {
    if (pstr[pos] != '0')
      break;
    pos--;
    tSrcAttr.iSrc_point_back_len--;
  }

  length = pos + 1;
  return;
}

/**
  If the number is a valid 0, the previous processing is fixed

  @param[in]    pstr       character pointer
  @param[out]   length     the length of modify string
  @param[out]   length     source string status

*/
static void fix_valid_zero_attribute(char *pstr, size_t &length,
                       T_TSrcAttr &tSrcAttr, T_TRuleAttr &tRuleAttr) {
  if (NULL == pstr) return;

  if ((0 == tSrcAttr.iSrc_point_front_len)
      && (0 == tSrcAttr.iSrc_point_back_len)) {
    pstr[0] = '0';
    length = 1;

    if (tRuleAttr.bExistsZero)
      tSrcAttr.iSrc_point_front_len = 0;
    else
      tSrcAttr.iSrc_point_front_len = 1;

    tSrcAttr.bFirstCharZero = true;
    return;
  }

  if ('0' == pstr[0]) tSrcAttr.bFirstCharZero = true;

  return;
}

/**
  get source string attribute status

  @param[in] pRuleStr
  @param[in,out] tSrcAttr

  @retval  false  Error.
  @retval  true   success.
*/
static bool get_src_attribute(String *pSrcStr, T_TSrcAttr &tSrcAttr,
                                    T_TRuleAttr &tRuleAttr) {
  char  *pstr = (char *)pSrcStr->ptr();
  size_t length = pSrcStr->length();
  bool   has_point = false;
  bool   minus = false;
  bool   eeee_zero = true;
  int eeee_zero_length = 0;
  size_t pos = 0;

  if ((NULL == pstr) || (length == 0)) return false;

  while (pos < length) {
    if (pstr[pos] == '\0') break;
    if (!check_decimal_char(pstr, pos, has_point, minus)) return false;

    if (has_point)
      tSrcAttr.iSrc_point_back_len++;
    else
      tSrcAttr.iSrc_point_front_len++;

    get_eeee_zero_len(pstr, pos, has_point, eeee_zero, eeee_zero_length);
    pos++;
  }

  tSrcAttr.bExistsMinus = minus;
  tSrcAttr.bExistsPoint = has_point;
  trim_positive_zero(pstr, length, tSrcAttr);
  fix_valid_zero_attribute(pstr, length, tSrcAttr, tRuleAttr);

  tSrcAttr.iSrc_zero_back_len = tSrcAttr.iSrc_point_back_len - eeee_zero_length;
  pSrcStr->length(length);

  return true;
}

/**
  processing 0 string

  @param[in]   pSrcStr
  @param[out]  pRuleStr
  @param[out]  tSrcAttr

*/
static void handle_rounding_zero(String *pSrcStr, T_TSrcAttr &tSrcAttr,
                                 T_TRuleAttr &tRuleAttr) {
  char *pstr = pSrcStr->ptr();
  size_t length = pSrcStr->length();

  if ((NULL == pstr) || (0 == length)) return;

  if ((0 == tSrcAttr.iSrc_point_front_len)
      && (0 == tRuleAttr.iPointBackLen)) {
    pstr[0] = '0';
    pSrcStr->length(1);
    tSrcAttr.iSrc_point_front_len = 1;
  }

  return;
}

/**
  processing rounding

  @param[in]   pSrcStr
  @param[out]  pRuleStr
  @param[out]  tSrcAttr

  @retval  false  Error.
  @retval  true   success.

*/
static bool handle_rounding_attribute(String *pSrcStr, T_TSrcAttr &tSrcAttr,
                                      T_TRuleAttr &tRuleAttr) {
  if (tSrcAttr.iSrc_point_back_len <= tRuleAttr.iPointBackLen)
    return true;

  if (tRuleAttr.bExistsEEEE)
    return true;

  pSrcStr->c_ptr_quick();
  char *pstr = (char *)pSrcStr->ptr();
  char  round_str[MAX_TOCHAR_SRC_LEN] = {0};

  RoundingNum(pstr, round_str, -(tRuleAttr.iPointBackLen + 1));
  memmove(pstr, round_str, pSrcStr->length());

  memset(&tSrcAttr, 0x0, sizeof(tSrcAttr));
  if (!get_src_attribute(pSrcStr, tSrcAttr, tRuleAttr))
    return false;

  handle_rounding_zero(pSrcStr, tSrcAttr, tRuleAttr);

  return true;
}

/**
  processing eeee attribute

  @param[in]   pSrcStr
  @param[out]  pRuleStr
  @param[out]  tSrcAttr

  @retval  false  Error.
  @retval  true   success.

*/
static bool handle_eeee_attribute(String *pSrcStr, T_TSrcAttr &tSrcAttr,
                                  T_TRuleAttr &tRuleAttr) {
  if (!tRuleAttr.bExistsEEEE) return true;

  char *pstr = (char *)pSrcStr->ptr();
  size_t length = pSrcStr->length();

  if ((NULL == pstr) || (length == 0))
    return false;

  if ('.' == pstr[0]) {
    memmove(pstr + 1, pstr, length);
    pstr[0] = '0';
    tSrcAttr.iSrc_point_front_len = 1;
    pSrcStr->length(length + 1);
  }

  length = pSrcStr->length();
  if (('-' == pstr[0]) && ('.' == pstr[1])) {
    memmove(pstr + 2, pstr + 1, length);
    pstr[1] = '0';
    tSrcAttr.iSrc_point_front_len = 1;
    pSrcStr->length(length + 1);
  }

  if (0 == tSrcAttr.iSrc_point_front_len)
    tSrcAttr.iSrc_point_front_len = 1;

  return true;
}

/**
  processing source string

  @param[in]   pSrcStr
  @param[out]  pRuleStr
  @param[out]  tSrcAttr

  @retval  false  Error.
  @retval  true   success.

*/
static bool handle_src_attribute(String *pSrcStr, T_TSrcAttr &tSrcAttr,
                                 T_TRuleAttr &tRuleAttr) {

  trim_space(pSrcStr);

  if (!get_src_attribute(pSrcStr, tSrcAttr, tRuleAttr)) return false;
  if (!handle_rounding_attribute(pSrcStr, tSrcAttr, tRuleAttr)) return false;
  if (!handle_eeee_attribute(pSrcStr, tSrcAttr, tRuleAttr)) return false;

  return true;
}

bool format_str_by_oracle_rule(String *pSrcStr, String *pRuleStr,
                               String *pDestStr) {
  T_TRuleAttr tRuleAttr;
  T_TSrcAttr  tSrcAttr;

  if (NULL == pSrcStr || NULL == pRuleStr)
    return false;

  if (!handle_rule_attribute(pRuleStr, tRuleAttr)) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), "to_char");
    return false;
  }

  if (!handle_src_attribute(pSrcStr, tSrcAttr, tRuleAttr)) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), "to_char");
    return false;
  }

  return handle_number_to_char(pSrcStr, pRuleStr, pDestStr, tRuleAttr, tSrcAttr);
}

/**
Determine the mode of to char by string

@param[in]   pstr    string
@param[in]   length
@param[out]  e_tochar_mode

*/
static void get_format_mode(char *pstr, size_t length,
                            enum_tochar_mode &e_tochar_mode) {
  size_t len = 0;

  if (nullptr == pstr || 0 == length)
    return;

  while ((*pstr != '\0') && (len < length)) {
    if (*pstr == '0' || *pstr == '9') {
      e_tochar_mode = ENUM_TOCHAR_NUM_MODE;
      pstr++;
      len++;
      continue;
    } else if (*pstr == 'X') {
      e_tochar_mode = ENUM_TOCHAR_HEX_MODE;
      return;
    } else if((*pstr == 'F' || *pstr == 'f')
              && (*(pstr-1) == 'F' || *(pstr-1) == 'f')) {
      e_tochar_mode = ENUM_TOCHAR_DATE_MODE;
      return;
    }
    pstr++;
    len++;
  }

  return;
}

/**
Is it the correct format item format

@param[in]  format_item

@retval  false  Error.
@retval  true   success.

*/

static bool check_format_item_type(Item *format_item) {
  if (NULL == format_item)
    return false;

  switch (format_item->type()) {
    case Item::STRING_ITEM:
    case Item::INT_ITEM:
    case Item::REAL_ITEM:
    case Item::DECIMAL_ITEM:
      return true;

    default:
      break;
  }
  return false;
}


bool ChooseModeByStr(Item *arg2, enum_tochar_mode &e_tochar_mode) {
  if (NULL == arg2)
    return false;

  Item *format_item = (down_cast<PTI_udf_expr *>(arg2))->get_item();
  char buff0[MAX_FIELD_WIDTH] = {0};
  String tmp0(buff0, sizeof(buff0), &my_charset_bin);
  String *str_arg0 = NULL;
  char *pstr = NULL;
  size_t  length = 0;

  if(!check_format_item_type(format_item))
    return false;

  if (format_item->type() == Item::STRING_ITEM)
  {
    LEX_STRING literal = ((PTI_text_literal *)format_item)->get_literal();
    pstr = literal.str;
    length = literal.length;
  }
  else
  {
     str_arg0 = format_item->val_str(&tmp0);
     if(str_arg0 != NULL)
     {
        pstr = str_arg0->ptr();
        length = str_arg0->length();
     }
  }

  if (NULL == pstr)
    return false;

  get_format_mode(pstr, length, e_tochar_mode);

  return true;
}
