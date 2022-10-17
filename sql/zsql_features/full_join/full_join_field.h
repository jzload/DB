#ifndef FULL_JOIN_FIELD_H
#define FULL_JOIN_FIELD_H

#include "sql/sql_list.h"

class Field;
class TABLE_LIST;
class THD;
class Item;
class Item_ident;
class Item_field;
class Item_type_holder;
class Item_name_string;
class Field_iterator_table_ref;

/* 
 * class for name resolution of FULL JOIN FIELD
 * @see comments in full_join.cc
 */
class Full_join_field
{
public:
  const char *real_db_name;
  const char *real_table_name;
  const char *real_field_name;
  const char *real_alias;
  bool is_natural_join_field;
  Item_ident *item;
  Item_type_holder *type_holder{NULL};
  Field *field{NULL};
  Full_join_field(THD *thd,
                  const char *db_arg,
                  const char *table_arg,
                  const char *field_arg,
                  const Item_name_string &alias_arg,
                  bool is_natural_join_field_arg,
                  Item_ident *item_arg);
  Full_join_field(THD *thd,
                  const char *db_arg,
                  const char *table_arg,
                  const char *field_arg,
                  const char *alias_arg,
                  bool is_natural_join_field_arg,
                  Item_ident *item_arg);
  Full_join_field(THD *thd,
                  bool is_natural_join_field_arg,
                  Item_ident *item_arg);
  Item *create_item(THD *thd);
};

/*
  Find field by name in a full join derived table.

  No privileges are checked, and the column is not marked in read_set/write_set.

  SYNOPSIS
    find_field_in_full_join()
    table_list    full join derived table where to search for the field
    db_name       name of db
    table_name    name of table
    field_name    name of field
    found[out]    if has non unique error, found points to the first match field.

  RETURN
    0	field is not found
    #	pointer to field
*/
Field *find_field_in_full_join(TABLE_LIST *table_list,
                               const char *db_name,
                               const char *table_name,
                               const char *field_name,
                               Field **found);

/*
 * Generate full join field for select_lex in insert fields
 * 
 * Param:
 * thd      current thd
 * item     the item to create full join field from
 * field_iterator   the field iterator to get full join field
 * fj_fields_list   output place of the generated full join field
 * 
 * Return
 * true for error, false for complete.
 */
bool generate_full_join_field(THD *thd, Item *item,
                              Field_iterator_table_ref &field_iterator,
                              List<Full_join_field> *fj_fields_list);

/*
 * Test if a db.table name match current Full_join_field.
 * If tables is not a full join derived table, return true;
 * If table_name and db_name is not specified, return true;
 */
bool is_table_name_match_full_join_field(const char *db_name,
                                         const char *table_name,
                                         TABLE_LIST *tables,
                                         Field_iterator_table_ref &field_iterator);

void get_full_join_field_name_from_item(Item_ident *item, const char **field_name,
                                        const char **table_name, const char **db_name);
#endif