#include "sql/oracle_compatibility/start_with_connect_by.h"
#include "sql/sql_class.h"
#include "sql/item.h"
#include "sql/parse_tree_nodes.h"
#include "sql/parse_tree_items.h"
#include "sql/strfunc.h"
#include "sql/oracle_compatibility/parse_tree_transformer.h"
#include "sql/protocol.h"

/**
  check some constraints for `start with connect by` usage

  @param pc: parse context of select_lex
  @param connect_by_node: the query_specification node, which contains hierarchical_clause
  @param out_query: the query_expression node to hold result of transformation of `start with connect by`

  @return true: error occurs; false: check succss
*/
bool transform_swcb_check_constraint(PT_query_specification *connect_by_node,
                                     PT_query_expression *out_query) 
{
  DBUG_ASSERT(connect_by_node);
  DBUG_ASSERT(out_query);
  DBUG_ASSERT(connect_by_node->hierarchical_clause());

  /* check, `with recursive` cannot be used together with `start with connect by` */
  if(out_query->with_clause()){
    my_error(ER_START_WITH_CONNECT_BY, MYF(0), 
        "`start with connect by` cannot exists together with `with recursive`");
    return true;
  }

  if(connect_by_node->window_clause() || 
      connect_by_node->group_clause() || 
      connect_by_node->having_clause()) {
    //TODO: window function not supported in `start with connect by` query
    //Oracle doesn't support `group by` or `having` together with `start with connect by`
    my_error(ER_START_WITH_CONNECT_BY, MYF(0),  
        "no usage for window/group/having in `start with connect by`");
    return true;
  }

  //Single table supported now, and `from dual` is OK
  if(connect_by_node->get_from_clause().size() > 1 ||
      (connect_by_node->get_from_clause().size() == 1 &&
      nullptr == dynamic_cast<PT_table_factor_table_ident*>(connect_by_node->get_from_clause().at(0))) ) {
    my_error(ER_START_WITH_CONNECT_BY, MYF(0), "single table supported");
    return true;
  }

  return false;
}

/**
  generate some idents which are used in transformation of `start with connect by`

  @param pc: parse context of select_lex
  @param connect_by_node: the query_specification node, which contains hierarchical_clause
  @param cte_lstr:  out, ident of `#tmp_inner_cte` table, LEX_STRING type
  @param cte_lcstr: out, ident of `#tmp_inner_cte` table, LEX_CSTRING type
  @param cte_tab: out, table_ident object for `#tmp_inner_cte` table
  @param cur_tab: out, table_ident object for current table

  @return true: failure; false: success
*/
bool transform_swcb_generate_idents(Parse_context *pc, PT_query_specification *connect_by_node,
                                    LEX_STRING *&cte_lstr,  LEX_CSTRING *&cte_lcstr,
                                    PT_table_factor_table_ident *&cte_tab,
                                    PT_table_factor_table_ident *&cur_tab) 
{
  THD *thd = pc->thd;
  Table_ident *cte_table_ident_base = NULL;
  replace_global_info rgi;

  char cte_name[64] = {0};
  const char *cte_name_header = "#tmp_inner_cte";
  snprintf(cte_name, sizeof(cte_name), "%s_%lu", cte_name_header, (uint64)pc);

  if(!(cte_lstr = new(thd->mem_root) LEX_STRING()) ||
      lex_string_strmake(thd->mem_root, cte_lstr, cte_name, my_strlen(cte_name))) {
    return true;
  }

  if(!(cte_lcstr = new(thd->mem_root) LEX_CSTRING()) ||
      lex_string_strmake(thd->mem_root, cte_lcstr, cte_name, my_strlen(cte_name))){
    return true;
  }

  if(!(cte_table_ident_base = new(thd->mem_root) Table_ident(*cte_lcstr))) {
    return true;
  }

  if(!(cte_tab = new(thd->mem_root) PT_table_factor_table_ident\
        (cte_table_ident_base, NULL, NULL_CSTR, NULL)) ) {
    return true;
  }

  // DUAL is supported
  if(connect_by_node->get_from_clause().size() == 0) {
    cur_tab = nullptr;
    rgi.cur_schema = nullptr;
    rgi.cur_table = nullptr;

  } else {
    // current table (has been checked previously! cannot be null except for DUAL)
    cur_tab = dynamic_cast<PT_table_factor_table_ident*>(connect_by_node->get_from_clause().at(0));

    //save schema/table name replacememt info
    if(cur_tab->table_alias()) {
      rgi.cur_schema = nullptr;
      rgi.cur_table = cur_tab->table_alias();
    } else {
      rgi.cur_schema = cur_tab->get_table_ident()->db.str;
      rgi.cur_table  = cur_tab->get_table_ident()->table.str;
    }
  }

  rgi.prior_schema = cte_tab->get_table_ident()->db.str;
  rgi.prior_table  = cte_tab->get_table_ident()->table.str;

  pc->thd->lex->push_rgi(rgi);

  return false;
}

