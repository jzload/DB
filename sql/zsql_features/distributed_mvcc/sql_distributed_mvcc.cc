#include "sql/sql_distributed_mvcc.h"
#include "sql/handler.h"
#include "m_string.h"
#include "base64.h"

/**
  check the next gtmgtid is correct
   @param thd Thread handler
   @return true if error ,false is success */
bool make_active_gtmgtids(THD *thd) {
  uchar *dst = nullptr;
  auto *val = thd->variables.gtmgtid_active_list;
  auto len = my_strlen(val);

  // len !=0 && thd->variables.next_gtmgtid ==0 is check in check_gtmgtids
  if (len == 0 && thd->variables.next_gtmgtid != 0) {
    my_error(ER_DISTRIBUTED_MVCC_ERROR, MYF(0), 
            "base64 code of gtmgtid active list cannot empty");
    return true;
  }

  //小于4个，必须都是空格
  if (len < 4) {
    for (size_t i = 0; i < len; i++) {
      if(!isspace(val[i])) {
        my_error(ER_BASE64_DECODE_ERROR, MYF(0));
        return true;
      }
    }
  }

  auto need_length = base64_needed_decoded_length(len);
  if (need_length == 0) {
    thd->active_gtmgtids = std::make_pair(0, nullptr);
    return false;
  }

  dst = (uchar *)thd->alloc(need_length + 1);
  if (!dst) {
    my_error(ER_OUTOFMEMORY, MYF(ME_FATALERROR), need_length + 1);
    return true;
  }

  DBUG_ASSERT(memset(dst, 0xcc, (size_t)(need_length + 1)));
  auto ret = base64_decode(val, len, dst, NULL, 0);
  DBUG_ASSERT(ret <= (int64)need_length);
  DBUG_ASSERT(dst[need_length] == 0xcc);

  // must be sizeof(ulonglong) length for an element
  if (ret == -1 || ret < (int64)sizeof(ulonglong) || ret % sizeof(ulonglong) != 0) {
    my_error(ER_BASE64_DECODE_ERROR, MYF(0));
    return true;
  }

  std::sort(pointer_cast<ulonglong *>(dst), pointer_cast<ulonglong *>(dst + ret));
  thd->active_gtmgtids = std::make_pair(ret, dst);

  return false;
}

/**
  check the next gtmgtid is correct
   @param thd Thread handler
   @return true if error ,false is success */
bool check_gtmgtids(const THD *thd) {

  //非RC,variables.next_gtmgtid != 0
  if (thd->variables.transaction_isolation != ISO_READ_COMMITTED && 
    thd->variables.next_gtmgtid != 0) {

    my_error(ER_DISTRIBUTED_MVCC_ERROR, MYF(0),
            "gtmgtid active list can only be set in RC isolation");
    return true;
  }

  // when active ids is empty , correct
  if (thd->active_gtmgtids.first == 0) return false;

  auto len = thd->active_gtmgtids.first / sizeof(ulonglong);

  ulonglong *arr = (ulonglong*)thd->active_gtmgtids.second;

  if (arr[len - 1] >= thd->variables.next_gtmgtid) {

    my_error(ER_DISTRIBUTED_MVCC_ERROR, MYF(0), 
            "the next gtmgtid must be greater than max gtmgtid in active list");
    return true;
  }

  return false;
}
