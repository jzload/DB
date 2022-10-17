/**
  @file sql/zsql_features/minus/minus.cc

  @brief
  Some functions used in MINUS (macro: HAVE_ZSQL_MINUS).
*/

#include "sql/sql_lex.h"
#include "sql/sql_optimizer.h" // JOIN
#include "sql/composite_iterators.h"
#include "sql/mem_root_array.h"  // Mem_root_array

#include "sql/zsql_features/minus/minus.h"

/*
 * Judge the query blocks type in SELECT_LEX list, the type can be one of
 * the below:
 * (1) the query block is not a linkage;
 * (2) all query blocks are UNIOIN linkage;
 * (3) all query blocks are MINUS linkage;
 * (4) query blocks are UNIOIN, MINUS linkage.
 *
 * first_select_lex(in): the first element of select lex list
 *
 * return:
 *   Query_Blocks_Linkage_Type
 */
Query_Blocks_Linkage_Type get_query_blocks_linkage_type(
                                     SELECT_LEX *first_select_lex)
{
  int count = 0; /* element of query blocks except the first one */
  int union_count = 0;
  int minus_count = 0;

  DBUG_ASSERT(first_select_lex != nullptr);

  for (SELECT_LEX *sl = first_select_lex->next_select();
       sl != nullptr; sl = sl->next_select()) {
    ++count;

    switch (sl->linkage) {
     case UNION_TYPE:
        ++union_count;
        break;

     case EXCEPT_TYPE:
        ++minus_count;
        break;

     default:
        break;
    }
  }

  if (count == 0) {
    return Query_Blocks_Linkage_Type::NOT_LINKAGE;
  }

  if (count == union_count) {
    return Query_Blocks_Linkage_Type::ALL_ARE_UNION;
  }

  if (count == minus_count) {
    return Query_Blocks_Linkage_Type::ALL_ARE_MINUS;
  }

  return Query_Blocks_Linkage_Type::ALL_ARE_MISC;
}

/* Get linkage type by qb_array.
 */
Query_Blocks_Linkage_Type get_linkage_type_by_array(
    const Mem_root_array<MaterializeIterator::QueryBlock> &qb_array)
{
  int count = 0; /* element of qb array except the first one */
  int idx = 0;
  int union_count = 0;
  int minus_count = 0;

  for (const MaterializeIterator::QueryBlock &query_block : qb_array) {
    if (idx++ == 0) {
      continue;
    }

    if (is_minus_query_block(query_block)) {
      ++minus_count;
      continue;
    }

    if (is_union_query_block(query_block)) {
      ++union_count;
      continue;
    }
  }

  count = idx - 1;

  if (count == 0) {
    return Query_Blocks_Linkage_Type::NOT_LINKAGE;
  }

  if (count == union_count) {
    return Query_Blocks_Linkage_Type::ALL_ARE_UNION;
  }

  if (count == minus_count) {
    return Query_Blocks_Linkage_Type::ALL_ARE_MINUS;
  }

  return Query_Blocks_Linkage_Type::ALL_ARE_MISC;
}

/* Get the index of the last minus query blocks in qb_array.
 *
 * qb_array(in):
 * last_minus_qb_index(out): last minus qb index when return true
 *
 * return:
 *   true:   minus query block exists
 *   false:  minus query block not exists
 */
bool get_last_minus_query_block(
    const Mem_root_array<MaterializeIterator::QueryBlock> &qb_array,
    size_t &last_minus_qb_index)
{
  DBUG_ASSERT(qb_array.size() > 0);

  size_t curr_qb_index = qb_array.size() - 1;

  /* no need to consider the first qb(that is index 0).
   * It's unspecified type. */
  for (; curr_qb_index > 0; --curr_qb_index) {
    if (is_minus_query_block(qb_array[curr_qb_index])) {
      last_minus_qb_index = curr_qb_index;
      return true;
    }
  }
  
  return false;
}

/* Materialize query blocks in array(m_query_blocks_to_materialize),
 * and optimize process UNION query blocks at tail of array.
 *
 * This optimization is adopted when the MINUS feature is developed.
 *
 * For example:
 *   A MINUS B UNION C UNION D
 * When m_limit_rows has reached after processing B, then the UNION query
 * blocks at tail of array(that is, C and D) can be ignored.
 *
 * last_minus_qb_index(in):
 *
 * return:
 *   false  -- no error
 *   true   -- error
 */