/**
  make up second child of union in cte
*/
static bool transform_swcb_setup_cte_rhs(Parse_context *pc, 
                                         PT_query_specification *connect_by_node,
                                         PT_table_factor_table_ident *cte_tab, 
                                         PT_table_factor_table_ident *cur_tab,
                                         Parse_tree_transformer *ptt, 
                                         PT_query_specification *&union_rhs) 
{
  THD *thd = pc->thd;

  //we make union children basing on orgin select_node, so we firstly clone a query_node
  if( !(union_rhs = static_cast<PT_query_specification*>(connect_by_node->clone(ptt))) ) {
    return true;
  }

  //union second will replace db/table names duaring contextualizing
  union_rhs->set_need_replace_schema_table(true);

  /* (1) for union second child, convert it to a join, and use `connect by` conditions
         as join_cond, and clear `start with connect by` conditions
  */
  Item *union_rhs_join_on_cond = NULL;
  union_rhs_join_on_cond = union_rhs->hierarchical_clause()->recurs_end();

  // make up join_on from `connect by` condition */
  Mem_root_array_YY<PT_table_reference *> join_from_clause;
  join_from_clause.init(thd->mem_root);

  PT_table_factor_table_ident *cur_tab_clone = NULL;
  PT_table_factor_table_ident *cte_tab_clone = NULL;

  if(!(cte_tab_clone = static_cast<PT_table_factor_table_ident*>(cte_tab->clone(ptt)))) {
    return true;
  }

  // cur_tab exists commonly
  if(cur_tab) {
    if(!(cur_tab_clone = static_cast<PT_table_factor_table_ident*>(cur_tab->clone(ptt))) ) {
      return true;
    }

    PT_joined_table_on *joined_table_on = new(thd->mem_root) PT_joined_table_on(cur_tab_clone, \
        POS(), JTT_INNER, cte_tab_clone, union_rhs_join_on_cond);
    if(!joined_table_on) {
      return true;
    }
    join_from_clause.push_back(joined_table_on);

    union_rhs->set_from_clause(join_from_clause);

    /* (2) convert select_list of union children nodes to `select *`
    */
    PT_select_item_list *il2 = new(thd->mem_root) PT_select_item_list;
    Item *item2 = new(thd->mem_root) Item_field(POS(), NULL, NULL, "*");
    if(!il2 || !item2) {
      return true;
    }
    il2->push_back(item2);
    union_rhs->set_item_list(il2);
    union_rhs->set_where_clause(nullptr);

  } else {
    //DUAL, transform to `from cte where ...`
    Mem_root_array_YY<PT_table_reference *> table_reference_list;
    table_reference_list.init(thd->mem_root);

    table_reference_list.push_back(cte_tab_clone);
    union_rhs->set_from_clause(table_reference_list);


    Item *where = new(thd->mem_root) PTI_where(POS(), union_rhs_join_on_cond);
    if(!where) {
      return true;
    }
    union_rhs->set_where_clause(where);
  }

  /* (3) clear `start with connect by` info
  */
  union_rhs->set_hierarchical_clause(NULL);

  /* (4) clear options and hints
  */
  Query_options empty_options;
  empty_options.query_spec_options = 0;
  union_rhs->set_options(empty_options);

  union_rhs->set_opt_hints(nullptr);


  return false;
}

/**
  make up the first child of union in cte
*/
static bool transform_swcb_setup_cte_lhs(Parse_context *pc,
                                         PT_query_specification *connect_by_node,
                                         Parse_tree_transformer *ptt,
                                         PT_query_specification *&union_lhs)
{
  THD *thd = pc->thd;

  //we make union children basing on orgin select_node, so we firstly clone a query
  if(!(union_lhs = static_cast<PT_query_specification*>(connect_by_node->clone(ptt)) )) {
    return true;
  }

  /* (1) for union first child, merge `start with` conditions to where_clause,
         and clear `start with connect by` conditions
  */
  Item *union_lhs_where_clause = NULL;
  //override old where_clause
  union_lhs_where_clause = new(thd->mem_root) PTI_where(POS(), union_lhs->hierarchical_clause()->recurs_begin());

  if(!union_lhs_where_clause) return true;
  union_lhs->set_where_clause(union_lhs_where_clause);

  /* (2) convert select_list of union children nodes to `select *`
     but keep select_list for DUAL
  */
  if(connect_by_node->get_from_clause().size() > 0) {
    PT_select_item_list *il1 = new(thd->mem_root) PT_select_item_list;
    Item *item1 = new(thd->mem_root) Item_field(POS(), NULL, NULL, "*");
    if(!il1 || !item1) {
      return true;
    }
    il1->push_back(item1);

    union_lhs->set_item_list(il1);
  }

  /*  (3) clear `start with connect by` conditions
  */
  union_lhs->set_hierarchical_clause(nullptr);

  /*  (4) clear options and hints
  */
  Query_options empty_options;
  empty_options.query_spec_options = 0;
  union_lhs->set_options(empty_options);

  union_lhs->set_opt_hints(nullptr);

  return false;
}


