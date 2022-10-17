
/** @file include/row0gtmvers.h
 Row versions for gtmgtid

 Created 6/30/2020 ke yuchang
 *******************************************************/

#ifndef row0gtmvers_h
#define row0gtmvers_h

#include "read0types.h"
#include "trx0types.h"

/** check record visable trxid or gtmgtid
  when it is unvisable in trxid  It must be unvisable
  when it is visable in trxid then check  visable in gtmtid
 @return true if the record can see otherwise cannot see */
bool trxid_gtmgtid_visable(
    trx_id_t trx_id,              /*!< in: trxid in record to check */
    ReadView *view,               /*!< in: transaction ReadView */
    gtmgtid_ReadView *p_gtm_view, /*!< in: transaction gtmgtid_ReadView */
    const rec_t *prev_version,    /*!< in: the version record to check */
    dict_index_t *index,          /*!< in: the cluster index where record in */
    const ulint *offsets);        /*!< in: column offset array */

/** check record visable trxid or gtmgtid
  when it is unvisable in trxid  It must be unvisable
  when it is visable in trxid then check  visable in gtmtid
 @return true if the record can see otherwise cannot see */
bool trxid_gtmgtid_visable_gtid(
    trx_id_t trx_id,              /*!< in: trxid in record to check */
    const table_name_t &name,     /*!< in: table name */
    ReadView *view,               /*!< in: transaction ReadView */
    gtmgtid_ReadView *p_gtm_view, /*!< in: transaction gtmgtid_ReadView */
    const ulonglong gtmgtid);     /*!< in: gtmgtid */


#include "row0vers.ic"
#include "row0gtmrow.ic"
#endif
