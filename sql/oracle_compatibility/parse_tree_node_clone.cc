/**
  @file sql/oracle_compatibility/parse_tree_node_clone.cc

  @brief
  This file implements clone methods for parse_tree_nodes.
*/
#include "sql/oracle_compatibility/parse_tree_node_clone.h"
#include "sql/parse_tree_node_base.h"
#include "sql/parse_tree_nodes.h"
#include "sql/item_strfunc.h"
#include "sql/item_cmpfunc.h"
#include "sql/item_regexp_func.h"
#include "sql/item_json_func.h"
#include "sql/parse_tree_items.h"
#include "sql/parse_tree_hints.h"
#include "sql/oracle_compatibility/parse_tree_transformer.h"
#include "sql/oracle_compatibility/rownum.h"
#include "sql/oracle_compatibility/listagg.h"
#include "sql/oracle_compatibility/systimestamp.h"
/*============================== tools ==================================*/
#ifndef NEW_PTN
#define NEW_PTN new(current_thd->mem_root)
#endif

#define cur_mem_root current_thd->mem_root

#define SAFE_CLONE(x,y) (x ? x->clone(y) : NULL)

String *clone_String(const String *src) {
  char *ptr_clone = current_thd->strmake(src->ptr(), src->length());
  if (!ptr_clone)
    return nullptr;

  String *node = new(cur_mem_root) String(ptr_clone, src->length(), src->charset());
  return node;
}

bool clone_lex_string(LEX_STRING &dest, const LEX_STRING &src) {
  if (src.length > 0 && src.str != nullptr) {
    if (!(dest.str = current_thd->strmake(src.str,src.length))) {
      dest.length = 0;
      return true;
    }
    dest.length = src.length;
  }
  else {
    dest.str = nullptr;
    dest.length = 0;
  }
  return false;
}

bool clone_lex_cstring(LEX_CSTRING &dest, const LEX_CSTRING &src) {
  if (src.length > 0 && src.str != nullptr) {
    if (!(dest.str = current_thd->strmake(src.str,src.length))) {
      dest.length = 0;
      return true;
    }
    dest.length = src.length;
  }
  else {
    dest.str = nullptr;
    dest.length = 0;
  }
  return false;
}

List<String> *clone_String_list(MEM_ROOT* mem_root, const List<String> *src) {
  List<String> *ret = new(mem_root) List<String>;
  if (!ret)
    return nullptr;
  List_iterator_const<String> it(*src);
  const String *obj;
  String *obj_clone;
  while ((obj = it++)) {
    if (!(obj_clone = clone_String(obj)))
      return nullptr;
    if (ret->push_back(obj_clone))
      return nullptr;
  }
  return ret;
}

PT_item_list *clone_func_args_as_item_list(Item **args, uint arg_count,
                                           Parse_tree_transformer *ptt) {
  Item *item_clone = nullptr;
  PT_item_list *opt_list_clone = NEW_PTN PT_item_list();
  if (opt_list_clone == nullptr)
    return nullptr;

  for(uint i = 0; i < arg_count; ++i) {
    if(!(item_clone = static_cast<Item*>(args[i]->clone(ptt)))) {
      return nullptr;
    }
    if (opt_list_clone->push_back(item_clone))
      return nullptr;
  }
  return opt_list_clone;
}

inline void transform_safe(Parse_tree_transformer *ptt, Parse_tree_node *node) {
  if(ptt && node) {
    ptt->transform(node);
  }
}

inline void transform_safe(Parse_tree_transformer *ptt, Table_ident *node) {
  if(ptt && node) {
    ptt->transform(node);
  }
}

inline LEX_CSTRING make_lex_cstring_safe(const char *str) {
  return {str, str ? my_strlen(str) : 0};
}

/* ============================Parse_tree_node============================== */