/**
  setup union all node in cte for `start with connect by`

  @param pc: parse context of select_lex
  @param connect_by_node: the query_specification node, which contains hierarchical_clause
  @param cte_tab: table_ident object for `#tmp_inner_cte` table
  @param cur_tab: table_ident object for current table
  @param ptt: Parse_tree_transformer object to use to change parse tree during clone
  @param union_node: out, the generated union all node in cte, which will be used to construct with_clause

  @return true: failure; false: success
*/
static bool transform_swcb_setup_cte_union(Parse_context *pc,
                                           PT_query_specification *connect_by_node,
                                           PT_table_factor_table_ident *cte_tab,
                                           PT_table_factor_table_ident *cur_tab,
                                           Parse_tree_transformer *ptt,
                                           PT_union *&union_node)
{
  THD *thd = pc->thd;

  PT_query_specification *union_lhs  = NULL;
  PT_query_specification *union_rhs = NULL;

  if(transform_swcb_setup_cte_lhs(pc, connect_by_node, ptt, union_lhs) ||
      transform_swcb_setup_cte_rhs(pc, connect_by_node, 
        cte_tab, cur_tab, ptt, union_rhs) ) {
    return true;
  }

  PT_query_expression_body_primary *union_lhs_prim = NULL;
  PT_query_expression_body_primary *union_rhs_prim = NULL;
  if( !(union_lhs_prim = new(thd->mem_root) PT_query_expression_body_primary(union_lhs)) ||
      !(union_rhs_prim = new(thd->mem_root) PT_query_expression_body_primary(union_rhs))) {
    return true;
  }

  PT_query_expression *union_lhs_expr = NULL;
  PT_query_expression *union_rhs_expr = NULL;
  if(!(union_lhs_expr = new(thd->mem_root) PT_query_expression(union_lhs_prim, NULL, NULL, NULL)) ||
      !(union_rhs_expr = new(thd->mem_root) PT_query_expression(union_rhs_prim, NULL, NULL, NULL))) {
    return true;
  }

  PT_nested_query_expression *union_rhs_qe = new(thd->mem_root) PT_nested_query_expression(union_rhs_expr);
  if(!union_rhs_qe) {
    return true;
  }

  //connect two nodes to PT_union
  if(!(union_node = new(thd->mem_root) PT_union(union_lhs_expr, POS(), false, union_rhs_qe))) {
    return true;
  }

  return false;
}


/**
  setup with_clause node in cte for `start with connect by`

  @param pc: parse context of select_lex
  @param connect_by_node: the query_specification node, which contains hierarchical_clause
  @param cte_tab: table_ident object for `#tmp_inner_cte` table
  @param cur_tab: table_ident object for current table
  @param cte_lstr: ident of `#tmp_inner_cte` table, LEX_STRING type
  @param ptt: Parse_tree_transformer object to use to change parse tree during clone
  @param with_clause: out, the generated with_clause node in cte definition, which is used to construct cte

  @return true: failure; false: success
*/

bool transform_swcb_generate_withclause(Parse_context *pc, PT_query_specification *connect_by_node,
                                        PT_table_factor_table_ident *cte_tab,
                                        PT_table_factor_table_ident *cur_tab,
                                        LEX_STRING *cte_lstr, Parse_tree_transformer *ptt,
                                        PT_with_clause *&with_clause)
{
  THD *thd = pc->thd;
  PT_union *union_node = NULL;
  if(transform_swcb_setup_cte_union(pc, connect_by_node, cte_tab, cur_tab, ptt, union_node)) {
    return true;
  }

  //this is the inner query of with_clause
  PT_query_expression *qe = new(thd->mem_root) \
      PT_query_expression(union_node, NULL, NULL, NULL);
  if(!qe) return true;

  PT_subquery *cte_subquery = new(thd->mem_root) PT_subquery(POS(), qe);
  if(!cte_subquery) return true;

  Create_col_name_list *col_name_list = new(thd->mem_root) Create_col_name_list();
  LEX_STRING *subq_text = new(thd->mem_root) LEX_STRING();
  if(!col_name_list || !subq_text) return true;

  PT_common_table_expr *cte = new(thd->mem_root) PT_common_table_expr\
      (*cte_lstr, *subq_text, 0, cte_subquery, col_name_list, thd->mem_root);
  PT_with_list *with_list = new(thd->mem_root) PT_with_list(thd->mem_root);
  if(!cte || !with_list) return true;

  with_list->push_back(cte);
  if(!(with_clause = new(thd->mem_root) PT_with_clause(with_list, true)))
    return true;

  return false;
}


