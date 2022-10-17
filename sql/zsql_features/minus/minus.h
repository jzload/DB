/**
  @file sql/zsql_features/minus/minus.h

  @brief
  Some functions used in MINUS (macro: HAVE_ZSQL_MINUS).
*/

#ifndef SQL_MINUS_H
#define SQL_MINUS_H

#include "my_base.h"                  // ha_rows
#include "sql/composite_iterators.h"  // MaterializeIterator

class SELECT_LEX;
class SELECT_LEX_UNIT;
class JOIN;
class TABLE;

enum class Query_Blocks_Linkage_Type {
  NOT_LINKAGE,    /* not query block linkage, not exist UNION or MINUS */
  ALL_ARE_UNION,
  ALL_ARE_MINUS,
  ALL_ARE_MISC,   /* query blocks exist UNION and MINUS */
};

Query_Blocks_Linkage_Type get_query_blocks_linkage_type(
                            SELECT_LEX *first_select_lex);

Query_Blocks_Linkage_Type get_linkage_type_by_array(
    const Mem_root_array<MaterializeIterator::QueryBlock> &qb_array);

bool get_last_minus_query_block(
    const Mem_root_array<MaterializeIterator::QueryBlock> &qb_array,
    size_t &last_minus_qb_index);

bool allow_replace_wild_in_exists_subselect(SELECT_LEX_UNIT *unit);

bool is_minus_query_block(const MaterializeIterator::QueryBlock &query_block);
bool is_union_query_block(const MaterializeIterator::QueryBlock &query_block);

int delete_row_for_minus(TABLE *table, ha_rows *stored_rows);

#endif // SQL_MINUS_H

