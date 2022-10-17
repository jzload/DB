#ifndef ORACLE_FUNC_H
#define ORACLE_FUNC_H
#include "sql/field.h"

char *StrToUpper(char *ptr, size_t length);
bool subYearString(uint32 *pCurrLen, uint32 *pTotalLen, uint32 *piformat, char *ptr,
                   char *ptoStr, uint *pYearFlag);
bool subHourString(uint32 *pCurrLen, uint32 *pTotalLen, uint32 *piformat,
                   char *ptr, char *ptoStr);
bool OracleStrToMysqlStr(LEX_STRING &pfromStr, String *ptoStr, uint *pYearFlag,
                         uint32 *pwForMatType, uint32 *ms_len);
Item* Create_func_to_timeformat_item(THD *thd, Item *param_1,
                                     Item *param_2, bool bToDateMode);

enum enum_oracle_rule
{
  ENUM_TOCHAR_RULE,
  ENUM_TONUMBER_RULE
};

#define MAX_TOCHAR_SRC_LEN 131
#define MAX_TOCHAR_FORMAT_LEN 128
#define MAX_TOCHAR_CONVERT_FMT_LEN 256
#define MAX_TOCHAR_COMMA_NUM 64

enum enum_tochar_mode
{
  ENUM_TOCHAR_DATE_MODE,
  ENUM_TOCHAR_NUM_MODE,
  ENUM_TOCHAR_HEX_MODE
};

typedef struct TTSCICAL
{
  char *fir_str;
  int fir_point_front_len;
  int fir_point_back_len;
  int fir_zero_back_len;
  int sec_point_front_len;
  int sec_point_back_len;
  int omit_len;
  String *ret;
}T_TSCICAL;


typedef struct TTNINEZERO
{
  char *fir_str;
  int fir_point_front_len;
  int fir_point_back_len;
  int fir_zero_back_len;
  int sec_point_front_len;
  int sec_point_back_len;
  int omit_len;
  String *ret;
}T_TNINEZERO;


typedef struct TTRuleAttr
{
  bool bExistsEEEE;
  bool bExistsDollar;
  bool bExistsRMB;
  bool bExistsS_Front;
  bool bExistsS_Back;
  bool bExistsB;
  bool bExistsMI;
  bool bExistsD;
  bool bExistsPR;
  bool bExistsFM;
  bool bExistsZero;
  int  iTailNineLen; /*Calculate the number of the last 9 after the decimal point*/
  int  iZerostartpos;
  int  iNeedappendzerolen;
  bool bExistsNine;
  bool bExistsPoint;
  int iPointFrontLen;
  int iPointBackLen;
  bool bExistsComma;
  int comma[MAX_TOCHAR_COMMA_NUM];
  int commanum;
  int commalastbacklen;
  TTRuleAttr()
  {
    bExistsEEEE   = false;
    bExistsDollar = false;
    bExistsRMB    = false;
    bExistsS_Front= false;
    bExistsS_Back = false;
    bExistsB      = false;
    bExistsMI     = false;
    bExistsD      = false;
    bExistsPR     = false;
    bExistsFM     = false;
    bExistsZero   = false;
    iTailNineLen  = 0;
    iZerostartpos = -1;
    iNeedappendzerolen = 0;
    bExistsNine   = false;
    bExistsPoint  = false;
    iPointFrontLen= 0;
    iPointBackLen = 0;
    bExistsComma  = false;
    commanum      = 0;
    commalastbacklen  =0;
  }
} T_TRuleAttr;


typedef struct TTSrcAttr
{
  int  iSrc_point_front_len; /* 小数点前的字符个数 */
  int  iSrc_point_back_len; /* 小数点后的字符个数 */
  int  iSrc_zero_back_len ; /* 小数点后，除紧邻小数点的连续0之外的字符个数 */
                            /* 例  1234.123007800,   1234.0000123000789 */
                            /*          |- ZBL -|             |- ZBL -| */
  bool bExistsMinus;  // 负数
  bool bFirstCharZero; // 以 0.***   开头 /0
  bool bExistsDollar;
  bool bExistsRMB;
  bool bExistsS_Front;
  bool bExistsS_Back;
  bool bExistsPoint;
  bool bExistsComma;
  int comma[64];
  int commanum;

  TTSrcAttr()
  {
    iSrc_point_front_len = 0;
    iSrc_point_back_len = 0;
    iSrc_zero_back_len = 0;
    bExistsMinus = false;
    bFirstCharZero = false;
    bExistsDollar = false;
    bExistsRMB    = false;
    bExistsS_Front = false;
    bExistsS_Back = false;
    bExistsPoint  = false;
    bExistsComma  = false;
    commanum      = 0;
  }
}T_TSrcAttr;