/**
  setup select node which access fields from cte, like: select c1,c2 from `#tmp_inner_cte`

  @param pc: parse context of select_lex
  @param connect_by_node: the query_specification node, which contains hierarchical_clause
  @param cte_tab: table_ident object for `#tmp_inner_cte` table
  @param body_primary: out, the generated query_body_primary, which will be wrapped in query_expression

  @return true: failure; false: success

  @note: for instance, 
           `select pid, ppid from nums where pid > 6 start with pid = 1 connect by ppid = prior pid` 
         will be replaced as:
             with recursive
               `#tmp_inner_cte` as (
                 select * from `nums` where pid = 1
                 union all
                 select `nums`.* from `nums` join `#tmp_inner_cte` on `nums`.`ppid` = `#tmp_inner_cte`.`pid`
               )
             select pid, ppid from `#tmp_inner_cte` where pid > 6;
*/
bool transform_swcb_create_outer_select(Parse_context *pc,
                                        PT_query_specification *connect_by_node,
                                        PT_table_factor_table_ident *cte_tab,
                                        PT_query_expression_body_primary *&body_primary) 
{
  THD *thd = pc->thd;

  // db&table names in item_list will be replaced as `#tmp_inner_cte` and checked later
  Replace_db_table_name_context_transformer *rdt_ptt = new(thd->mem_root) 
      Replace_db_table_name_context_transformer(
      pc->thd->lex->current_rgi().prior_schema, 
      pc->thd->lex->current_rgi().prior_table);
  if(!rdt_ptt) return true;

  // clone item_list
  PT_item_list *item_list = nullptr;

  if(connect_by_node->get_from_clause().size() != 0) {
    item_list = static_cast<PT_item_list*>(connect_by_node->get_item_list()->clone(rdt_ptt));
    if(!item_list) 
      return true;
  } else {
    //DUAL
    if(!(item_list = new(thd->mem_root) PT_select_item_list)) {
      return true;
    }
    Item *item2 = new(thd->mem_root) Item_field(POS(), NULL, NULL, "*");
    item_list->push_back(item2);
  }

  Mem_root_array_YY<PT_table_reference *> table_reference_list;
  table_reference_list.init(thd->mem_root);
  PT_table_factor_table_ident *cte_tab_clone = nullptr;
  if(!(cte_tab_clone = static_cast<PT_table_factor_table_ident*>(cte_tab->clone(NULL)))) {
    return true;
  }
  table_reference_list.push_back(cte_tab_clone);

  // clone where_clause and remove db&table names
  Item *where_clause = connect_by_node->where_clause();
  if(where_clause && !(where_clause = static_cast<Item*>(where_clause->clone(rdt_ptt))) ) {
    // clone failed
    return true;
  }

  //just use it, no need to clone
  PT_hint_list *opt_hints = connect_by_node->get_opt_hints();
  Query_options select_options_param = connect_by_node->get_options();

  PT_query_specification *outer_query_expr_body = new(thd->mem_root) PT_query_specification(
      opt_hints, select_options_param, item_list,
      NULL, table_reference_list, where_clause,
      NULL, NULL, NULL, NULL);
  if(!outer_query_expr_body) return true;

  body_primary = new(thd->mem_root) PT_query_expression_body_primary(outer_query_expr_body);
  if(!body_primary) return true;

  return false;
}


/*
  interface to transform `start with connect by` to `with recursive`, for query_expression

  @param pc: parse context of select_lex
  @param connect_by_node: current PT_query_specification node with `start with connecy by`
  @param out_query: parent node of @connect_by_node, type is PT_query_expression, 
                    of which body and with_clause will be replaced
*/
bool transform_swcb_qs(Parse_context *pc, PT_query_specification *connect_by_node, \
                       PT_query_expression *out_query)
{
  /* STEP-0: check some constraints for `start with connect by` */
  if(transform_swcb_check_constraint(connect_by_node, out_query))
    return true;

  /* Prepare 2 table nodes: cte table and current table */
  LEX_STRING *cte_lstr = NULL; /* `#tmp_inner_cte` */
  LEX_CSTRING *cte_lcstr = NULL; /* `#tmp_inner_cte` */
  PT_table_factor_table_ident *cte_tab = NULL; /* table ident of `#tmp_inner_cte` */
  PT_table_factor_table_ident *cur_tab = NULL; /* table ident of current table */

  /* generate idents for tables */
  if(transform_swcb_generate_idents(pc, connect_by_node, cte_lstr, 
      cte_lcstr, cte_tab, cur_tab)) {
    return true;
  }

  /* STEP-1: generate with_clause */
  PT_with_clause *with_clause = NULL;
  Parse_tree_transformer *ptt = NULL;

  if(transform_swcb_generate_withclause(pc, connect_by_node, cte_tab, 
      cur_tab, cte_lstr, ptt, with_clause)) {
    return true;
  }

  /* STEP-2: generate the `select pid, ppid from cte where pid > 6` query_expression_body */
  PT_query_expression_body_primary *body_primary = NULL;
  if(transform_swcb_create_outer_select(pc, connect_by_node, cte_tab, body_primary)) {
    return true;
  }

  /* STEP-3: generate an outer query_expression */
  out_query->set_with_clause(with_clause);
  out_query->set_body(body_primary);
  return false;
}



