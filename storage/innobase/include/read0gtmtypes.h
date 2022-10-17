#ifndef gtmread0types_h
#define gtmread0types_h

#include <algorithm>

#include "base64.h"
#include "m_string.h"
#include "trx0types.h"

static const int def_cap_size = 200;

/* gtmgtid Read view lists the gtmgtids of those transactions for which a
consistent read should not see the modifications to the goldendb*/
class gtmgtid_ReadView {
 public:
  // in mvcc pool cannot alloc when construct
  gtmgtid_ReadView()
      : min_gtmgtid(0),
        ids_arr(nullptr),
        ids_size(0),
        ids_capablity(0),
        self_gtmgtid(0),
        next_gtmgtid(0) {}

  ~gtmgtid_ReadView() {
    UT_DELETE_ARRAY(ids_arr);
    ids_arr = nullptr;
  }

  ulonglong get_next_gtmgtid() const { return next_gtmgtid; }
  void set_next_gtmgtid(ulonglong next_value) { next_gtmgtid = next_value; }
  /**
  copy the active gtmgtid list to arrary

    @param arr 		 array copy from
    @param size     size of copy in*/
  void copy_ids(ulonglong *arr, int64 size) {
    if (size == 0 || !arr) {
      ids_size = 0;
      min_gtmgtid = 0;
      return;
    }

    if (size > ids_capablity) {
      if (ids_arr) {
        UT_DELETE_ARRAY(ids_arr);
        ids_arr = nullptr;
      }

      // at least alloc def_cap_size
      ids_capablity = size > def_cap_size ? size : def_cap_size;
      ids_arr = UT_NEW_ARRAY_NOKEY(ulonglong, ids_capablity);
    }

    if (ids_arr != NULL) {
      ::memmove(ids_arr, arr, size * sizeof(ulonglong));
    }

    ids_size = size;
    min_gtmgtid = ids_arr[0];
  }

  /**
   The clon the oldest view need this function,make copy
    @param other copy content from */
  void copy(gtmgtid_ReadView &other) {
    ut_ad(&other != this);
    min_gtmgtid = other.min_gtmgtid;
    ids_size = other.ids_size;

    if (other.ids_size > 0) {
      copy_ids(other.ids_arr, ids_size);
    }

    self_gtmgtid = other.self_gtmgtid;
    next_gtmgtid = other.next_gtmgtid;

    if (self_gtmgtid > 0) {
      // the oldest readview self gtmgtid can not see too
      reserve(ids_size + 1);
      ids_arr[ids_size - 1] = self_gtmgtid;
      std::sort(ids_arr, ids_arr + ids_size);
      self_gtmgtid = 0;
    }
  }

  /**
    Try and increase the size of the array. Old elements are
    copied across. It is a no-op if n is < current size.

    @param n 		Make space for n elements */
  void reserve(int64 size) {
    if (size <= ids_capablity) {
      ids_size = size;
      return;
    }

    auto *p = ids_arr;
    ids_arr = UT_NEW_ARRAY_NOKEY(ulonglong, size);
    ids_capablity = size;

    if (p) {
      ::memmove(ids_arr, p, ids_size * sizeof(ulonglong));
      ids_size = size;
      UT_DELETE_ARRAY(p);
    }
  }

  /**
   check the record gtmgtid is visiable

   @param gtmgtid 		the record gtmgtid
   @return   true, can see, false cannot see*/
  bool changes_visible(ulonglong gtmgtid) {
    if (gtmgtid < min_gtmgtid || gtmgtid == self_gtmgtid) {
      return (true);
    }

    if (gtmgtid >= next_gtmgtid) {
      return (false);
    } else if (ids_size == 0) {
      return (true);
    }

    return (!std::binary_search(ids_arr, ids_arr + ids_size, gtmgtid));
  }

  /**
   fetch the gtmgtid_active list

   @param thd_gtmgtid_list 		the thd for gtmgtid list save in
   @param len 		the length of gtmgtid list
   @param next_gtmgtid_para 		next gtmgtid
   @param self_gtmtid_para 		self gtmgtid
   */
  void fetch_gtmgtid_active_list(void *thd_gtmgtid_list, int64 len,
                                 ulonglong next_gtmgtid_para,
                                 ulonglong self_gtmtid_para) {
    ut_ad((!thd_gtmgtid_list && !len) || (thd_gtmgtid_list && len > 0));
    // if ptr is nullptr, len must be 0
    copy_ids((ulonglong *)thd_gtmgtid_list, (len / sizeof(ulonglong)));

    this->next_gtmgtid = next_gtmgtid_para;
    this->self_gtmgtid = self_gtmtid_para;
  }

  friend class MVCC;

 private:
  ulonglong min_gtmgtid;
  ulonglong *ids_arr;
  int64 ids_size{0};
  int64 ids_capablity{0};
  ulonglong self_gtmgtid;
  ulonglong next_gtmgtid;
  typedef UT_LIST_NODE_T(gtmgtid_ReadView) node_t;
  /** List of gtmgtid read views in trx_sys */
  byte pad1[64 - sizeof(node_t)];
  node_t m_view_list;
};

#endif