#include "row0gtmvers.h"

/** check record visable trxid or gtmgtid
  when it is unvisable in trxid  It must be unvisable 
  when it is visable in trxid then check  visable in gtmtid
 @return true if the record can see otherwise cannot see */
bool trxid_gtmgtid_visable(
    trx_id_t trx_id, /*!< in: trxid in record to check */
    ReadView *view, /*!< in: transaction ReadView */
    gtmgtid_ReadView* p_gtm_view, /*!< in: transaction gtmgtid_ReadView */
    const rec_t *prev_version, /*!< in: the version record to check */
    dict_index_t* index, /*!< in: the cluster index where record in */
    const ulint *offsets) /*!< in: column offset array */
{
  bool is_local_visable = true;

  is_local_visable = view->changes_visible(trx_id, index->table->name);

  return is_local_visable;
}


/** check record visable trxid or gtmgtid
  when it is unvisable in trxid  It must be unvisable 
  when it is visable in trxid then check  visable in gtmtid
 @return true if the record can see otherwise cannot see */
bool trxid_gtmgtid_visable_gtid(
    trx_id_t trx_id, /*!< in: trxid in record to check */
    const table_name_t &name,  /*!< in: table name */
    ReadView *view, /*!< in: transaction ReadView */
    gtmgtid_ReadView* p_gtm_view, /*!< in: transaction gtmgtid_ReadView */
    const ulonglong gtmgtid/*!< in: gtmgtid */)
{
  bool is_gtm_visable = true;
  bool is_local_visable = true;

  is_local_visable = view->changes_visible(trx_id, name);

  if (is_local_visable && p_gtm_view) {
      is_gtm_visable = p_gtm_view->changes_visible(gtmgtid);
  }

  return is_local_visable &&  is_gtm_visable;
}