typedef struct TTCommaFormate
{
  int comma[64];
  int commanum;
  TTCommaFormate()
  {
    commanum = 0;
  }
} T_TCommaFormate;

typedef struct TTStrOracleToMysql
{
    uint32 *iCurrLen;
    uint32 *iFormatLen;
    uint32  iTotalLen;
    uint32 *msLen;
    char   *ptr;
    char   *ptoStr;

} T_TStrOracleToMysql;


void RoundingNum(char *pInputStr, char *pOutStr, int pos);
void AppendStrWhenExistsInteger(char *pSrcStr, String *pDestStr,
                                T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);
void AppendStrWhenNoComma(char *psrcstr, String *pDestStr,
                          T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);


void AppendStrWhenNoComma(char *psrcstr, String *pDestStr,
                          T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);


void AppendStrWhenExistsComma(char *psrcstr, String *pDestStr,
                              T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);

void AppendStrWhenExistsCommaForZero(char *pSrcStr, String *pDestStr,
                                  T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);

void AppendStrWhenExistsS(char *pSrcStr, String *pDestStr, T_TRuleAttr &tRuleAttr,
                          T_TSrcAttr &tSrcAttr, bool bMarkSFront);


void AppendStrWhenExistsMoney(String *pDestStr, T_TRuleAttr tRuleAttr);

void AppendRightBracketByPRFlag(char *pSrcStr, String *pDestStr,
                                T_TRuleAttr tRuleAttr);
char* AppendMinusByFlag(char *pSrcStr, String *pDestStr, T_TRuleAttr tRuleAttr);

void AppendStrWhenExistsMI(char *pSrcStr, String *pDestStr,
                           T_TRuleAttr tRuleAttr);
char* RemoveMinusWhenExistsMI(char *pSrcStr, String *pDestStr,
                              T_TRuleAttr tRuleAttr);
char* AppendMinusStr(char *pSrcStr, String *pDestStr);

void AppendStrOnlyInteger(char *pSrcStr, String *pDestStr,
                          T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);

void AppendStrIntegerWithDecimal(char *pSrcStr, String *pDestStr,
                                 T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);
void AppendStrWhenExistsDecimal(char *pSrcStr, String *pDestStr,
                                T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);
void AppendDecimalLenEq(char *pSrcStr, String *pDestStr, T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr);
void AppendDecimalLenGt(char *pSrcStr, String *pDestStr, T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr);

void AppendDecimalLenLt(char *pSrcStr, String *pDestStr, T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr);

bool bHandleNine(char *prulestr,char *psrcstr, String *pdestsrc,
                 T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);
bool bHandleZero(char *prulestr,char *psrcstr, String *pdestsrc,
                 T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);

void AppendZero(String *pdestsrc, int len);
char* AppendStr(String *pdestsrc, int len, char *pSrcStr);

void HandleIntegerLenLtForZero(char *pSrcStr, String *pdestsrc,
                               T_TRuleAttr &tRuleAttr, T_TSrcAttr &tSrcAttr);

void HandleIntegerLenEq(char *pSrcStr, String *pDestStr, T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr);


void HandleIntegerLenGt(char *prulestr, String *pDestStr, T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr);

void HandleIntegerLenLt(char *pSrcStr, String *pDestStr, T_TRuleAttr &tRuleAttr,
                        T_TSrcAttr &tSrcAttr);

bool MarkSrcAttribute(char *psrcstr, T_TSrcAttr &tSrcAttr);
bool MarkRuleAttribute(char *pRuleStr, T_TRuleAttr &tRuleAttr);

void AppendZero(String *pdestsrc, int len);

char *gotochar(char *pfromstr, char *ptostr);
char *gotocharbylen(char *pfromstr, int len);

bool handlezero(char *prulestr, char *psrcstr, String *pdestsrc);
bool FormatStrbyRule(char *psrcstr, char *prulestr, String *pDestStr,
                     bool bForZero, enum_oracle_rule e_oracle_rule);
bool format_str_by_oracle_rule(String *pSrcStr, String *pRuleStr,
                               String *pDestStr);

//char* StrToUpper(char *ptr);
bool OracleStrToMysqlStr(String *pfromStr, String *ptoStr);
const char* getOtherCharPos(const char *s, char *chr);
bool ChooseModeByStr(Item *arg2, enum_tochar_mode &e_tochar_mode);
char *AppendSciCalTail(int num_len, char *retstr);
bool  handleEEEE(T_TSCICAL &tSciCal);

#endif