/*
  transform `start with connect by` to `with recursive`, for PT_union

  @param connect_by_node: current PT_query_specification node with `start with connecy by`
  @param union_parent: parent node of @connect_by_node, type is PT_union, 
                       of which rhs will be replaced

  @return true: failure; false: success
*/
bool transform_swcb_union_parent(Parse_context *pc,
                                 PT_query_specification *connect_by_node,
                                 PT_union *union_parent)
{
  THD *thd = pc->thd;
  PT_query_expression *out_query_expr = new(thd->mem_root) PT_query_expression\
      (NULL, NULL, NULL, NULL, NULL);
  if(!out_query_expr) {
    return true;
  }

  if(transform_swcb_qs(pc, connect_by_node, out_query_expr)) {
    return true;
  }

  if(connect_by_node != union_parent->rhs()) {
    my_error(ER_START_WITH_CONNECT_BY, MYF(0), "query node not consistent");
    return true;
  }

  union_parent->set_rhs(new(thd->mem_root) PT_nested_query_expression(out_query_expr));
  return false;
}

/**
  judge if item is Item_ident type, including Item_field and Item_ref

  @param item: input item to judge

  @return true if is Item_ident; else false
*/
inline static bool is_item_ident(Item *item) {
  if(item->type() == Item::FIELD_ITEM ||
      item->type() == Item::REF_ITEM) {
    return true;
  }

  return false;
}

/**
  judge if item is Parse_tree_item type

  @param item: input item to judge

  @return true if is Parse_tree_item; else false
*/
inline static bool is_parse_tree_item(Item *item) {
  return (item->type() == Item::INVALID_ITEM);
}

/**
  copy replace info to Item_ident(Item_field or Item_ref), from Parse_tree_item or Item_ident

  @param dst: the dst to which copy replace info
  @param src: the src from which copy replace info

  @return true: failure; false: success
*/
bool pass_item_replace_info(Item *dst, Item* src) {
  //check type of dst, which should be Item_ident
  if(!is_item_ident(dst)) {
    my_error(ER_START_WITH_CONNECT_BY, MYF(0), "mark replace failed");
    return true;
  }
  Item_ident *ident = down_cast<Item_ident*>(dst);

  //check type of src, which should be Item_ident or Parse_tree_item
  if(is_item_ident(src)) {
    ident->name_replace_info = down_cast<Item_ident*>(src)->name_replace_info;
  } else if(is_parse_tree_item(src)) {
    ident->name_replace_info = down_cast<Parse_tree_item*>(src)->name_replace_info;
  } else {
    my_error(ER_START_WITH_CONNECT_BY, MYF(0), "mark replace failed");
    return true;
  }

  return false;
}


/**
  copy table_list from tl to end(linked list), ignoring tables whose name matches `except_table_name`,
  utilized to copy name_resolution_tables

  @param root: mem_root
  @param tl: the table_list to copy
  @param end: the end of tl linked list (end will be also copied)
  @param except_table_name: tables will be skipped if name matches except_table_name

  @return true: failure; false: success
*/
static TABLE_LIST *copy_table_list_except(MEM_ROOT *root, TABLE_LIST *start, 
                                          TABLE_LIST *end, const char *except_table_name) {
  if(!start) return nullptr;

  TABLE_LIST sentinel;
  TABLE_LIST *last = &sentinel;
  TABLE_LIST *cur = start;

  if(end) { 
    end = end->next_name_resolution_table;
  }

  for(; cur != end; cur = cur->next_name_resolution_table) {
    if(except_table_name && 
        my_strcasecmp(table_alias_charset, cur->alias, except_table_name) == 0) {
      continue;
    }

    TABLE_LIST *node = (TABLE_LIST*)root->Alloc(sizeof(TABLE_LIST));
    if(!node) return nullptr;

    memcpy(node, cur, sizeof(TABLE_LIST));
    node->next_name_resolution_table = nullptr;
    last->next_name_resolution_table = node;
    last = node;
  }

  return sentinel.next_name_resolution_table;
}

