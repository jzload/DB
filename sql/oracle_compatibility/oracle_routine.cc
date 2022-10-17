#include "oracle_routine.h"

/**
  some functions to generate full_name of ident
*/
static void default_ident_if_null(const char *&ident) {
  if(!ident) {
    ident = "[unspecified]";
  }
}

static const char *construct_3d_full_name(THD *thd, const char *db,
                                          const char *table, const char *field) {
  default_ident_if_null(db);
  default_ident_if_null(table);
  default_ident_if_null(field);

  size_t len = my_strlen(db) + my_strlen(table) + my_strlen(field) + 3;
  char *full_name = new (thd->mem_root) char[len];

  if(!full_name) return nullptr;

  snprintf(full_name, len, "%s.%s.%s", db, table, field);
  return full_name;
}

static const char *construct_2d_full_name(THD *thd, const char *table, const char *field) {
  default_ident_if_null(table);
  default_ident_if_null(field);

  size_t len = my_strlen(table) + my_strlen(field) + 2;
  char *full_name = new (thd->mem_root) char[len];

  if(!full_name) return nullptr;

  snprintf(full_name, len, "%s.%s", table, field);
  return full_name;
}

static const char *construct_1d_full_name(THD *thd, const char *field) {
  default_ident_if_null(field);

  size_t len = my_strlen(field) + 1;
  char *full_name = new (thd->mem_root) char[len];

  if(!full_name) return nullptr;

  snprintf(full_name, len, "%s", field);
  return full_name;
}

/**
  interface to generate full name of ident

  @param thd: THD
  @param db: database name
  @param table: table name
  @param field: field name

  @note some instances:
      'db1','tb1','fld1' ==> "db1.tb1.fld1"
       NULL,'tb1','fld1' ==> "tb1.fld1"
       NULL, NULL,'fld1' ==> "fld1"
      'db1', NULL,'fld1' ==> "db1.[unspecified].fld1"
       NULL,'tb1', NULL  ==> "tb1.[unspecified]"
       NULL, NULL, NULL  ==> ""
*/
const char *construct_full_name(THD *thd, const char *db,
                                const char *table, const char *field) {
  if(db) {
    return construct_3d_full_name(thd, db, table, field);
  } else if(table) {
    return construct_2d_full_name(thd, table, field);
  } else if(field) {
    return construct_1d_full_name(thd, field);
  } else {
    return "";
  }
}


