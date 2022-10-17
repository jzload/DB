/**
  @file sql/oracle_compatibility/parse_tree_transformer.cc

  @brief
  This file implements Parse_tree_transformer classes.
*/

#include "sql/oracle_compatibility/parse_tree_transformer.h"

/**
  transform db&table names of parse tree nodes, and set name context
  in replace info, for `start with connect by`
*/
void Replace_db_table_name_context_transformer::transform(Parse_tree_node *ptn) {
  PTI_simple_ident_q_3d *ident_3q = NULL;
  PTI_table_wild *wild = NULL;
  Item_field *field = NULL;

  if((ident_3q = dynamic_cast<PTI_simple_ident_q_3d*>(ptn))) {
    ident_3q->set_schema_table(db, table);
    ident_3q->name_replace_info.set_name_context(DERIVED_FIRST_SELECT);
  }
  else if((wild = dynamic_cast<PTI_table_wild*>(ptn))) {
    wild->set_schema_table(db, table);
    wild->name_replace_info.set_name_context(DERIVED_FIRST_SELECT);
  }
  else if((field = dynamic_cast<Item_field*>(ptn))) {
    field->set_schema_table(db, table);
    field->name_replace_info.set_name_context(DERIVED_FIRST_SELECT);
  }
}