bool MaterializeIterator::MaterializeQueryBlocksWithMinus(
                          size_t last_minus_qb_index)
{
  ha_rows stored_rows = 0;

  DBUG_ASSERT(last_minus_qb_index > 0);
  DBUG_ASSERT(last_minus_qb_index < m_query_blocks_to_materialize.size());

  /* backup limit_rows */
  ha_rows save_limit_rows = m_limit_rows;
  m_limit_rows = HA_POS_ERROR;

  size_t curr_qb_index = 0; /* current query block index in the array. */
  for (const MaterializeIterator::QueryBlock &query_block :
       m_query_blocks_to_materialize) {
    /* optimization for below cases:
     * 1. minus in IN_SUBSELECT;
     * 2. (s1 minus s2 UNION s3 UNION s4 UNION s5) limit n
     *
     * for example:
     *   IN_SUBSELECT is (s1 minus s2 UNION s3 UNION s4 UNION s5)
     * if 's1 minus s2' has found stored_rows, the later union query blocks
     * are not need to process any more.
     */
    if (curr_qb_index > last_minus_qb_index){
      m_limit_rows = save_limit_rows;
    }

    if (stored_rows >= m_limit_rows) {
      break;
    }

    if (MaterializeIterator::MaterializeQueryBlock(query_block, &stored_rows)) {
      return true;
    }

    ++curr_qb_index;
  }

  /* restore limit_rows */
  m_limit_rows = save_limit_rows;

  return false;
}

/* Is it allowed to replace wild(*) in EXISTS subselect query to constant 1?
 * (1) When the query blocks in EXISTS subselect are all of UNION or not a
 * linkage, it's allowed to replace * to 1;
 * (2) The query blocks in EXISTS subselect includes MINUS, not allowed to
 * to replace.
 *
 * return:
 *   true  -- it's allowed to replace
 *   flase -- not allowed
 */
bool allow_replace_wild_in_exists_subselect(SELECT_LEX_UNIT *unit)
{
  if (unit == nullptr) {
    return false;
  }

  Query_Blocks_Linkage_Type qb_type =
    get_query_blocks_linkage_type(unit->first_select());

  if (qb_type == Query_Blocks_Linkage_Type::ALL_ARE_UNION ||
      qb_type == Query_Blocks_Linkage_Type::NOT_LINKAGE) {
    return true;
  }

  return false;
}

/* Is minus operator to the given query block?
 *
 * query_block(in):
 *
 * return:
 *   true  -- the query block of the given join is a MINUS
 *   false -- not a MINUS
 */
bool is_minus_query_block(const MaterializeIterator::QueryBlock &query_block)
{
  JOIN *join = query_block.join;

  if (join && join->select_lex->linkage == EXCEPT_TYPE) {
    /* the result set of query_block need do MINUS operate. */
    return true;
  }

  return false;
}

/* Is union operator to the given query block?
 *
 * query_block(in):
 *
 * return:
 *   true  -- the query block of the given join is a UNION
 *   false -- not a UNION
 */
bool is_union_query_block(const MaterializeIterator::QueryBlock &query_block)
{
  JOIN *join = query_block.join;

  if (join && join->select_lex->linkage == UNION_TYPE) {
    /* the result set of query_block need do UNION operate. */
    return true;
  }

  return false;
}

/* delete table->record[0] from temp table:
 *   first try to delete by index, if fail, turn to rnd.
 * Used in MINUS.
 *
 * table(in): temp table
 * stored_rows(out): the rows after deleting
 *
 * return:
 *   0     -- no error
 *   other -- has error. Return error code.
 */
int delete_row_for_minus(TABLE *table, ha_rows *stored_rows)
{
  int error;

  DBUG_ASSERT(table != nullptr);
  DBUG_ASSERT(stored_rows != nullptr);

  /* when exists hash_field, delete be index of hash_field */
  if (table->hash_field) {
    DBUG_ASSERT(!table->no_keyread);

    if (check_unique_constraint(table)) {
      /* not found */
      return 0;
    }

    error = table->file->ha_delete_row(table->record[0]);
    if (0 == error) {
      /* error 0 means deleting one row successfully */
      --*stored_rows;
    }
    return error;
  }

  /* Initialize the index used for finding the deleting recored. */
  error = table->file->ha_index_init(0, 0);
  if (error != 0) {
    return error;
  }

  /* below delete record by index */
  uchar key[MAX_KEY_LENGTH];
  key_copy(key, table->record[0], table->key_info,
           table->key_info->key_length);
  error = table->file->ha_index_read_map(
           table->record[0], key, HA_WHOLE_KEY, HA_READ_KEY_EXACT);

  switch (error) {
    case HA_ERR_KEY_NOT_FOUND:
      error = 0;
      break;

    case 0:
      error = table->file->ha_delete_row(table->record[0]);
      if (0 == error) {
        /* error 0 means deleting one row successfully */
        --*stored_rows;
      }
      break;

    default:
      break;
  }

  table->file->ha_index_end();

  return error;
}