void *PT_order_expr::clone(Parse_tree_transformer *ptt) const {
  Item *item_clone = NULL;
  PT_order_expr *node = NULL;

  if( !(item_clone = static_cast<Item*>(item_ptr->clone(ptt)) ) ||
    !(node = NEW_PTN PT_order_expr(item_clone, direction)) ) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_order_list::clone(Parse_tree_transformer *ptt) const {
  PT_order_list *node = NEW_PTN PT_order_list();
  PT_order_expr *order_expr_clone = nullptr;
  if(!node) {
    return nullptr;
  }

  //clone all order_expr
  for (ORDER *o = value.first; o != nullptr; o = o->next) {
    if(!(order_expr_clone = static_cast<PT_order_expr*>
                  (static_cast<PT_order_expr *>(o)->clone(ptt)))) {
      return nullptr;
    }
    node->push_back(order_expr_clone);
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_common_table_expr::clone(Parse_tree_transformer *ptt) const {
  LEX_STRING name_clone{nullptr,0};
  LEX_STRING subq_text_clone{nullptr,0};
  PT_subquery *subq_node_clone = nullptr;
  Create_col_name_list column_names_clone;

  if (clone_lex_string(name_clone, m_name))
    return nullptr;

  if (clone_lex_string(subq_text_clone, m_subq_text))
    return nullptr;

  if (!(subq_node_clone = static_cast<PT_subquery*>(m_subq_node->clone(ptt))))
    return nullptr;

  //clone column_names
  column_names_clone.init(cur_mem_root);
  LEX_CSTRING col_name{nullptr,0};
  for (auto it : m_column_names) {
    if (clone_lex_cstring(col_name, it))
      return nullptr;
    column_names_clone.push_back(col_name);
  }

  PT_common_table_expr *node = NEW_PTN PT_common_table_expr(
    name_clone, subq_text_clone, m_subq_text_offset, 
    subq_node_clone, &column_names_clone, cur_mem_root
  );
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_with_list::clone(Parse_tree_transformer *ptt) const {
  PT_with_list *node = NEW_PTN PT_with_list(cur_mem_root);
  if(!node) {
    return NULL;
  }

  PT_common_table_expr *element_clone = NULL;
  for(auto it : m_elements) {
    element_clone = static_cast<PT_common_table_expr*>(it->clone(ptt));
    if(!element_clone) {
      return NULL;
    }
    node->push_back(element_clone);
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_with_clause::clone(Parse_tree_transformer *ptt) const {
  PT_with_list *l_clone = static_cast<PT_with_list*>(m_list->clone(ptt));
  if(!l_clone) {
    return NULL;
  }

  PT_with_clause *node = NEW_PTN PT_with_clause(l_clone, m_recursive);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_select_item_list::clone(Parse_tree_transformer *ptt) const {
  PT_select_item_list *node = NEW_PTN PT_select_item_list();
  if(!node) {
    return NULL;
  }

  List_iterator_const<Item> it(value);
  const Item *item;
  Item *item_clone;
  while ((item = it++)) {
    if(!(item_clone = static_cast<Item*>(item->clone(ptt))) ) {
      return NULL;
    }
    node->push_back(item_clone);
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_limit_clause::clone(Parse_tree_transformer *ptt) const {
  Limit_options limit_options_clone;
  limit_options_clone.limit = static_cast<Item*>(limit_options.limit->clone(ptt));
  limit_options_clone.opt_offset = nullptr;
  limit_options_clone.is_offset_first = limit_options.is_offset_first;

  if(!limit_options_clone.limit) {
    return nullptr;
  }

  if (limit_options.opt_offset &&
      !(limit_options_clone.opt_offset = 
            static_cast<Item*>(limit_options.opt_offset->clone(ptt))))
    return nullptr;

  PT_limit_clause *node = NEW_PTN PT_limit_clause(limit_options_clone);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}


void *PT_table_factor_table_ident::clone(Parse_tree_transformer *ptt) const {
  Table_ident *table_ident_clone = nullptr;
  List<String> *opt_use_partition_clone = nullptr;
  LEX_CSTRING opt_table_alias_arg_clone{nullptr,0};
  List<Index_hint> *opt_key_definition_clone = nullptr;
  PT_table_factor_table_ident *node = nullptr;

  if( !(table_ident_clone = static_cast<Table_ident*>(table_ident->clone(ptt))) ) {
    return nullptr;
  }

  //clone opt_use_partition
  if (opt_use_partition) {
    if (!(opt_use_partition_clone = 
              clone_String_list(cur_mem_root, opt_use_partition)))
      return nullptr;
  }

  //clone opt_table_alias_arg
  LEX_CSTRING opt_table_alias_str = make_lex_cstring_safe(opt_table_alias);
  if (clone_lex_cstring(opt_table_alias_arg_clone, opt_table_alias_str)) {
    return nullptr;
  }

  //clone opt_key_definition
  if (opt_key_definition) {
    if (!(opt_key_definition_clone =
              clone_list_with_ptt(cur_mem_root, opt_key_definition,ptt)))
      return nullptr;
  }

  //clone PT_table_factor_table_ident
  if(!(node = NEW_PTN PT_table_factor_table_ident(table_ident_clone,
    opt_use_partition_clone, opt_table_alias_arg_clone, opt_key_definition_clone))) {
    return nullptr;
  }
  transform_safe(ptt, node);
  return node;
}


void *PT_table_reference_list_parens::clone(Parse_tree_transformer *ptt) const {
  Mem_root_array_YY<PT_table_reference *> table_list_clone;
  table_list_clone.init(cur_mem_root);
  PT_table_reference *tr_clone = nullptr;;
  PT_table_reference_list_parens *node = nullptr;

  for (auto it : table_list) {
    if(!(tr_clone = static_cast<PT_table_reference*>(it->clone(ptt)))) {
      return nullptr;
    }
    if (table_list_clone.push_back(tr_clone))
      return nullptr;
  }

  if(!(node = NEW_PTN PT_table_reference_list_parens(table_list_clone))) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_derived_table::clone(Parse_tree_transformer *ptt) const {
  PT_derived_table *node = nullptr;
  bool lateral_clone = m_lateral;
  PT_subquery *subquery_clone = nullptr;
  LEX_CSTRING table_alias_clone{nullptr,0};
  Create_col_name_list column_names_clone;

  //clone subquery
  if(!(subquery_clone = static_cast<PT_subquery*>(m_subquery->clone(ptt)))) {
    return nullptr;
  }

  //clone table_alias
  // m_table_alias should be a valid c string
  table_alias_clone.length = my_strlen(m_table_alias);
  table_alias_clone.str = strmake_root(cur_mem_root, m_table_alias, table_alias_clone.length);
  if(!table_alias_clone.str) {
    return nullptr;
  }

  //clone column_names
  column_names_clone.init(cur_mem_root);
  LEX_CSTRING column_name{nullptr,0};
  for (auto it : column_names) {
    if (clone_lex_cstring(column_name, it))
      return nullptr;
    if (column_names_clone.push_back(column_name))
      return nullptr;
  }

  if(!(node = NEW_PTN PT_derived_table(lateral_clone, subquery_clone, table_alias_clone, &column_names_clone))) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_table_factor_joined_table::clone(Parse_tree_transformer *ptt) const {
  PT_joined_table *joined_table_clone = static_cast<PT_joined_table*>(m_joined_table->clone(ptt));
  if(!joined_table_clone) {
    return NULL;
  }

  PT_table_factor_joined_table *node = NEW_PTN PT_table_factor_joined_table(joined_table_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}


void *PT_cross_join::clone(Parse_tree_transformer *ptt) const {
  PT_table_reference *tab1_node_clone = static_cast<PT_table_reference*>(tab1_node->clone(ptt));
  PT_table_reference *tab2_node_clone = static_cast<PT_table_reference*>(tab2_node->clone(ptt));
  if(!tab1_node_clone || !tab2_node_clone) {
    return NULL;
  }

  PT_cross_join *node = NEW_PTN PT_cross_join(tab1_node_clone,
      join_pos, m_type, tab2_node_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_joined_table_on::clone(Parse_tree_transformer *ptt) const {
  PT_table_reference *tab1_node_clone = static_cast<PT_table_reference*>(tab1_node->clone(ptt));
  PT_table_reference *tab2_node_clone = static_cast<PT_table_reference*>(tab2_node->clone(ptt));
  Item *on_clone = static_cast<Item*>(on->clone(ptt));
  if(!tab1_node_clone || !tab2_node_clone || !on_clone) {
    return NULL;
  }

  PT_joined_table_on *node = NEW_PTN PT_joined_table_on(tab1_node_clone,
      join_pos, m_type, tab2_node_clone, on_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_joined_table_using::clone(Parse_tree_transformer *ptt) const {
  PT_table_reference *tab1_node_clone = static_cast<PT_table_reference*>(tab1_node->clone(ptt));
  PT_table_reference *tab2_node_clone = static_cast<PT_table_reference*>(tab2_node->clone(ptt));
  List<String> *using_fields_clone = NULL;
  if(!tab1_node_clone || !tab2_node_clone) {
    return NULL;
  }

  if(using_fields) {
    if (!(using_fields_clone = clone_String_list(cur_mem_root, using_fields)))
      return nullptr;
  }

  PT_joined_table_using *node = NEW_PTN PT_joined_table_using(tab1_node_clone,
      join_pos, m_type, tab2_node_clone, using_fields_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_hierarchical::clone(Parse_tree_transformer *ptt) const {
  Item *recurs_begin_clone = NULL;
  Item *recurs_end_clone = NULL;
  if(!(recurs_begin_clone = static_cast<Item*>(m_recurs_begin->clone(ptt))) ||
      !(recurs_end_clone = static_cast<Item*>(m_recurs_end->clone(ptt)))) {
    return NULL;
  }

  PT_hierarchical *node = NEW_PTN PT_hierarchical(recurs_begin_clone, recurs_end_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_group::clone(Parse_tree_transformer *ptt) const {
  PT_group *node = NULL;
  PT_order_list *group_list_clone = NULL;

  if(!(group_list_clone = static_cast<PT_order_list*>(group_list->clone(ptt)))) {
    return NULL;
  }

  if(!(node = NEW_PTN PT_group(group_list_clone, olap))) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}


void *PT_order::clone(Parse_tree_transformer *ptt) const {
  PT_order *node = NULL;
  PT_order_list *order_list_clone = NULL;

  if(!(order_list_clone = static_cast<PT_order_list*>(order_list->clone(ptt)))) {
    return NULL;
  }

  if(!(node = NEW_PTN PT_order(order_list_clone))) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

#if 0
/* 
 * CLONE NOT USED,
 * ref to sql_yacc.yy: option_value_following_option_type
 * and option_value_no_option_type.
 */
void *PT_internal_variable_name_1d::clone(Parse_tree_transformer *ptt) const {
  //clone ident
  LEX_CSTRING ident_clone{nullptr,0};
  ident_clone.str = strmake_root(cur_mem_root, ident.str, ident.length);
  ident_clone.length = ident.length;
  if(!ident_clone.str) {
    return NULL;
  }

  PT_internal_variable_name_1d *node = NEW_PTN PT_internal_variable_name_1d(ident_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_internal_variable_name_2d::clone(Parse_tree_transformer *ptt) const {
  //clone ident1
  LEX_CSTRING ident1_clone{nullptr,0}, ident2_clone{nullptr,0};
  ident1_clone.str = strmake_root(cur_mem_root, ident1.str, ident1.length);
  ident1_clone.length = ident1.length;
  if(!ident1_clone.str) {
    return NULL;
  }

  //clone ident2
  ident2_clone.str = strmake_root(cur_mem_root, ident2.str, ident2.length);
  ident2_clone.length = ident2.length;
  if(!ident2_clone.str) {
    return NULL;
  }

  PT_internal_variable_name_2d *node = NEW_PTN PT_internal_variable_name_2d(pos, ident1_clone, ident2_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_internal_variable_name_default::clone(Parse_tree_transformer *ptt) const {
  LEX_STRING ident_clone{nullptr,0};
  ident_clone.str = strmake_root(cur_mem_root, ident.str, ident.length);
  ident_clone.length = ident.length;
  if(!ident_clone.str) {
    return NULL;
  }

  PT_internal_variable_name_default *node = NEW_PTN PT_internal_variable_name_default(ident_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}
#endif

void *PT_item_list::clone(Parse_tree_transformer *ptt) const {
  PT_item_list *node = NEW_PTN PT_item_list();
  if (!node) return nullptr;
  List_iterator_const<Item> li(value);
  const Item *item_tmp;
  Item *item_clone;
  while((item_tmp = li++)) {
    if (!(item_clone = static_cast<Item *>(item_tmp->clone(ptt))))
      return nullptr;
    node->push_back(item_clone);
  }
  transform_safe(ptt, node);
  return node;
}

void *PT_border::clone(Parse_tree_transformer *ptt) const {
  PT_border *node = nullptr;
  Item *value_clone = nullptr;
  if (m_value && !(value_clone = static_cast<Item *>(m_value->clone(ptt))))
    return nullptr;
  if (m_date_time)
    node = NEW_PTN PT_border(m_border_type, value_clone, m_int_type);
  else
    node = NEW_PTN PT_border(m_border_type, value_clone);
  if (!node) return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PT_exclusion::clone(Parse_tree_transformer *ptt) const {
  PT_exclusion *node = NEW_PTN PT_exclusion(m_exclusion);
  if (!node) return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PT_frame::clone(Parse_tree_transformer *ptt) const {
  PT_frame *node = nullptr;
  PT_border *from_clone = nullptr,
            *to_clone = nullptr;
  PT_exclusion *exclusion_clone = nullptr;
  if (m_exclusion && !(exclusion_clone = static_cast<PT_exclusion *>
                        (m_exclusion->clone(ptt))))
    return nullptr;
  if (!(from_clone = static_cast<PT_border *>(m_from->clone(ptt)))
      || !(to_clone = static_cast<PT_border *>(m_to->clone(ptt))))
    return nullptr;
  // from_to_clone is local
  PT_borders from_to_clone(from_clone, to_clone);
  if (!(node = NEW_PTN PT_frame(m_unit, &from_to_clone, exclusion_clone)))
    return nullptr;
  node->m_originally_absent = m_originally_absent;
  transform_safe(ptt, node);
  return node;
}

void *PT_window::clone(Parse_tree_transformer *ptt) const {
  PT_window *node = nullptr;
  PT_order_list *partition_by_clone = nullptr, // nullable
                *order_by_clone = nullptr; // nullable
  PT_frame *frame_clone = nullptr; //not nullable
  Item_string *name_clone = nullptr, // nullable
              *inherit_clone = nullptr; // nullable

  if (m_partition_by && !(partition_by_clone = static_cast<PT_order_list *>
                            (m_partition_by->clone(ptt))))
    return nullptr;
  if (m_order_by && !(order_by_clone = static_cast<PT_order_list *>
                            (m_order_by->clone(ptt))))
    return nullptr;
  if (m_frame && !(frame_clone = static_cast<PT_frame *>(m_frame->clone(ptt))))
    return nullptr;
  if (m_name && !(name_clone = static_cast<Item_string *>
                            (m_name->clone(ptt))))
    return nullptr;
  if (m_inherit_from && !(inherit_clone = static_cast<Item_string *>
                            (m_inherit_from->clone(ptt))))
    return nullptr;
  
  if (frame_clone) {
    if (!(node = NEW_PTN PT_window(partition_by_clone,
                                   order_by_clone,
                                   frame_clone,
                                   inherit_clone)))
      return nullptr;
    if (name_clone)
      node->set_name(name_clone);
  }
  else {
    DBUG_ASSERT(name_clone);
    if (!(node = NEW_PTN PT_window(name_clone)))
      return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_window_list::clone(Parse_tree_transformer *ptt) const {
  PT_window_list *node = NEW_PTN PT_window_list();
  if (!node) return nullptr;
  List_iterator_const<Window> li(m_windows);
  const PT_window *win_tmp;
  PT_window *win_clone;
  // This is assured to be PT_window
  while((win_tmp = (const PT_window *)li++)) {
    if (!(win_clone = static_cast<PT_window *>(win_tmp->clone(ptt))))
      return nullptr;
    node->push_back(win_clone);
  }
  transform_safe(ptt, node);
  return node;
}

void *PT_query_specification::clone(Parse_tree_transformer *ptt) const {
  PT_hint_list *opt_hints_clone = nullptr;
  Query_options options_clone = options;
  PT_item_list *item_list_clone = nullptr;
  PT_into_destination *opt_into1_clone = nullptr;
  Mem_root_array_YY<PT_table_reference *> from_clause_clone;
  Item *opt_where_clause_clone = nullptr;
  PT_hierarchical *hierarchical_clause_clone = nullptr;
  PT_group *opt_group_clause_clone = nullptr;
  Item *opt_having_clause_clone = nullptr;
  PT_window_list *opt_window_clause_clone = nullptr;
  PT_table_reference *fc_clone = nullptr;

  if (opt_hints &&
      !(opt_hints_clone = static_cast<PT_hint_list*>(opt_hints->clone(ptt))))
    return nullptr;
  if (!(item_list_clone = static_cast<PT_item_list*>(item_list->clone(ptt))))
    return nullptr;
  if (opt_into1 &&
      !(opt_into1_clone = static_cast<PT_into_destination*>(opt_into1->clone(ptt))))
    return nullptr;
  if (opt_where_clause &&
      !(opt_where_clause_clone = static_cast<Item*>(opt_where_clause->clone(ptt))))
    return nullptr;
  if (opt_hierarchical_clause && 
      !( hierarchical_clause_clone = static_cast<PT_hierarchical*>(opt_hierarchical_clause->clone(ptt))))
    return nullptr;
  if (opt_group_clause &&
      !(opt_group_clause_clone = static_cast<PT_group*>(opt_group_clause->clone(ptt))))
    return nullptr;
  if (opt_having_clause &&
      !(opt_having_clause_clone = static_cast<Item*>(opt_having_clause->clone(ptt))))
    return nullptr;
  if (opt_window_clause &&
      !(opt_window_clause_clone = static_cast<PT_window_list*>(opt_window_clause->clone(ptt))))
    return nullptr;

  from_clause_clone.init(cur_mem_root);
  for (auto it : from_clause) {
    if(!(fc_clone = static_cast<PT_table_reference*>(it->clone(ptt))) ){
      return NULL;
    }
    from_clause_clone.push_back(fc_clone);
  }

  PT_query_specification *node = NEW_PTN PT_query_specification(
    opt_hints_clone, options_clone, item_list_clone, opt_into1_clone,
    from_clause_clone, opt_where_clause_clone, hierarchical_clause_clone, 
    opt_group_clause_clone, opt_having_clause_clone, opt_window_clause_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_query_expression::clone(Parse_tree_transformer *ptt) const {
  PT_query_expression_body *m_body_clone = NULL;
  PT_order *m_order_clone = NULL;
  PT_limit_clause *m_limit_clone = NULL;
  PT_locking_clause_list *m_locking_clauses_clone = NULL;
  PT_with_clause *m_with_clause_clone = NULL;

  if(m_body && !(m_body_clone = static_cast<PT_query_expression_body*>(m_body->clone(ptt))) ) {
    return NULL;
  }

  if(m_order && !(m_order_clone = static_cast<PT_order*>(m_order->clone(ptt)))) {
    return NULL;
  }

  if(m_limit && !(m_limit_clone = static_cast<PT_limit_clause*>(m_limit->clone(ptt)))) {
    return NULL;
  }

  if(m_locking_clauses && !(m_locking_clauses_clone = 
      static_cast<PT_locking_clause_list*>(m_locking_clauses->clone(ptt)))) {
    return NULL;
  }

  if(m_with_clause && !(m_with_clause_clone = static_cast<PT_with_clause*>(m_with_clause->clone(ptt)))) {
    return NULL;
  }

  PT_query_expression *node = NEW_PTN PT_query_expression(m_with_clause_clone, m_body_clone, m_order_clone,
      m_limit_clone, m_locking_clauses_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}


void *PT_subquery::clone(Parse_tree_transformer *ptt) const {
  PT_query_expression *query_expression_clone = static_cast<PT_query_expression*>(qe->clone(ptt));
  if(!query_expression_clone) {
    return NULL;
  }

  PT_subquery *node = NEW_PTN PT_subquery(pos, query_expression_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}


void *PT_query_expression_body_primary::clone(Parse_tree_transformer *ptt) const {
  PT_query_primary *query_primary_clone = static_cast<PT_query_primary*>(m_query_primary->clone(ptt));
  if(!query_primary_clone) {
    return NULL;
  }

  PT_query_expression_body_primary *node = NEW_PTN PT_query_expression_body_primary(query_primary_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_union::clone(Parse_tree_transformer *ptt) const {
  PT_query_expression *lhs_clone = static_cast<PT_query_expression*>(m_lhs->clone(ptt));
  PT_query_primary *rhs_clone = static_cast<PT_query_primary*>(m_rhs->clone(ptt));
  if(!lhs_clone || !rhs_clone) {
    return NULL;
  }

  PT_union *node = NEW_PTN PT_union(lhs_clone, m_lhs_pos, m_is_distinct, rhs_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_nested_query_expression::clone(Parse_tree_transformer *ptt) const {
  PT_query_expression *qe_clone = static_cast<PT_query_expression*>(m_qe->clone(ptt));
  if(!qe_clone) {
    return NULL;
  }

  PT_nested_query_expression *node = NEW_PTN PT_nested_query_expression(qe_clone);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

/*===============================items==================================*/
void *Item_field::clone(Parse_tree_transformer *ptt) const {
  //before itemize, so there should be no context
  DBUG_ASSERT(!is_contextualized());
  DBUG_ASSERT(orig_field_name && orig_field_name[0]);
  const char *db_name_clone = nullptr;
  const char *tbl_name_clone = nullptr;
  const char *fld_name_clone = strmake_root(cur_mem_root, orig_field_name, my_strlen(orig_field_name));

  if(!fld_name_clone)
    return nullptr;
  if (orig_db_name &&
      !(db_name_clone = strmake_root(cur_mem_root, orig_db_name, my_strlen(orig_db_name))))
    return nullptr;
  if (orig_table_name &&
      !(tbl_name_clone = strmake_root(cur_mem_root, orig_table_name, my_strlen(orig_table_name))))
    return nullptr;

  Item_field *node = NEW_PTN Item_field(POS(), db_name_clone, tbl_name_clone, fld_name_clone);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_null::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_null *node = nullptr;
  if (need_pos)
    node = NEW_PTN Item_null(POS());
  else
    node = NEW_PTN Item_null(item_name);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_int::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_int *node = nullptr;
  if (need_pos)
    node = NEW_PTN Item_int(POS(),item_name,value,max_length);
  else
    node = NEW_PTN Item_int(this);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_uint::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_uint *node = nullptr;
  if (need_pos)
    node = NEW_PTN Item_uint(POS(),item_name.ptr(),item_name.length());
  else
    node = NEW_PTN Item_uint(item_name, value, max_length);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_decimal::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_decimal *node = nullptr;
  if (need_pos) {
    const CHARSET_INFO *cs = current_thd->m_parser_state->m_lip.query_charset;
    node = NEW_PTN Item_decimal(POS(),item_name.ptr(),
                                item_name.length(), cs);
  }
  else
    node = NEW_PTN Item_decimal(item_name, &decimal_value, decimals, max_length);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_float::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_float *node = nullptr;
  if (need_pos)
    node = NEW_PTN Item_float(POS(),item_name, value, decimals, max_length);
  else
    node = NEW_PTN Item_float(item_name, value, decimals, max_length);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

// For const values, we don't clone the strings, for they can be shared.
// @NOTE Not verified.
void *Item_string::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_string *node = nullptr;
  if (need_pos)
    node = NEW_PTN Item_string(POS(),
                               item_name,
                               str_value.ptr(),
                               str_value.length(),
                               collation.collation,
                               collation.derivation,
                               collation.repertoire);
  else
    node = NEW_PTN Item_string(item_name,
                               str_value.ptr(),
                               str_value.length(),
                               collation.collation,
                               collation.derivation,
                               collation.repertoire);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_hex_string::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_hex_string *node = nullptr;
  if (need_pos) {
    node = NEW_PTN Item_hex_string(POS(),this);
  }
  else
    node = NEW_PTN Item_hex_string(this);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_bin_string::clone(Parse_tree_transformer *ptt) const {
  bool need_pos = get_is_parser_item();
  Item_bin_string *node = nullptr;
  if (need_pos)
    node = NEW_PTN Item_bin_string(POS(),this);
  else
    node = NEW_PTN Item_bin_string(this);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_insert_value::clone(Parse_tree_transformer *ptt) const {
  Item *arg_clone = nullptr;
  if (arg && !(arg_clone = static_cast<Item *>(arg->clone(ptt))))
    return nullptr;
  Item_insert_value *node = NEW_PTN Item_insert_value(POS(),arg_clone);
  if (!node)
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *Item_default_value::clone(Parse_tree_transformer *ptt) const {
  Item *arg_clone = nullptr;
  if (arg && !(arg_clone = static_cast<Item *>(arg->clone(ptt))))
    return nullptr;
  Item_default_value *node = NEW_PTN Item_default_value(POS(),arg_clone);
  if (!node)
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

/* ====================================others================================== */

void *Table_ident::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(!sel);
  DBUG_ASSERT(!table_function);

  Table_ident *node = NEW_PTN Table_ident(db, table);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Index_hint::clone(Parse_tree_transformer *) const {
  Index_hint *node = NEW_PTN Index_hint(key_name.str, key_name.length);
  if(!node) {
    return nullptr;
  }
  node->type = type;

  return node;
}


/* =============================== item_funcs =================================== */

#define ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 0); \
 \
  Item_class *node = nullptr; \
 \
  if(!(node = NEW_PTN Item_class(POS()))) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 1); \
  DBUG_ASSERT(args[0]); \
 \
  Item_class *node = nullptr; \
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt)); \
  if(!a_clone) { \
    return nullptr; \
  } \
 \
  if(!(node = NEW_PTN Item_class(POS(), a_clone))) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 2); \
  DBUG_ASSERT(args[0] && args[1]); \
 \
  Item_class *node = nullptr; \
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt)); \
  Item *b_clone = static_cast<Item*>(args[1]->clone(ptt)); \
  if(!a_clone || !b_clone) { \
    return nullptr; \
  } \
 \
  if(!(node = NEW_PTN Item_class(POS(), a_clone, b_clone))) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_FUNC_TEMPLATE_POS_3_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 3); \
  DBUG_ASSERT(args[0] && args[1] && args[2]); \
 \
  Item_class *node = nullptr; \
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt)); \
  Item *b_clone = static_cast<Item*>(args[1]->clone(ptt)); \
  Item *c_clone = static_cast<Item*>(args[2]->clone(ptt)); \
  if(!a_clone || !b_clone || !c_clone) { \
    return nullptr; \
  } \
 \
  if(!(node = NEW_PTN Item_class(POS(), a_clone, b_clone, c_clone))) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_FUNC_TEMPLATE_POS_4_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 4); \
  DBUG_ASSERT(args[0] && args[1] && args[2] && args[3]); \
 \
  Item_class *node = nullptr; \
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt)); \
  Item *b_clone = static_cast<Item*>(args[1]->clone(ptt)); \
  Item *c_clone = static_cast<Item*>(args[2]->clone(ptt)); \
  Item *d_clone = static_cast<Item*>(args[3]->clone(ptt)); \
  if(!a_clone || !b_clone || !c_clone || !d_clone) { \
    return nullptr; \
  } \
 \
  if(!(node = NEW_PTN Item_class(POS(), a_clone, b_clone, c_clone, d_clone))) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)


#define ITEM_FUNC_TEMPLATE_POS_ITEMLIST(Item_class, ptt) \
do { \
  Item_class *node = nullptr; \
  PT_item_list *opt_list_clone = nullptr; \
 \
  if (arg_count > 0 && \
      !(opt_list_clone = clone_func_args_as_item_list(args, arg_count, ptt))) \
    return nullptr; \
 \
  if(!(node = NEW_PTN Item_class(POS(), opt_list_clone))) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

void *Item_func_plus::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_plus, ptt);
}

void *Item_func_minus::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_minus, ptt);
}

void *Item_func_mul::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_mul, ptt);
}

void *Item_func_div::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_div, ptt);
}

void *Item_func_int_div::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_int_div, ptt);
}

void *Item_func_mod::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_mod, ptt);
}

void *Item_func_neg::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_neg, ptt);
}

void *Item_func_round::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 2);
  DBUG_ASSERT(args[0] && args[1]);

  Item_func_round *node = NULL;
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  Item *b_clone = static_cast<Item*>(args[1]->clone(ptt));
  if(!a_clone || !b_clone) {
    return NULL;
  }

  if(!(node = NEW_PTN Item_func_round(POS(), a_clone, b_clone, truncate))) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_locate::clone(Parse_tree_transformer *ptt) const {
  if(arg_count == 2) {
    ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_locate, ptt);
  } else if(arg_count == 3) {
    ITEM_FUNC_TEMPLATE_POS_3_ITEM(Item_func_locate, ptt);
  } else {
    return NULL;
  }
}

void *Item_func_ascii::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_ascii, ptt);
}

void *Item_func_bit_or::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_bit_or, ptt);
}

void *Item_func_bit_and::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_bit_and, ptt);
}

void *Item_func_bit_xor::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_bit_xor, ptt);
}

void *Item_func_shift_left::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_shift_left, ptt);
}

void *Item_func_shift_right::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_shift_right, ptt);
}

void *Item_func_bit_neg::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_bit_neg, ptt);
}

void *Item_func_match::clone(Parse_tree_transformer *ptt) const {
  PT_item_list *a_clone = nullptr;
  if(!(a_clone = clone_func_args_as_item_list(args, arg_count, ptt))) {
    return nullptr;
  }

  Item *against_clone = static_cast<Item*>(against->clone(ptt));
  if(!against_clone) {
    return nullptr;
  }

  Item_func_match *node = NEW_PTN Item_func_match(POS(), a_clone, against_clone, flags);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_row_count::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_func_row_count, ptt);
}