/**
  judge if two db&table are equal or not. 

  @param db1: first db string
  @param tb1: first table string
  @param db2: second db string
  @param tb2: second table string

  @return true: equal; false: not equal
*/
static bool db_table_equal(const char *db1, const char *tb1, 
                           const char *db2, const char *tb2)
{
  // check if same db
  if(db1 == db2 || (db1 && db2 && strcmp(db1, db2) == 0)) 
  {
    // check if same table
    if(tb1 == tb2 || (tb1 && tb2 && my_strcasecmp(table_alias_charset, tb1, tb2) == 0)) 
    {
      return true;
    }
  }

  return false;
}
/**
  determin if there is need to check replace info of item_idents

  @param cur_ident: current ident to check replace info

  @return true: it's necessary to check replace; false: no need
*/
static bool need_check_replace(THD *thd, Item_ident *cur_ident) {
  if(likely(!thd->lex->is_swcb)) {
    return false;
  }

  Item_replace_info *ri = &cur_ident->name_replace_info;

  if(!ri->db_table_name_fixed) {
    return false;
  }

  // this is a constructed fake ident to check names, no need to check it anymore
  if(ri->fake_ident_flag) {
    return false;
  }

  //all same, didn't changed, no need to check anymore
  if(db_table_equal(ri->replace_orig_db_name, ri->replace_orig_table_name,
      ri->replace_new_schema_name, ri->replace_new_table_name)) {
    return false;
  }

  return true;
}

/**
  duaring validation of origin db/table names, construct an item_ident with 
    old names firstly. then, try to call the fix_fields and check if names match.

  @param cur_ident: the current ident from which we construct a fake_ident
  @param fake_ident: out, the constructed fake ident to validate db/table names
*/
static bool construct_fake_ident_to_validate_names(THD *thd, Item_ident *cur_ident,
                                                   Name_resolution_context *name_context,
                                                   Item_ident *&fake_ident) {
  //clear
  fake_ident = NULL;
  Item_replace_info *ri = &cur_ident->name_replace_info;

  /* except for Item_ref and Item_field, other cases are unexpected! */
  if(cur_ident->type() == Item::REF_ITEM) {
    fake_ident = new (thd->mem_root) Item_field(name_context,
        ri->replace_orig_db_name, ri->replace_orig_table_name,
        cur_ident->field_name);

  } else if(cur_ident->type() == Item::FIELD_ITEM) {
    fake_ident = new (thd->mem_root) Item_field(name_context,
        ri->replace_orig_db_name, ri->replace_orig_table_name,
        cur_ident->field_name);
  } else {
    my_error(ER_START_WITH_CONNECT_BY, MYF(0), "unexpected type of item");
  }

  if(!fake_ident) return true;

  fake_ident->name_replace_info.fake_ident_flag = true; //indicates that this is a fake ident

  return false;
}

/**
  get the first select_lex in the first derived table in current select.
  this is expected: current select contains only 1 table_list, which should be
  a derived table.

  @param thd: THD
  @param sel: out SELECT_LEX, the first select in derived table

*/
static bool get_derived_first_select(THD *thd, SELECT_LEX *&sel) {

  SELECT_LEX *cur_select = thd->lex->current_select();

  //check number of table_list, which is expected as 1
  if(cur_select->table_list.first && !cur_select->table_list.first->next_local) {

    // now we check if derived unit exists
    if(!cur_select->table_list.first->derived_unit() ||
        !cur_select->table_list.first->derived_unit()->first_select()) {
      return true;
    }

    sel = cur_select->table_list.first->derived_unit()->first_select();
    return false;
  }

  return true;
}


/**
  make up a name_context to do the fix_fields of fake_ident duaring old db/table name validation

  @param cur_ident: current ident to validate names
  @param check-context: out, name context to construct to validate names

  @return true: failed; false: success
*/
static bool make_up_name_context_to_validate_names(THD *thd, Item_ident *cur_ident, 
                                                   Name_resolution_context *&check_context)
{
  check_context = (Name_resolution_context *)\
      thd->mem_root->Alloc(sizeof(Name_resolution_context));
  if(!check_context) return true;

  if(cur_ident->name_replace_info.name_context == DERIVED_FIRST_SELECT)
  {
    /* for outer select node of cte, use name context of first select 
         in derived table (union first node)
    */
    SELECT_LEX *derived_first = nullptr;

    if(get_derived_first_select(thd, derived_first)) {
      my_error(ER_START_WITH_CONNECT_BY, MYF(0), "failed to get name_context");
      return true;
    }

    memcpy(check_context, &derived_first->context, sizeof(Name_resolution_context));
  }
  else if(cur_ident->name_replace_info.name_context == CURRENT_CONTEXT_EXCEPT_CTE)
  {
    /* for union second node, use name_context of current select but ignore `cte`
    */
    memcpy(check_context, cur_ident->context, sizeof(Name_resolution_context));
    const char *generate_cte_name = cur_ident->name_replace_info.prior_table;
    TABLE_LIST *new_tl = copy_table_list_except(thd->mem_root, \
        check_context->first_name_resolution_table, \
        check_context->last_name_resolution_table, generate_cte_name);
    if(!new_tl) {
      my_error(ER_START_WITH_CONNECT_BY, MYF(0), "construct name_context failed");
      return true;
    }

    check_context->first_name_resolution_table = new_tl;
    check_context->last_name_resolution_table = nullptr;
  }
  else
  {
    my_error(ER_START_WITH_CONNECT_BY, MYF(0), "unrecognized name_context type");
    return true;
  }

  return false;
}

