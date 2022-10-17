/**
  @file sql/oracle_compatibility/parse_tree_transformer.h

  @brief
  This file defines Parse_tree_transformer class and derived classes .
*/
#ifndef PARSE_TREE_TRANSFORMER_H
#define PARSE_TREE_TRANSFORMER_H

#include "sql/parse_tree_node_base.h"  // Parse_tree_node
#include "sql/sql_lex.h"
#include "sql/parse_tree_items.h"

/*
  @description
  this class is utilized to transform parse_tree_nodes while cloning the parse tree.
  for each parse_tree_node, transform() will be called.

  @note
  each transforming strategy should be a subclass.
*/
class Parse_tree_transformer {
 public:
  virtual ~Parse_tree_transformer(){}
  virtual void transform(Parse_tree_node *ptn) = 0;
  virtual void transform(Table_ident *ptn) = 0;
  virtual void transform(Index_hint *ptn) = 0;

};

class Replace_db_table_name_context_transformer : public Parse_tree_transformer {

 public:
  Replace_db_table_name_context_transformer(const char *db_arg, const char *table_arg) {
    db = db_arg;
    table = table_arg;
  }

 public:
  void transform(Parse_tree_node *ptn) override;

  void transform(Table_ident * /*ptn*/) { return; }

  void transform(Index_hint * /*ptn*/) { return; }

 private:
  const char *db;
  const char *table;
};

#endif