/* ============================= item_str_func ============================== */

void *Item_func_concat::clone(Parse_tree_transformer *ptt) const {
  if(arg_count == 2) {
    ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_concat, ptt);
  } else {
    ITEM_FUNC_TEMPLATE_POS_ITEMLIST(Item_func_concat, ptt);
  }
}

void *Item_func_reverse::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_reverse, ptt);
}

void *Item_func_replace::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_3_ITEM(Item_func_replace, ptt);
}

void *Item_func_insert::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_4_ITEM(Item_func_insert, ptt);
}

void *Item_func_left::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_left, ptt);
}

void *Item_func_right::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_right, ptt);
}

void *Item_func_trim::clone(Parse_tree_transformer *ptt) const {
  Item *a_clone = NULL;
  Item *b_clone = NULL;
  Item_func_trim *node = NULL;

  if(arg_count == 1) {
    if(!(a_clone = static_cast<Item*>(args[0]->clone(ptt))) || 
        !(node = NEW_PTN Item_func_trim(POS(), a_clone, m_trim_mode))) {
      return NULL;
    }
  } else if(arg_count == 2) {
    if(!(a_clone = static_cast<Item*>(args[0]->clone(ptt))) || 
        !(b_clone = static_cast<Item*>(args[1]->clone(ptt))) ||
        !(node = NEW_PTN Item_func_trim(POS(), a_clone, b_clone, m_trim_mode))) {
      return NULL;
    }
  } else {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_database::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_func_database, ptt);
}

