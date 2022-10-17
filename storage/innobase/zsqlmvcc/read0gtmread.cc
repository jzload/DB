/** @file read/gtmread0read.cc
 gtmgtid view read

 Created 6/30/2020 ke yuchang
 *******************************************************/

#include "read0types.h"
#include "trx0sys.h"
#include "read0gtmread.h"

#include "sql_thd_internal_api.h"

/**
enter the trx_sys_t::mutex.
@param need_mutex		true enter false do nothing */

static inline void gtm_trx_sys_mutex_lock (bool need_mutex) {
  if (need_mutex)  trx_sys_mutex_enter();
}

/**
exit the trx_sys_t::mutex.
@param need_mutex		true enter false do nothing */

static inline void gtm_trx_sys_mutex_unlock (bool need_mutex) {
  if (need_mutex)  trx_sys_mutex_exit();
}

/**
add the gtmview to readView
@param thd    thd owned by the transaction
@param view		view owned by this class created for the
                        caller. Must be freed by calling view_close()
@param need_mutex		transaction instance of caller */

void MVCC::add_gtmview_to_view(THD *thd, ReadView *view, bool need_mutex=false) {
  if (thd) {
    auto next_gtmgtid = thd_get_next_gtmgtid(thd);
    if (next_gtmgtid != 0) {
        int64 len;
        gtm_trx_sys_mutex_lock(need_mutex);
        auto *gtm_view = get_gtmgtid_view();
        gtm_trx_sys_mutex_unlock(need_mutex);

        if (!gtm_view) return;

        auto *gtm_active_ptr = thd_get_gtmgtids(thd, &len);
        //gtm_active_ptr can be nullptr
        gtm_view->fetch_gtmgtid_active_list(gtm_active_ptr,
                          len,
                          next_gtmgtid,
                          thd_get_gtmgtid(thd));

        gtm_trx_sys_mutex_lock(need_mutex);
        UT_LIST_ADD_FIRST(m_gtmgtid_views, gtm_view);
        gtm_trx_sys_mutex_unlock(need_mutex);

        view->gtmgtid_readview_ptr = gtm_view;
    }
  }
}

/**
remove the gtmview to readView
@param view		view owned by this class created for the
                        caller. Must be freed by calling view_close()
@param need_mutex		transaction instance of caller */

void MVCC::remove_gtmview_from_view(ReadView *view, bool need_mutex) {

  ut_ad(view);

  if (!view->gtmgtid_readview_ptr) return;

  gtm_trx_sys_mutex_lock(need_mutex);;

  UT_LIST_REMOVE(m_gtmgtid_views, view->gtmgtid_readview_ptr);
  UT_LIST_ADD_LAST(m_gtmgtid_free, view->gtmgtid_readview_ptr);

  gtm_trx_sys_mutex_unlock(need_mutex);

  view->gtmgtid_readview_ptr = nullptr;

}

/**
Find a free view from the active list, if none found then allocate
a new view.
@return a view to use */

gtmgtid_ReadView *MVCC::get_gtmgtid_view() {
  ut_ad(mutex_own(&trx_sys->mutex));
  
  gtmgtid_ReadView *view;
  if (UT_LIST_GET_LEN(m_gtmgtid_free) > 0) {
    //尽量均匀
    view = UT_LIST_GET_FIRST(m_gtmgtid_free);
    UT_LIST_REMOVE(m_gtmgtid_free, view);
  } else {
    view = UT_NEW_NOKEY(gtmgtid_ReadView());

    if (view == NULL) {
      ib::error(ER_IB_MSG_918) << "Failed to allocate MVCC gtm view";
    }
  }
  return (view);
}