/**
  check the replace info of ident. used in replacement of db/schema names of fields.
  because names of idents are replaced duaring contextualization, and the 
  correctness of the names can be made sure later, we replace the names firstly 
  and check them later.

  @param thd: THD
  @param cur_ident: the ident after replacing

  @return true: check failed or names not match; false: check success
*/
static bool check_replace_name_valid(THD *thd, Item_ident *cur_ident) {
  if(likely(!need_check_replace(thd, cur_ident))) {
    return false;
  }

  /** different idents use different name_resolution_contexts
      (1) idents in union second child of cte will use current context ignoring
          `#tmp_inner_cte`
      (2) idents in outer select will use context of union first child in derived
          table `#tmp_inner_cte`
  */
  Name_resolution_context *check_context = NULL;
  if(make_up_name_context_to_validate_names(thd, cur_ident, check_context)) {
    return true;
  }

  //construct an item_ident with replace_orig_db/table
  Item_ident *fake_ident = nullptr;

  if(construct_fake_ident_to_validate_names(thd, cur_ident, check_context, fake_ident)) {
    return true;
  }

  // try to fix_fields
  Item *item = NULL;
  return fake_ident->fix_fields(thd, &item);
}

/**
  validate replace info of Item_field
  @return true: check failed; false: check ok
*/
bool Item_field::check_replace_schema_table_valid(THD *thd) {
  return check_replace_name_valid(thd, this);  
}

/**
   validate replace info of Item_ref
   @return true: check failed; false: check ok
*/
bool Item_ref::check_replace_schema_table_valid(THD *thd) {
  return check_replace_name_valid(thd, this);
}


/**
  choose replacing names of schema and table for union second child for cte

  @param prev_schema_name: pre-used schema name
  @param prev_table_name: pre-used table name
  @param pc: parse context of select_lex
  @param out_replaced_schema_name: out, schema name after replacement; maybe changed, maybe not;
  @param out_replaced_table_name: out, table name after replacement; maybe changed, maybe not;
*/
void get_replaced_schema_table_name(const char *prev_schema_name,
                                    const char *prev_table_name,
                                    Parse_context *pc,
                                    const char **out_replaced_schema_name,
                                    const char **out_replaced_table_name)
{
  const char *fld_schema_name = prev_schema_name;
  const char *fld_table_name = prev_table_name;

  if(pc->select->if_need_replace_schema_table) {
    if(!pc->select->prior_expr_flag) {
      fld_schema_name = pc->thd->lex->current_rgi().cur_schema;
      fld_table_name = pc->thd->lex->current_rgi().cur_table;
    } else {
      fld_schema_name = pc->thd->lex->current_rgi().prior_schema;
      fld_table_name = pc->thd->lex->current_rgi().prior_table;
    }
  }

  *out_replaced_schema_name = fld_schema_name;
  *out_replaced_table_name = fld_table_name;
}

/**
  transform parse tree for `start with connect by` query_expression

  @param pc: parse context
  @param transformed: if has transformed for `start with connect by`

  @return true: transform failed; false: transform succss
*/
bool PT_query_expression::transform_swcb(Parse_context *pc, bool &transformed) {
  //common query, not in a union
  if(!is_union()) 
  {
    PT_query_expression_body_primary *body_primary = dynamic_cast<PT_query_expression_body_primary*>(m_body);
    if(!body_primary) {
      my_error(ER_START_WITH_CONNECT_BY, MYF(0), "unexpected body type");
      return true;
    }

    PT_query_primary *query_primary = body_primary->query_primary();
    if(!query_primary) {
      my_error(ER_START_WITH_CONNECT_BY, MYF(0), "unexpected qp type");
      return true;
    }

    PT_query_specification *query_specification = dynamic_cast<PT_query_specification*>(query_primary);
    if(!query_specification) {
      // it's a PT_nested_query_expression, no need to transform
      return false;
    }

    if(query_specification->hierarchical_clause()) {
      transformed = true;
      return transform_swcb_qs(pc, query_specification, this);
    }
  } 
  else 
  {
    PT_union *union_body = dynamic_cast<PT_union*>(m_body);
    if(!union_body || !union_body->rhs()) {
      //it's ok, maybe the union is in the deeper layer
      return false;
    }

    PT_query_specification *query_specification = dynamic_cast<PT_query_specification*>(union_body->rhs());
    if(!query_specification) {
      //rhs is a PT_nested_query_expression, no need to transform
      return false;
    }

    if(query_specification->hierarchical_clause()) {
      transformed = true;
      return transform_swcb_union_parent(pc, query_specification, union_body);
    }
  }

  return false;
}