void *Item_func_user::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_func_user, ptt);
}

void *Item_func_current_user::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_func_current_user, ptt);
}

void *Item_func_soundex::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_soundex, ptt);
}

void *Item_func_format::clone(Parse_tree_transformer *ptt) const {
  if(arg_count == 2) {
    ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_format, ptt);
  } else if(arg_count == 3) {
    ITEM_FUNC_TEMPLATE_POS_3_ITEM(Item_func_format, ptt);
  } else {
    return NULL;
  }
}

void *Item_func_char::clone(Parse_tree_transformer *ptt) const {
  PT_item_list *opt_list_clone = nullptr;
  if(!(opt_list_clone = clone_func_args_as_item_list(args, arg_count, ptt))) {
    return nullptr;
  }

  Item_func_char *node = NEW_PTN Item_func_char(POS(), opt_list_clone, collation.collation);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_repeat::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_repeat, ptt);
}

void *Item_typecast_char::clone(Parse_tree_transformer *ptt) const {
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(!a_clone) {
    return NULL;
  }
  Item_typecast_char *node = NEW_PTN Item_typecast_char(POS(), a_clone, cast_length, cast_cs);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_conv_charset::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(!args[0]->fixed);

  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(!a_clone) {
    return NULL;
  }
  Item_func_conv_charset *node = NEW_PTN Item_func_conv_charset(POS(), a_clone, conv_charset);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_set_collation::clone(Parse_tree_transformer *ptt) const {
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(!a_clone) {
    return NULL;
  }
  Item_func_set_collation *node = NEW_PTN Item_func_set_collation(POS(), a_clone, collation_string);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_charset::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_charset, ptt);
}

void *Item_func_collation::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_collation, ptt);
}

void *Item_func_weight_string::clone(Parse_tree_transformer *ptt) const {
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(!a_clone) {
    return NULL;
  }
  Item_func_weight_string *node = NEW_PTN Item_func_weight_string(POS(), a_clone, result_length, num_codepoints, flags, as_binary);
  if(!node) {
    return NULL;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_substr::clone(Parse_tree_transformer *ptt) const {
  if(arg_count == 2) {
    ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_substr, ptt);
  } else if(arg_count == 3) {
    ITEM_FUNC_TEMPLATE_POS_3_ITEM(Item_func_substr, ptt);
  } else {
    return NULL;
  }
}

/* ==================================Item_cmpfunc================================= */
void *Item_func_eq::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_eq, ptt);
}

void *Item_func_isnull::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_isnull, ptt);
}

void *Item_func_isnotnull::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_isnotnull, ptt);
}

