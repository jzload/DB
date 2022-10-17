#ifndef INSERT_ALL_INCLUDED
#define INSERT_ALL_INCLUDED


#include "sql/sql_lex.h"
#include "sql/parse_tree_helpers.h"
#include "sql/parse_tree_nodes.h"


/*
table_fields_pair is used to store table and column list in
insert all into statement
*/
typedef struct table_fields_pair {
  Table_ident *table;
  PT_item_list *column_list;
} table_fields_pair;

/*
table_values_pair is used to store table and value list in
insert all into statement
*/
typedef struct table_values_pair {
  Table_ident *table;
  PT_insert_values_list *row_values_list;
} table_values_pair;

#endif /* INSERT_ALL_INCLUDED */