/**
  replace db&table names with for `start with connect by`, and the new names 
    will be used to construct Item_field with replaced names.
  used for union second child.

  @param pc: parse context
*/
void PTI_table_wild::fix_db_table_name(Parse_context *pc) {
  schema = pc->thd->get_protocol()->has_client_capability(CLIENT_NO_SCHEMA)
               ? NullS
               : schema;

  Item_replace_info *ri = &name_replace_info;
  ri->prior_table = pc->thd->lex->current_rgi().prior_table;

  if(!ri->db_table_name_fixed) {
    ri->replace_orig_db_name = schema;
    ri->replace_orig_table_name = table;
    get_replaced_schema_table_name(ri->replace_orig_db_name, ri->replace_orig_table_name, \
        pc, &ri->replace_new_schema_name, &ri->replace_new_table_name);
    ri->db_table_name_fixed = true;
  }
}

/**
  replace db&table names for `start with connect by`, and the new names
    will be used to construct Item_field with replaced names
  used for union second child.

  @param pc: parse context
*/
void PTI_simple_ident_ident::fix_db_table_name(Parse_context *pc) {
  Item_replace_info *ri = &name_replace_info;
  ri->prior_table = pc->thd->lex->current_rgi().prior_table;

  if(!ri->db_table_name_fixed) {
    ri->replace_orig_db_name = NullS;
    ri->replace_orig_table_name = NullS;
    get_replaced_schema_table_name(ri->replace_orig_db_name, ri->replace_orig_table_name, \
        pc, &ri->replace_new_schema_name, &ri->replace_new_table_name);
    ri->db_table_name_fixed = true;
  }
}

/**
  replace db&table names for `start with connect by`, and the new names
    will be used to construct Item_field with replaced names
  used for union second child.

  @param pc: parse context
*/
void PTI_simple_ident_q_3d::fix_db_table_name(Parse_context *pc) {
  THD *thd = pc->thd;
  const char *schema =
      thd->get_protocol()->has_client_capability(CLIENT_NO_SCHEMA) ? nullptr
                                                                   : db;
  Item_replace_info *ri = &name_replace_info;
  ri->prior_table = pc->thd->lex->current_rgi().prior_table;

  if(!ri->db_table_name_fixed) {
    ri->replace_orig_db_name = schema;
    ri->replace_orig_table_name = table;
    get_replaced_schema_table_name(ri->replace_orig_db_name, ri->replace_orig_table_name, \
        pc, &ri->replace_new_schema_name, &ri->replace_new_table_name);
    ri->db_table_name_fixed = true;
  }
}

/**
  replace db&table names of Item_field  for `start with connect by` dirrectly

  @param pc: parse context
*/
void Item_field::fix_db_table_name(Parse_context *pc) {
  //replace schema & table name
  Item_replace_info *ri = &name_replace_info;
  ri->prior_table = pc->thd->lex->current_rgi().prior_table;

  if(!ri->db_table_name_fixed) {
    ri->replace_orig_db_name = db_name;
    ri->replace_orig_table_name = table_name;
    get_replaced_schema_table_name(ri->replace_orig_db_name, ri->replace_orig_table_name, \
        pc, &ri->replace_new_schema_name, &ri->replace_new_table_name);

    ri->db_table_name_fixed = true;

    set_db_table_name(ri->replace_new_schema_name, ri->replace_new_table_name);
  }
}

/**
  set names of db and table for Item_field 
*/
void Item_field::set_db_table_name(const char *db_arg, const char *table_arg) {
  db_name = db_arg;
  table_name = table_arg;
}


/**
  set replaced schema/tables names, for outer select `start with connect by` 
    (after transformation)
*/
void PTI_table_wild::set_schema_table(const char *db_arg, const char *table_arg) {
  schema = current_thd->get_protocol()->has_client_capability(CLIENT_NO_SCHEMA)
           ? NullS
           : schema;

  Item_replace_info *ri = &name_replace_info;
  ri->replace_orig_db_name = schema;
  ri->replace_orig_table_name = table;
  ri->replace_new_schema_name = db_arg;
  ri->replace_new_table_name = table_arg;
  ri->db_table_name_fixed = true;
}

/**
  set replaced schema/tables names, for outer select `start with connect by` 
    (after transformation)
*/
void PTI_simple_ident_q_3d::set_schema_table(const char *db_arg, const char *table_arg) {
  const char *schema =
      current_thd->get_protocol()->has_client_capability(CLIENT_NO_SCHEMA) ? nullptr
                                                                   : db;
  Item_replace_info *ri = &name_replace_info;
  ri->replace_orig_db_name = schema;
  ri->replace_orig_table_name = table;
  ri->replace_new_schema_name = db_arg;
  ri->replace_new_table_name = table_arg;
  ri->db_table_name_fixed = true;
}

/**
  set replaced schema/tables names, for outer select `start with connect by` 
    (after transformation)
*/
void Item_ident::set_schema_table(const char *db_arg, const char *table_arg) {
  //replace schema & table name
  Item_replace_info *ri = &name_replace_info;
  ri->replace_orig_db_name = db_name;
  ri->replace_orig_table_name = table_name;
  ri->replace_new_schema_name = db_arg;
  ri->replace_new_table_name = table_arg;
  ri->db_table_name_fixed = true;
}