void *Item_func_in::clone(Parse_tree_transformer *ptt) const {
  Item_func_in *node = nullptr;
  PT_item_list *list_arg = nullptr;
  if (!(list_arg = clone_func_args_as_item_list(args, arg_count, ptt)))
    return nullptr;
  if (!(node = NEW_PTN Item_func_in(POS(), list_arg, negated)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_between::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 3);
  Item_func_between *node = nullptr;
  Item *a_clone = nullptr;
  Item *b_clone = nullptr;
  Item *c_clone = nullptr;
  if (args[0] && !(a_clone = static_cast<Item *>(args[0]->clone(ptt))))
    return nullptr;
  if (args[1] && !(b_clone = static_cast<Item *>(args[1]->clone(ptt))))
    return nullptr;
  if (args[2] && !(c_clone = static_cast<Item *>(args[2]->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN Item_func_between(POS(), a_clone, b_clone,
                                         c_clone, negated)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_like::clone(Parse_tree_transformer *ptt) const {
  Item_func_like *node = nullptr;
  Item *a_clone = nullptr;
  Item *b_clone = nullptr;
  Item *escape_clone = nullptr;
  if (args[0] && !(a_clone = static_cast<Item *>(args[0]->clone(ptt))))
    return nullptr;
  if (args[1] && !(b_clone = static_cast<Item *>(args[1]->clone(ptt))))
    return nullptr;
  if (escape_item &&
      !(escape_clone = static_cast<Item *>(escape_item->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN Item_func_like(POS(), a_clone, b_clone,
                                      escape_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_not::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_not, ptt);
}

void *Item_func_false::clone(Parse_tree_transformer *ptt) const {
  Item_func_false *node = nullptr;
  if (!(node = NEW_PTN Item_func_false(POS())))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *Item_func_true::clone(Parse_tree_transformer *ptt) const {
  Item_func_true *node = nullptr;
  if (!(node = NEW_PTN Item_func_true(POS())))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *Item_func_xor::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_xor, ptt);
}

/* ==================================Item_sum==================================== */
#define ITEM_SUM_FUNC_TEMPLATE_0_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 0); \
 \
  Item_class *node = nullptr; \
  PT_window *win_clone = nullptr; \
  if(m_window && !(win_clone = static_cast<PT_window*>(m_window->clone(ptt)))) \
    return nullptr; \
 \
  node = NEW_PTN Item_class(POS(), win_clone); \
  if(!node) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 1 && args[0]); \
 \
  Item_class *node = nullptr; \
  PT_window * win_clone = nullptr; \
  if(m_window && !(win_clone = static_cast<PT_window*>(m_window->clone(ptt)))) \
    return nullptr; \
 \
  Item *item_clone_1 = nullptr; \
  if(!(item_clone_1 = static_cast<Item*>(args[0]->clone(ptt)))) \
    return nullptr; \
 \
  node = NEW_PTN Item_class(POS(), item_clone_1, win_clone); \
  if(!node) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_SUM_FUNC_TEMPLATE_1_ITEM_1_PARAM(Item_class, ptt, param) \
do { \
  DBUG_ASSERT(arg_count == 1 && args[0]); \
 \
  Item_class *node = nullptr; \
  PT_window * win_clone = nullptr; \
  if(m_window && !(win_clone = static_cast<PT_window*>(m_window->clone(ptt)))) \
    return nullptr; \
 \
  Item *item_clone_1 = nullptr; \
  if(!(item_clone_1 = static_cast<Item*>(args[0]->clone(ptt)))) \
    return nullptr; \
 \
  node = NEW_PTN Item_class(POS(), item_clone_1, param, win_clone); \
  if(!node) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_SUM_FUNC_TEMPLATE_2_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 2); \
  DBUG_ASSERT(args[0]); \
  DBUG_ASSERT(args[1]); \
 \
  Item_class *node = nullptr; \
  PT_window * win_clone = nullptr; \
  if(m_window && !(win_clone = static_cast<PT_window*>(m_window->clone(ptt)))) \
    return nullptr; \
 \
  Item *item_clone_1 = nullptr; \
  if(!(item_clone_1 = static_cast<Item*>(args[0]->clone(ptt)))) \
    return nullptr; \
 \
  Item *item_clone_2 = nullptr; \
  if(!(item_clone_2 = static_cast<Item*>(args[1]->clone(ptt)))) \
    return nullptr; \
 \
  node = NEW_PTN Item_class(POS(), item_clone_1, item_clone_2, win_clone); \
  if(!node) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

#define ITEM_SUM_FUNC_TEMPLATE_ITEM_LIST(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 0 || args != nullptr); \
 \
  Item_class *node = nullptr; \
  PT_window * win_clone = nullptr; \
  if(m_window && !(win_clone = static_cast<PT_window*>(m_window->clone(ptt)))) \
    return nullptr; \
 \
  PT_item_list *list_clone = clone_func_args_as_item_list(args, arg_count, ptt); \
  if (list_clone == nullptr) \
    return nullptr; \
  node = NEW_PTN Item_class(POS(), list_clone, win_clone); \
  if(!node) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

void *Item_sum_sum::clone(Parse_tree_transformer *ptt) const {
  bool distinct = has_with_distinct();
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM_1_PARAM(Item_sum_sum, ptt, distinct);
}

void *Item_sum_avg::clone(Parse_tree_transformer *ptt) const {
  bool distinct = has_with_distinct();
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM_1_PARAM(Item_sum_avg, ptt, distinct);
}

void *Item_sum_count::clone(Parse_tree_transformer *ptt) const {
  bool distinct = has_with_distinct();
  if (distinct) {
    ITEM_SUM_FUNC_TEMPLATE_ITEM_LIST(Item_sum_count, ptt);
  }
  else {
    ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_sum_count, ptt);
  }
}

void *Item_sum_std::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM_1_PARAM(Item_sum_std, ptt, sample);
}

void *Item_sum_variance::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM_1_PARAM(Item_sum_variance, ptt, sample);
}

void *Item_sum_min::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_sum_min, ptt);
}

void *Item_sum_max::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_sum_max, ptt);
}

void *Item_sum_and::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_sum_and, ptt);
}

void *Item_sum_or::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_sum_or, ptt);
}

void *Item_sum_xor::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_sum_xor, ptt);
}

