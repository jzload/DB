#ifndef START_WITH_CONNECT_BY_H
#define START_WITH_CONNECT_BY_H

#include "sql/parse_tree_items.h"


bool pass_item_replace_info(Item *dst, Item *src);

bool check_replace_schema_table_valid(Item *res);

void get_replaced_schema_table_name(const char *prev_schema_name, 
    const char *prev_table_name, Parse_context *pc, 
    const char **out_replaced_schema_name, const char **out_replaced_table_name);

#endif