void *Item_func_group_concat::clone(Parse_tree_transformer *ptt) const {
  Item_func_group_concat *node = nullptr;
  PT_window *window_clone = nullptr;
  PT_item_list *item_list_clone = nullptr;
  PT_gorder_list *order_list_clone = nullptr;
  if (m_window && !(window_clone = static_cast<PT_window*>(m_window->clone(ptt))))
    return nullptr;
  if (!(item_list_clone = clone_func_args_as_item_list(args, arg_count_field, ptt)))
    return nullptr;
  if (arg_count_order > 0) {
    if (!(order_list_clone = NEW_PTN PT_gorder_list()))
      return nullptr;
    PT_order_expr *ord_clone = nullptr;
    Item *item_clone = nullptr;
    for (const ORDER *ord = order_array.begin();
         ord;ord = ord->next) {
      if (!(item_clone = static_cast<Item *>(ord->item_ptr->clone(ptt))))
        return nullptr;
      if (!(ord_clone = NEW_PTN PT_order_expr(item_clone, ord->direction)))
        return nullptr;
      order_list_clone->push_back(ord_clone);
    }
  }
  if (!(node = NEW_PTN Item_func_group_concat(POS(), distinct,
                                              item_list_clone,
                                              order_list_clone,
                                              separator,
                                              window_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_listagg::clone(Parse_tree_transformer *ptt) const {
  Item_func_listagg *node = nullptr;
  PT_window *window_clone = nullptr;
  PT_item_list *item_list_clone = nullptr;
  PT_gorder_list *order_list_clone = nullptr;
  String *separator_clone = nullptr;
  if (m_window && !(window_clone = static_cast<PT_window*>(m_window->clone(ptt))))
    return nullptr;
  if (!(item_list_clone = clone_func_args_as_item_list(args, arg_count_field, ptt)))
    return nullptr;
  if (arg_count_order > 0) {
    if (!(order_list_clone = NEW_PTN PT_gorder_list()))
      return nullptr;
    PT_order_expr *ord_clone = nullptr;
    Item *item_clone = nullptr;
    for (const ORDER *ord = order_array.begin();
         ord;ord = ord->next){
      if (!(item_clone = static_cast<Item *>(ord->item_ptr->clone(ptt))))
        return nullptr;
      if (!(ord_clone = NEW_PTN PT_order_expr(item_clone, ord->direction)))
        return nullptr;
      order_list_clone->push_back(ord_clone);
    }
  }
  if (!(separator_clone = clone_String(separator)))
    return nullptr;
  if (!(node = NEW_PTN Item_func_listagg(POS(),
                                         item_list_clone,
                                         separator_clone,
                                         order_list_clone,
                                         window_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_row_number::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_0_ITEM(Item_row_number, ptt);
}

void *Item_rank::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 0);

  Item_rank *node = nullptr;
  PT_window *win_clone = nullptr;
  if(m_window && !(win_clone = static_cast<PT_window*>(m_window->clone(ptt))))
    return nullptr;

  node = NEW_PTN Item_rank(POS(), m_dense, win_clone);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}
void *Item_cume_dist::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_0_ITEM(Item_cume_dist, ptt);
}

void *Item_percent_rank::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_0_ITEM(Item_percent_rank, ptt);
}

void *Item_ntile::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_ntile, ptt);
}

void *Item_lead_lag::clone(Parse_tree_transformer *ptt) const {
  Item_lead_lag *node = nullptr;
  PT_window *window_clone = nullptr;
  PT_item_list *item_list_clone = nullptr;
  if (m_window && !(window_clone = static_cast<PT_window*>(m_window->clone(ptt))))
    return nullptr;
  if (!(item_list_clone = clone_func_args_as_item_list(args, arg_count, ptt)))
    return nullptr;
  if (!(node = NEW_PTN Item_lead_lag(POS(), m_is_lead,
                                     item_list_clone,
                                     m_null_treatment,
                                     window_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_first_last_value::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 1);
  DBUG_ASSERT(args[0]);
  Item_first_last_value *node = nullptr;
  PT_window *window_clone = nullptr;
  Item *item_clone = nullptr;
  if (m_window && !(window_clone = static_cast<PT_window*>(m_window->clone(ptt))))
    return nullptr;
  if(!(item_clone = static_cast<Item*>(args[0]->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN Item_first_last_value(POS(), m_is_first,
                                             item_clone,
                                             m_null_treatment,
                                             window_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_nth_value::clone(Parse_tree_transformer *ptt) const {
  Item_nth_value *node = nullptr;
  PT_window *window_clone = nullptr;
  PT_item_list *item_list_clone = nullptr;
  if (m_window && !(window_clone = static_cast<PT_window*>(m_window->clone(ptt))))
    return nullptr;
  if (!(item_list_clone = clone_func_args_as_item_list(args, arg_count, ptt)))
    return nullptr;
  if (!(node = NEW_PTN Item_nth_value(POS(),
                                     item_list_clone,
                                     m_from_last,
                                     m_null_treatment,
                                     window_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_grouping::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_ITEMLIST(Item_func_grouping, ptt);
}

/* ==============================JSON func================================= */
void *Item_sum_json_array::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_1_ITEM(Item_sum_json_array, ptt);
}

void *Item_sum_json_object::clone(Parse_tree_transformer *ptt) const {
  ITEM_SUM_FUNC_TEMPLATE_2_ITEM(Item_sum_json_object, ptt);
}

/* ==============================parse_tree_hints================================= */
void *PT_qb_level_hint::clone(Parse_tree_transformer *ptt) const {
  PT_qb_level_hint *node;
  LEX_CSTRING qb_name_clone{nullptr,0};
  Hint_param_table_list table_list_clone;
  if (qb_name.length > 0) {
    qb_name_clone.str = current_thd->strmake(qb_name.str,qb_name.length);
    qb_name_clone.length = qb_name.length;
  }
  if (with_table_list) {
    table_list_clone.init(cur_mem_root);
    for (const Hint_param_table &table: table_list) {
      Hint_param_table table_clone{{nullptr,0},{nullptr,0}};
      if (table.table.length > 0) {
        table_clone.table.str =
                current_thd->strmake(table.table.str,table.table.length);
        table_clone.table.length = table.table.length;
      }
      if (table.opt_query_block.length > 0) {
        table_clone.opt_query_block.str =
                current_thd->strmake(table.opt_query_block.str,
                                     table.opt_query_block.length);
        table_clone.opt_query_block.length = table.opt_query_block.length;
      }
      table_list_clone.push_back(table_clone);
    }
  }
  if ((with_table_list &&
      !(node = NEW_PTN PT_qb_level_hint(qb_name_clone,
                                        switch_on(),
                                        type(),
                                        table_list_clone)))
      || (!with_table_list &&
      !(node = NEW_PTN PT_qb_level_hint(qb_name_clone,
                                        switch_on(),
                                        type(),
                                        args))))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PT_hint_sys_var::clone(Parse_tree_transformer *ptt) const {
  PT_hint_sys_var *node = nullptr;
  LEX_CSTRING sys_var_name_clone = {nullptr, 0};
  Item *sys_var_value_clone = nullptr;

  if (sys_var_name.length > 0) {
    if(!(sys_var_name_clone.str = current_thd->strmake(sys_var_name.str, 
                                                        sys_var_name.length))) {
      return nullptr;
    }
    sys_var_name_clone.length = sys_var_name.length;
  }

  if(sys_var_value && !(sys_var_value_clone = static_cast<Item*>(sys_var_value->clone(ptt)))) {
    return nullptr;
  }

  if(!(node = NEW_PTN PT_hint_sys_var(sys_var_name_clone, sys_var_value_clone))) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *PT_hint_list::clone(Parse_tree_transformer *ptt) const {
  PT_hint_list *node = NEW_PTN PT_hint_list(cur_mem_root);
  PT_hint *hint_clone;
  for (const PT_hint *hint_tmp : hints) {
    if (!(hint_clone = static_cast<PT_hint *>(hint_tmp->clone(ptt))))
      return nullptr;
    node->push_back(hint_clone);
  }
  transform_safe(ptt, node);
  return node;
}

/* ==============================parse_tree_items================================= */
void *PTI_table_wild::clone(Parse_tree_transformer *ptt) const {
  PTI_table_wild *node = nullptr;
  const char *schema_clone = nullptr;
  const char *table_clone = nullptr;
  if (schema && 
      !(schema_clone = current_thd->strmake(schema,my_strlen(schema))))
    return nullptr;
  if (table && 
      !(table_clone = current_thd->strmake(table,my_strlen(table))))
    return nullptr;
  if (!(node = NEW_PTN PTI_table_wild(POS(), schema_clone, table_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_truth_transform::clone(Parse_tree_transformer *ptt) const {
  PTI_truth_transform *node = nullptr;
  Item *expr_clone = nullptr;
  if (!(expr_clone = static_cast<Item *>(expr->clone(ptt))))
    return nullptr;
  if(!(node = NEW_PTN PTI_truth_transform(POS(),expr_clone,truth_test)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_comp_op::clone(Parse_tree_transformer *ptt) const {
  Item *left_clone = nullptr;
  Item *right_clone = nullptr;
  PTI_comp_op *node = nullptr;

  if (!(left_clone = static_cast<Item *>(left->clone(ptt))) ||
      !(right_clone = static_cast<Item *>(right->clone(ptt))))
    return nullptr;
  if(!(node = NEW_PTN PTI_comp_op(POS(), left_clone, boolfunc2creator, right_clone)))
    return nullptr;
  
  transform_safe(ptt, node);
  return node;
}

void *PTI_comp_op_all::clone(Parse_tree_transformer *ptt) const {
  PTI_comp_op_all *node = nullptr;
  Item *left_clone = nullptr;
  PT_subquery *subselect_clone = nullptr;
  if (!(left_clone = static_cast<Item *>(left->clone(ptt))) ||
      !(subselect_clone = static_cast<PT_subquery *>(subselect->clone(ptt))))
    return nullptr;
  if(!(node = NEW_PTN PTI_comp_op_all(POS(), left_clone, comp_op,
                                      is_all, subselect_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_simple_ident_ident::clone(Parse_tree_transformer *ptt) const {
  PTI_simple_ident_ident *node = nullptr;
  LEX_CSTRING ident_clone{nullptr,0};
  if (ident.length > 0) {
    ident_clone.str = strmake_root(cur_mem_root, ident.str, ident.length);
    ident_clone.length = ident.length;
  }
  if (!ident_clone.str) {
    return nullptr;
  }
  POS pos_clone{{nullptr,nullptr},{raw.start,raw.end}};
  if (!(node = NEW_PTN PTI_simple_ident_ident(pos_clone, ident_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_simple_ident_q_3d::clone(Parse_tree_transformer *ptt) const {
  PTI_simple_ident_q_3d *node = nullptr;
  const char *db_clone = (db) ? current_thd->strmake(db,my_strlen(db)) : nullptr;
  const char *table_clone = (table) ? current_thd->strmake(table,my_strlen(table)) : nullptr;
  const char *field_clone = (field) ? current_thd->strmake(field,my_strlen(field)) : nullptr;
  if (!(node = NEW_PTN 
              PTI_simple_ident_q_3d(POS(),
                                    db_clone,
                                    table_clone,
                                    field_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_simple_ident_q_2d::clone(Parse_tree_transformer *ptt) const {
  PTI_simple_ident_q_2d *node = nullptr;
  const char *table_clone = (table) ? current_thd->strmake(table,my_strlen(table)) : nullptr;
  const char *field_clone = (field) ? current_thd->strmake(field,my_strlen(field)) : nullptr;
  if (!(node = NEW_PTN 
              PTI_simple_ident_q_2d(POS(),
                                    table_clone,
                                    field_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_simple_ident_nospvar_ident::clone(Parse_tree_transformer *ptt) const {
  LEX_STRING ident_clone;
  if (clone_lex_string(ident_clone, ident))
    return nullptr;
  PTI_simple_ident_nospvar_ident *node;
  if (!(node = NEW_PTN PTI_simple_ident_nospvar_ident(POS(), ident_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_function_call_nonkeyword_now::clone(Parse_tree_transformer *ptt) const {
  PTI_function_call_nonkeyword_now *node = nullptr;
  if(!(node = NEW_PTN PTI_function_call_nonkeyword_now(POS(), decimals)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_function_call_nonkeyword_sysdate::clone(Parse_tree_transformer *ptt) const {
  PTI_function_call_nonkeyword_sysdate *node = nullptr;
  if(!(node = NEW_PTN PTI_function_call_nonkeyword_sysdate(POS(), decimals)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_udf_expr::clone(Parse_tree_transformer *ptt) const {
  PTI_udf_expr *node = nullptr;
  Item *expr_clone = nullptr;
  LEX_STRING alias_clone{nullptr, 0};
  if (clone_lex_string(alias_clone, select_alias))
    return nullptr;
  if (!(expr_clone = static_cast<Item *>(expr->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_udf_expr(POS(), expr_clone,
                                    alias_clone, expr_loc)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_function_call_generic_ident_sys::clone(Parse_tree_transformer *ptt) const {
  PTI_function_call_generic_ident_sys *node = nullptr;
  PT_item_list *opt_udf_expr_list_clone = nullptr;
  LEX_STRING ident_clone{nullptr,0};
  if (clone_lex_string(ident_clone, ident))
    return nullptr;
  if (opt_udf_expr_list &&
      !(opt_udf_expr_list_clone = static_cast<PT_item_list *>
                                    (opt_udf_expr_list->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_function_call_generic_ident_sys(POS(),
                                                           ident_clone,
                                                           opt_udf_expr_list_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_function_call_generic_2d::clone(Parse_tree_transformer *ptt) const {
  PTI_function_call_generic_2d *node = nullptr;
  PT_item_list *opt_expr_list_clone = nullptr;
  LEX_STRING func_clone{nullptr,0}, db_clone = {nullptr,0};
  if (clone_lex_string(func_clone, func))
    return nullptr;
  if (clone_lex_string(db_clone, db))
    return nullptr;
  if (opt_expr_list &&
      !(opt_expr_list_clone = static_cast<PT_item_list *>
                                    (opt_expr_list->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_function_call_generic_2d(POS(),
                                                    db_clone,
                                                    func_clone,
                                                    opt_expr_list_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_text_literal_text_string::clone(Parse_tree_transformer *ptt) const {
  PTI_text_literal_text_string *node = nullptr;
  LEX_STRING literal_clone{nullptr,0};
  if (clone_lex_string(literal_clone, literal))
    return nullptr;
  if (!(node = NEW_PTN PTI_text_literal_text_string(POS(), is_7bit, literal_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_text_literal_nchar_string::clone(Parse_tree_transformer *ptt) const {
  PTI_text_literal_nchar_string *node = nullptr;
  LEX_STRING literal_clone{nullptr,0};
  if (clone_lex_string(literal_clone, literal))
    return nullptr;
  if (!(node = NEW_PTN PTI_text_literal_nchar_string(POS(), is_7bit, literal_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_text_literal_underscore_charset::clone(Parse_tree_transformer *ptt) const {
  PTI_text_literal_underscore_charset *node = nullptr;
  LEX_STRING literal_clone{nullptr,0};
  if (clone_lex_string(literal_clone, literal))
    return nullptr;
  if (!(node = NEW_PTN PTI_text_literal_underscore_charset(POS(), is_7bit, cs, literal_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_text_literal_concat::clone(Parse_tree_transformer *ptt) const {
  PTI_text_literal_concat *node = nullptr;
  PTI_text_literal *head_clone = nullptr;
  LEX_STRING tail_clone{nullptr,0};
  if (clone_lex_string(tail_clone, literal))
    return nullptr;
  if (head && !(head_clone = static_cast<PTI_text_literal *>
                                (head->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_text_literal_concat(POS(),
                                               is_7bit,
                                               head_clone,
                                               tail_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_temporal_literal::clone(Parse_tree_transformer *ptt) const {
  PTI_temporal_literal *node = nullptr;
  LEX_STRING literal_clone{nullptr,0};
  if (clone_lex_string(literal_clone, literal))
    return nullptr;
  if (!(node = NEW_PTN PTI_temporal_literal(POS(), literal_clone,
                                            field_type, cs)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

PTI_literal_underscore_charset_hex_num::\
  PTI_literal_underscore_charset_hex_num(const POS &pos,
                                         const Item_string *item)
  : super(pos, item->item_name, item->get_str_value_addr()->ptr(),
          item->get_str_value_addr()->length(),item->collation.collation,
          item->collation.derivation, item->collation.repertoire) {}

PTI_literal_underscore_charset_bin_num::\
  PTI_literal_underscore_charset_bin_num(const POS &pos,
                                         const Item_string *item)
  : super(pos, item->item_name, item->get_str_value_addr()->ptr(),
          item->get_str_value_addr()->length(),item->collation.collation,
          item->collation.derivation, item->collation.repertoire) {}

void *PTI_literal_underscore_charset_hex_num::clone(Parse_tree_transformer *ptt) const {
  PTI_literal_underscore_charset_hex_num *node = nullptr;
  if (!(node = NEW_PTN PTI_literal_underscore_charset_hex_num(POS(), this)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}
void *PTI_literal_underscore_charset_bin_num::clone(Parse_tree_transformer *ptt) const {
  PTI_literal_underscore_charset_bin_num *node = nullptr;
  if (!(node = NEW_PTN PTI_literal_underscore_charset_bin_num(POS(), this)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_variable_aux_set_var::clone(Parse_tree_transformer *ptt) const {
  PTI_variable_aux_set_var *node = nullptr;
  Item *expr_clone = nullptr;
  char *str = nullptr;
  if (!(str = strmake_root(cur_mem_root,name.ptr(),name.length())))
    return nullptr;
  LEX_STRING name_clone{str, name.length()};
  if (args[0] && !(expr_clone = static_cast<Item *>(args[0]->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_variable_aux_set_var(POS(), name_clone, expr_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_variable_aux_ident_or_text::clone(Parse_tree_transformer *ptt) const {
  PTI_variable_aux_ident_or_text *node = nullptr;
  char *str = nullptr;
  if (!(str = strmake_root(cur_mem_root,name.ptr(),name.length())))
    return nullptr;
  LEX_STRING name_clone{str, name.length()};
  if (!(node = NEW_PTN PTI_variable_aux_ident_or_text(POS(), name_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_variable_aux_3d::clone(Parse_tree_transformer *ptt) const {
  PTI_variable_aux_3d *node = nullptr;
  LEX_STRING ident1_clone, ident2_clone;
  if (clone_lex_string(ident1_clone, ident1) ||
      clone_lex_string(ident2_clone, ident2))
    return nullptr;
  if (!(node = NEW_PTN PTI_variable_aux_3d(POS(), var_type,
                                           ident1_clone,
                                           ident1_pos,
                                           ident2_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_count_sym::clone(Parse_tree_transformer *ptt) const {
  PTI_count_sym *node = nullptr;
  PT_window *window_clone = nullptr;
  if (m_window && !(window_clone = static_cast<PT_window *>
                                    (m_window->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_count_sym(POS(), window_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_in_sum_expr::clone(Parse_tree_transformer *ptt) const {
  PTI_in_sum_expr *node = nullptr;
  Item *expr_clone = nullptr;
  if (!(expr_clone = static_cast<Item *>(expr->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_in_sum_expr(POS(), expr_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_singlerow_subselect::clone(Parse_tree_transformer *ptt) const {
  PTI_singlerow_subselect *node = nullptr;
  PT_subquery *subselect_clone = nullptr;
  if (subselect && !(subselect_clone = static_cast<PT_subquery *>
                                          (subselect->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_singlerow_subselect(POS(),subselect_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_exists_subselect::clone(Parse_tree_transformer *ptt) const {
  PTI_exists_subselect *node = nullptr;
  PT_subquery *subselect_clone = nullptr;
  if (subselect && !(subselect_clone = static_cast<PT_subquery *>
                                          (subselect->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_exists_subselect(POS(),subselect_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_odbc_date::clone(Parse_tree_transformer *ptt) const {
  PTI_odbc_date *node = nullptr;
  LEX_STRING ident_clone;
  Item *expr_clone = nullptr;
  if (clone_lex_string(ident_clone, ident))
    return nullptr;
  if (!(expr_clone = static_cast<Item *>(expr->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_odbc_date(POS(),ident_clone,expr_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_handle_sql2003_note184_exception::clone(Parse_tree_transformer *ptt) const {
  PTI_handle_sql2003_note184_exception *node = nullptr;
  Item *left_clone = nullptr;
  Item *right_clone = nullptr;
  if (left && !(left_clone = static_cast<Item *>(left->clone(ptt))))
    return nullptr;
  if (right && !(right_clone = static_cast<Item *>(right->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN 
          PTI_handle_sql2003_note184_exception(POS(), left_clone,
                                               is_negation,
                                               right_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_expr_with_alias::clone(Parse_tree_transformer *ptt) const {
  PTI_expr_with_alias *node = nullptr;
  LEX_CSTRING alias_clone{nullptr,0};
  if (clone_lex_cstring(alias_clone, alias))
    return nullptr;
  Item *expr_clone = nullptr;
  if (!(expr_clone = static_cast<Item *>(expr->clone(ptt))))
    return nullptr;
  Symbol_location expr_loc_clone{expr_loc.start, expr_loc.end};
  if (!(node = NEW_PTN PTI_expr_with_alias(POS(), expr_clone,
                                           expr_loc_clone,
                                           alias_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *PTI_limit_option_ident::clone(Parse_tree_transformer *ptt) const {
  PTI_limit_option_ident *node = nullptr;
  LEX_CSTRING ident_clone;
  if (clone_lex_cstring(ident_clone, ident))
    return nullptr;
  if (!(node = NEW_PTN PTI_limit_option_ident(POS(), ident_clone, ident_loc)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_limit_option_param_marker::clone(Parse_tree_transformer *ptt) const {
  PTI_limit_option_param_marker *node = nullptr;
  if (!(node = NEW_PTN PTI_limit_option_param_marker(POS(), param_marker)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *PTI_prior_item::clone(Parse_tree_transformer *ptt) const {
  Item *item_clone = static_cast<Item*>(item->clone(ptt));
  if(!item_clone) {
    return nullptr;
  }

  PTI_prior_item *node = NEW_PTN PTI_prior_item(POS(), item_clone);
  if(!node) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

template class PTI_context<CTX_WHERE>;
template class PTI_context<CTX_HAVING>;
template <enum_parsing_context Context>
void *PTI_context<Context>::clone(Parse_tree_transformer *ptt) const {
  PTI_context<Context> *node = nullptr;
  Item *expr_clone = nullptr;
  if (!(expr_clone = static_cast<Item *>(expr->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN PTI_context<Context>(POS(),expr_clone)))
    return nullptr;
  
  transform_safe(ptt,node);
  return node;
}

/* =================================new_items================================= */
#define ITEM_COND_TEMPLATE_POS_N_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(list.elements >= 2); \
 \
  Item_class *node = nullptr; \
  Item *a_clone = nullptr; \
  Item *b_clone = nullptr; \
  Item *tmp_clone = nullptr; \
 \
  List_iterator_const<Item> li(list); \
  if (!(a_clone = static_cast<Item *>((li++)->clone(ptt))) || \
      !(b_clone = static_cast<Item *>((li++)->clone(ptt)))) \
    return nullptr; \
  if (!(node = NEW_PTN Item_class(POS(), a_clone, b_clone))) \
    return nullptr; \
 \
  if (list.elements > 2) { \
    const Item *item; \
    while ((item = li++)){ \
      if (!(tmp_clone = static_cast<Item *>(item->clone(ptt)))) \
        return nullptr; \
      if (node->add(tmp_clone)) \
        return nullptr; \
    } \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

// and & or condition clone
void *Item_cond_and::clone(Parse_tree_transformer *ptt) const {
  ITEM_COND_TEMPLATE_POS_N_ITEM(Item_cond_and, ptt);
}

void *Item_cond_or::clone(Parse_tree_transformer *ptt) const {
  ITEM_COND_TEMPLATE_POS_N_ITEM(Item_cond_or, ptt);
}

// rownum
void *Item_func_rownum::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_func_rownum, ptt);
}

// type cast items
void *Item_typecast_date::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_typecast_date, ptt);
}

void *Item_typecast_time::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 1);
  DBUG_ASSERT(args[0]);

  Item_typecast_time *node = nullptr;
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(!a_clone) {
    return nullptr;
  }

  if (detect_precision_from_arg)
    node = NEW_PTN Item_typecast_time(POS(), a_clone);
  else {
    node = NEW_PTN Item_typecast_time(POS(), a_clone, decimals);
  }
  if (!node)
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_typecast_datetime::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 1);
  DBUG_ASSERT(args[0]);

  Item_typecast_datetime *node = nullptr;
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(!a_clone) {
    return nullptr;
  }

  if (detect_precision_from_arg)
    node = NEW_PTN Item_typecast_datetime(POS(), a_clone);
  else {
    node = NEW_PTN Item_typecast_datetime(POS(), a_clone, decimals);
  }
  if (!node)
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_typecast_signed::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_typecast_signed, ptt);
}

void *Item_typecast_unsigned::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_typecast_unsigned, ptt);
}

// date and time func items
void *Item_func_year::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_year, ptt);
}

void *Item_func_month::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_month, ptt);
}

void *Item_func_dayofmonth::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_dayofmonth, ptt);
}

void *Item_func_hour::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_hour, ptt);
}

void *Item_func_minute::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_minute, ptt);
}

void *Item_func_second::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_second, ptt);
}

void *Item_func_microsecond::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_microsecond, ptt);
}

void *Item_func_quarter::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_quarter, ptt);
}

void *Item_func_week::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 2);
  DBUG_ASSERT(args[0]);

  Item_func_week *node = nullptr;
  Item *b_clone = nullptr;
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(args[1])
    b_clone = static_cast<Item*>(args[1]->clone(ptt));
  if(!a_clone || (args[1] && !b_clone)) {
    return nullptr;
  }

  if(!(node = NEW_PTN Item_func_week(POS(), a_clone, b_clone))) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_row::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(items[0]);
  Item_row *node = nullptr;
  List<Item> tail_arg;
  Item *head_clone = nullptr;
  if (!(head_clone = static_cast<Item *>(items[0]->clone(ptt))))
    return nullptr;
  for (uint i = 1; i < arg_count; i++) {
    Item *item_clone = nullptr;
    if (!(item_clone = static_cast<Item *>(items[i]->clone(ptt))))
      return nullptr;
    if (tail_arg.push_back(item_clone))
      return nullptr;
  }
  if (!(node = NEW_PTN Item_row(POS(), head_clone, tail_arg)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *Item_in_subselect::clone(Parse_tree_transformer *ptt) const {
  Item_in_subselect *node = nullptr;
  Item *left_expr_clone = nullptr;
  PT_subquery *subselect_clone = nullptr;

  if (pt_subselect &&
      !(subselect_clone = static_cast<PT_subquery *>
                          (pt_subselect->clone(ptt))))
    return nullptr;
  if (left_expr &&
      !(left_expr_clone = static_cast<Item *>(left_expr->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN Item_in_subselect(POS(), left_expr_clone,
                                         subselect_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}


void *Item_param::clone(Parse_tree_transformer *) const {
  return const_cast<Item_param *>(this);
}

void *Item_extract::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 1);
  DBUG_ASSERT(args[0]);
  Item_extract *node = nullptr;
  Item *arg_clone = static_cast<Item *>(args[0]->clone(ptt));
  if (!arg_clone)
    return nullptr;
  if (!(node = NEW_PTN Item_extract(POS(), int_type, arg_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_interval::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(row);
  uint n_cols = row->cols();
  DBUG_ASSERT(n_cols >= 2);
  Item *expr1_clone = nullptr;
  Item *expr2_clone = nullptr;
  if (!(expr1_clone = static_cast<Item *>(row->element_index(0)->clone(ptt)))
      || !(expr2_clone = static_cast<Item *>(row->element_index(1)->clone(ptt))))
    return nullptr;
  PT_item_list *exprs_clone = nullptr;
  if (n_cols>2 &&
      !(exprs_clone = clone_func_args_as_item_list(row->addr(2),
                                                   n_cols-2, ptt)))
    return nullptr;
  Item_func_interval *node = nullptr;
  if (!(node = NEW_PTN Item_func_interval(POS(), cur_mem_root,
                                          expr1_clone,
                                          expr2_clone,
                                          exprs_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_add_time::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 2);
  DBUG_ASSERT(args[0]);
  DBUG_ASSERT(args[1]);
  Item_func_add_time *node = nullptr;
  Item *a_clone = nullptr;
  Item *b_clone = nullptr;
  if (!(a_clone = static_cast<Item *>(args[0]->clone(ptt))) ||
      !(b_clone = static_cast<Item *>(args[1]->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN Item_func_add_time(POS(), a_clone, b_clone,
                                          is_date, sign<0)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_date_add_interval::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 2);
  DBUG_ASSERT(args[0]);
  DBUG_ASSERT(args[1]);
  Item_date_add_interval *node = nullptr;
  Item *a_clone = nullptr;
  Item *b_clone = nullptr;
  if (!(a_clone = static_cast<Item *>(args[0]->clone(ptt))) ||
      !(b_clone = static_cast<Item *>(args[1]->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN Item_date_add_interval(POS(), a_clone,
                                              b_clone,
                                              int_type,
                                              date_sub_interval)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_curdate_local::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_func_curdate_local, ptt);
}

void *Item_func_curdate_utc::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_0_ITEM(Item_func_curdate_utc, ptt);
}

#define ITEM_TIMEFUNC_TEMPLATE_POS_0_ITEM(Item_class, ptt) \
do { \
  DBUG_ASSERT(arg_count == 0); \
 \
  Item_class *node = nullptr; \
 \
  if(!(node = NEW_PTN Item_class(POS(), decimals))) { \
    return nullptr; \
  } \
 \
  transform_safe(ptt, node); \
  return node; \
} while(0)

void *Item_func_now_utc::clone(Parse_tree_transformer *ptt) const {
  ITEM_TIMEFUNC_TEMPLATE_POS_0_ITEM(Item_func_now_utc, ptt);
}

void *Item_func_curtime_local::clone(Parse_tree_transformer *ptt) const {
  ITEM_TIMEFUNC_TEMPLATE_POS_0_ITEM(Item_func_curtime_local, ptt);
}

void *Item_func_curtime_utc::clone(Parse_tree_transformer *ptt) const {
  ITEM_TIMEFUNC_TEMPLATE_POS_0_ITEM(Item_func_curtime_utc, ptt);
}

void *Item_func_now_systimestamp::clone(Parse_tree_transformer *ptt) const {
  ITEM_TIMEFUNC_TEMPLATE_POS_0_ITEM(Item_func_now_systimestamp, ptt);
}

void *Item_func_get_format::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 1);
  DBUG_ASSERT(args[0]);
  Item_func_get_format *node = nullptr;
  Item *arg_clone = static_cast<Item *>(args[0]->clone(ptt));
  if (!arg_clone)
    return nullptr;
  if (!(node = NEW_PTN Item_func_get_format(POS(), type, arg_clone)))
    return nullptr;
  transform_safe(ptt, node);
  return node;
}

void *Item_func_timestamp_diff::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 2);
  DBUG_ASSERT(args[0]);
  DBUG_ASSERT(args[1]);
  Item_func_timestamp_diff *node = nullptr;
  Item *a_clone = nullptr;
  Item *b_clone = nullptr;
  if (!(a_clone = static_cast<Item *>(args[0]->clone(ptt))) ||
      !(b_clone = static_cast<Item *>(args[1]->clone(ptt))))
    return nullptr;
  if (!(node = NEW_PTN Item_func_timestamp_diff(POS(), a_clone,
                                              b_clone,
                                              int_type)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

void *Item_func_member_of::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_2_ITEM(Item_func_member_of, ptt);
}

void *Item_func_json_extract::clone(Parse_tree_transformer *ptt) const {
  /* Only need to clone this func with 2 args */
  DBUG_ASSERT(arg_count == 2);
  DBUG_ASSERT(args[0] && args[1]);

  Item_func_json_extract *node = nullptr;
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  Item *b_clone = static_cast<Item*>(args[1]->clone(ptt));
  if(!a_clone || !b_clone) {
    return nullptr;
  }

  if(!(node = NEW_PTN Item_func_json_extract(current_thd, POS(), a_clone, b_clone))) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_json_unquote::clone(Parse_tree_transformer *ptt) const {
  /* Only need to clone this func with 1 args */
  ITEM_FUNC_TEMPLATE_POS_1_ITEM(Item_func_json_unquote, ptt);
}

void *Item_func_array_cast::clone(Parse_tree_transformer *ptt) const {
  DBUG_ASSERT(arg_count == 1);
  DBUG_ASSERT(args[0]);

  Item_func_array_cast *node = nullptr;
  Item *a_clone = static_cast<Item*>(args[0]->clone(ptt));
  if(!a_clone) {
    return nullptr;
  }

  if(!(node = NEW_PTN Item_func_array_cast(POS(), a_clone,
                                           cast_type,
                                           orig_len_arg,
                                           decimals,
                                           cs))) {
    return nullptr;
  }

  transform_safe(ptt, node);
  return node;
}

void *Item_func_regexp_like::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_ITEMLIST(Item_func_regexp_like, ptt);
}

void *Item_func_if::clone(Parse_tree_transformer *ptt) const {
  ITEM_FUNC_TEMPLATE_POS_3_ITEM(Item_func_if, ptt);
}

void *Item_func_case::clone(Parse_tree_transformer *ptt) const {
  Item *first_expr_clone = nullptr;
  Item *else_expr_clone = nullptr;
  PT_item_list *list_clone = nullptr;
  Item_func_case *node = nullptr;
  if (first_expr_num >= 0) {
    DBUG_ASSERT((longlong)arg_count > first_expr_num);
    DBUG_ASSERT(args[first_expr_num]);
    if (!(first_expr_clone = static_cast<Item *>(args[first_expr_num]->clone(ptt))))
      return nullptr;
  }
  if (else_expr_num >= 0) {
    DBUG_ASSERT((longlong)arg_count > else_expr_num);
    DBUG_ASSERT(args[else_expr_num]);
    if (!(else_expr_clone = static_cast<Item *>(args[else_expr_num]->clone(ptt))))
      return nullptr;
  }
  if (!(list_clone = clone_func_args_as_item_list(args, ncases, ptt)))
    return nullptr;
  if (!(node = NEW_PTN Item_func_case(POS(), list_clone->value,
                                      first_expr_clone,
                                      else_expr_clone)))
    return nullptr;

  transform_safe(ptt, node);
  return node;
}

