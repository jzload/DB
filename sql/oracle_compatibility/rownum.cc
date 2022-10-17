#include "sql/oracle_compatibility/rownum.h"
#include "sql/sql_lex.h"
#include "sql/sql_class.h"
#include "sql/item_cmpfunc.h"

#include <string.h>
#include <atomic>
#include <string>
#include <vector>

RownumIterator::RownumIterator(
    THD *thd,
    unique_ptr_destroy_only<RowIterator> source,
    Item *condition,
    SELECT_LEX *select_lex)
    : RowIterator(thd),
      m_source(move(source)),
      m_condition(condition),
      m_select(select_lex)
    {}

bool RownumIterator::Init() {
  m_select->m_Rownum_controller.init();
  return m_source->Init();
}

int RownumIterator::Read() {
  bool matched = true;
  m_select->m_Rownum_controller.set_increased(false);

  int err = m_source->Read();
  if (err != 0) return err;

  if (thd()->killed) {
    thd()->send_kill_message();
    return 1;
  }

  if (NULL != m_condition)
    matched = 0 != m_condition->val_int();

  /* check for errors evaluating the condition */
  if (thd()->is_error())
    return 1;

  if (!matched) {
    m_source->UnlockRow();
    // Once one unmatched row is found, select finished early.
    return -1;
  }
  else {
    m_select->m_Rownum_controller.increase_rownum();
    // Successful row.
    return 0;
  }

}

std::vector<std::string> RownumIterator::DebugString() const {
  //vector<string> ret = FullDebugString(thd(), *m_source);
  std::vector<std::string> ret;
  std::string strRownum("ROWNUM");
  if (m_condition)
    strRownum.append(" with filter: ").append(ItemToString(m_condition));
  ret.push_back(strRownum);
  return ret;
}

Item_func_rownum::Item_func_rownum(
  const POS &pos)
  : Item_int_func(pos),
  m_select(NULL)
  {
  item_name.set("rownum");
  }

Item_func_rownum::Item_func_rownum(SELECT_LEX *select)
  : Item_int_func(),
  m_select(select)
  {
  item_name.set("rownum");
  }

bool Item_func_rownum::itemize(Parse_context *pc, Item **res) {
  if (skip_itemize(res)) return false;
  if (super::itemize(pc, res)) return true;

  m_select = pc->select;
  pc->select->with_rownum = true;

  switch (m_select->parsing_place)
  {
    case CTX_SELECT_LIST:
      break;
    case CTX_WHERE:
      break;
    case CTX_GROUP_BY:
    case CTX_SIMPLE_GROUP_BY:
      m_select->m_rownum_option |= ROWNUM_IN_GROUP_FIELD;
      break;
    case CTX_HAVING:
      m_select->m_rownum_option |= ROWNUM_IN_HAVING_COND;
      break;
    case CTX_ORDER_BY:
    case CTX_SIMPLE_ORDER_BY:
      m_select->m_rownum_option |= ROWNUM_IN_ORDER_FIELD;
      break;
    case CTX_ON:
      my_error(ER_MISUSE_ROWNUM, MYF(0), "in ON condition");
      return true;
    default:
      m_select->m_rownum_option |= ROWNUM_ALL_FLAGS;
      break;
  }

  pc->thd->lex->set_stmt_unsafe(LEX::BINLOG_STMT_UNSAFE_ROWNUM);
  pc->thd->lex->safe_to_cache_query = false;
  return false;
}

longlong Item_func_rownum::val_int() {
  return m_select->m_Rownum_controller.get_counter();
}

void Item_func_rownum::print(const THD *, String *str, enum_query_type) const {
  str->append(func_name());
}

/*
    Input: THD *thd, SELECT_LEX *select_lex, uint el, Item** array, uint array_length
    Return: true: fail, false: succeed

    Function: The number of items in array is array_length. Recursively search
    each item in the array and check if this item is Item_func_rownum.
    If the item is a Item_func_rownum, the item in array
    is replaced by a new item Item_ref pointing to select_lex->base_ref_items[el].

    The output change_row_to_ref of function find_rownum_item marks whether
    the item is a Item_func_rownum.
*/
bool Item::search_array_for_rownum(THD *thd, SELECT_LEX *select_lex, uint el,
                                   Item** array, uint array_length) {
  bool change_row_to_ref;
  Item **arg, **arg_end;
  for (arg = array, arg_end = array + array_length; arg != arg_end; arg++)
  {
    if((*arg)->find_rownum_item(thd, select_lex, el, change_row_to_ref))
      return true;

    if (change_row_to_ref)
    {
      Item_ref *item_ref = new Item_ref(
                           &select_lex->context,
                           &select_lex->base_ref_items[el], 0,
                           (select_lex->base_ref_items[el])->item_name.ptr());
      if (NULL == item_ref)
      {
        my_error(ER_OUT_OF_RESOURCES, MYF(ME_FATALERROR));
        return true;
      }

      thd->change_item_tree(arg, item_ref);
      if (!item_ref->fixed && item_ref->fix_fields(thd, &select_lex->base_ref_items[el]))
        return true;
    }
  }
  return false;
}

/*
    Input: THD *thd, SELECT_LEX *select_lex, uint el
    Output: bool change_row_to_ref : id always false, that means this is not a Item_func_rownum
    Return: true: fail, false: succeed

    Function: Recursively search each item in the args and replace it if needed.
    The action is implemented in function search_array_for_rownum.
*/
bool Item_func::find_rownum_item(THD *thd, SELECT_LEX *select_lex, uint el,
                                 bool &change_row_to_ref) {
  if (search_array_for_rownum(thd, select_lex, el, args, arg_count))
    return true;

  change_row_to_ref = false;
  return false;
}

/*
    Input: THD *thd, SELECT_LEX *select_lex, uint el
    Output: bool change_row_to_ref : id always false, that means this is not a Item_func_rownum
    Return: true: fail, false: succeed

    Function: Recursively search each item in the items and replace it if needed.
    The action is implemented in function search_array_for_rownum.
*/
bool Item_row::find_rownum_item(THD *thd, SELECT_LEX *select_lex, uint el,
                                 bool &change_row_to_ref) {
  if (search_array_for_rownum(thd, select_lex, el, items, arg_count))
    return true;

  change_row_to_ref = false;
  return false;
}

/*
    Return: true: this item is an extractable rownum cond, false: this item is not extractable

    Function: An extractable rownum cond means:
    1. It is a compare between a Item_func_rownum and a const item.
       The compare operation can be ==, !=, <, <=, > or >=.
    2. ROWNUM betweem a and b. a and b are both const items and the same type
    3. ROWNUM in (a list of items that are all const items and the same type)

    At present, only simple compare with ROWNUM is extractable for stopping the query early.
*/
bool Item_func::find_extractable_rownum_cond() {
  switch (functype()) {
  case EQ_FUNC:
  case NE_FUNC:
  case LT_FUNC:
  case LE_FUNC:
  case GE_FUNC:
  case GT_FUNC: {
     if ((args[0]->type() == Item::FUNC_ITEM &&
          down_cast<Item_func *>(args[0])->functype() == ROWNUM_FUNC && args[1]->const_item()) ||
        (args[1]->type() == Item::FUNC_ITEM &&
          down_cast<Item_func *>(args[1])->functype() == ROWNUM_FUNC && args[0]->const_item())) {
      return true;
    }
    break;
  }
  case BETWEEN:
  case IN_FUNC: {
    if (args[0]->type() == Item::FUNC_ITEM &&
        down_cast<Item_func *>(args[0])->functype() == ROWNUM_FUNC) {
      // Can only substitute if all the operands on the right-hand
      // side are constants of the same type.
      Item_result type = args[1]->result_type();
      if (std::all_of(args + 1, args + arg_count, [type](const Item *arg) {
          return arg->const_item() && arg->result_type() == type;})) {
        return true;
       }
     }
    break;
  }
  default:
    return false;
  }
  return false;
}

/*
    Input: THD *thd, SELECT_LEX *select_lex, uint el
    Output: bool change_row_to_ref : id always false, that means this is not a Item_func_rownum
    Return: true: fail, false: succeed

    Function: Recursively search each item in list.
    A Item_func_rownum in list of Item_cond will be replace by a Item_func_ne in
    the sql parse step (itemize), so the item in list will never be a Item_func_rownum.
*/
bool Item_cond::find_rownum_item(THD *thd, SELECT_LEX *select_lex, uint el, bool &change_row_to_ref) {
  Item *item_in_list;
  List_iterator<Item> li(list);
  while ((item_in_list = li++))
  {
    if(item_in_list->find_rownum_item(thd, select_lex, el, change_row_to_ref))
      return true;

    DBUG_ASSERT(!change_row_to_ref);
  }

  change_row_to_ref = false;
  return false;
}

/*
    Return: true: fail, false: succeed

    Function: A new Item_func_rownum is created and pushed to all_fields as a hidden column,
    and the address is saved in select_lex->base_ref_items[el].
    Increase tmp_table_param.func_count because it has been counted before in .
    The Item_func_rownum in having_cond will be replace to a Item_ref pointing to
    the new Item_func_rownum by the way of the recursively calling find_rownum_item.
*/
bool JOIN::replace_rownum_item()
{
  uint el = all_fields.elements;
  Item_func_rownum *const item = new Item_func_rownum(select_lex);
  if (NULL == item)
  {
    my_error(ER_OUT_OF_RESOURCES, MYF(ME_FATALERROR));
    return true;
  }
  if (item->fix_fields(thd, NULL)) return true;
  select_lex->base_ref_items[el] = item;
  all_fields.push_front(item);
  tmp_table_param.func_count++;

  if (having_cond)
  {
    bool change_row_to_ref = false;
    if(having_cond->find_rownum_item(thd, select_lex, el, change_row_to_ref))
      return true;
    DBUG_ASSERT(!change_row_to_ref);
  }

  return false;
}


/***************************************************************************
    Return: true: fail, false: succeed

steps:
1. filter that it doesen't need to new a func_rownum. if so, return.
2. If there is a group by and a having with Item_func_rownum,
   this rownum item should be replaced by a Item_ref reffered to a new
   Item_func_rownum which is a hidden column in all_fields,
   otherwise the Item_func_rownum in having cannot get a right result
   associoate with the row.
   If it is a implicit grouping like sum without group or window func needs sorting
   in the first sorting should be handled the same way.
3. new a func_rownum, then replace all rownum items in lex tree
***************************************************************************/

bool JOIN::make_rownum_info()
{
  if ((select_lex->m_rownum_option & ROWNUM_IN_HAVING_COND) &&
      (group_list || implicit_grouping ||
       (m_windows.elements != 0 && m_windows[0]->needs_sorting())))
  {
    if(replace_rownum_item())
      return true;
  }

  return false;
}

/*
    Input:THD *thd, Item *where_cond: the item to be processed
    Input&Output: Item_cond_and *new_rownum_cond:
                  extracted rownum cond is saved in the list in new_rownum_cond
    Return: true: this item is an extractable rownum cond,
            false: this item is not extractable

    Function:
    1. where_cond is a Item_cond_and:
       check each item in cond->argument_list(),
       if it is extract_rownum_from_cond, remove the it in list (the previous item of li),
       return false.
    2. where_cond is a Item_func:
       check if it is a extractable rownum cond,
       if true, push it to new_rownum_cond->argument_list(), return true.
    3. others:
       return false.
*/
bool extract_rownum_from_cond(THD *thd, Item *where_cond, Item_cond_and *new_rownum_cond) {
  if (thd == NULL || where_cond == NULL || new_rownum_cond == NULL)
    return false;

  if (where_cond->type() == Item::COND_ITEM) {
    Item_cond *cond = down_cast<Item_cond *>(where_cond);
    if (cond->functype() == Item_func::COND_AND_FUNC) {
      List_iterator<Item> li(*cond->argument_list());
      Item *item;
      while ((item = li++)) {
        if (extract_rownum_from_cond(thd, item, new_rownum_cond))
        {
          li.remove();
        }
      }
    }
  }
  else if (where_cond->type() == Item::FUNC_ITEM) {
    Item_func *cond = down_cast<Item_func *>(where_cond);
    if (cond->arg_count >= 2 && cond->find_extractable_rownum_cond()) {
      new_rownum_cond->argument_list()->push_back(where_cond);
      return true;
    }
  }
  return false;
}

/*
    Input:THD *thd, Item *where_cond: the item to be processed
    Output:  Item **rownum_cond:
             collection of extracted rownum conds.
    Return: true: fail, false: succeed

    Function:
    1. prepare a new Item_cond_and
    2. extract from where_cond
    3. reorganize where_cond if needed
    4. reorganize rownum_cond if needed
*/
bool extract_rownum_from_where_cond(THD *thd, Item **where_cond, Item **rownum_cond) {
  if (thd == NULL || where_cond == NULL)
    return false;
  DBUG_ASSERT(NULL != *where_cond);
  DBUG_ASSERT((Item *)1 != *where_cond);

  /* Create new top level AND item from rownum */
  Item_cond_and *where_rownum_cond = new Item_cond_and;
  if (NULL == where_rownum_cond)
  {
    my_error(ER_OUT_OF_RESOURCES, MYF(ME_FATALERROR));
    return true;
  }

  if (extract_rownum_from_cond(thd, *where_cond, where_rownum_cond))
  {
    *where_cond = NULL;
  }

  switch (where_rownum_cond->argument_list()->elements) {
    case 0:
      *rownum_cond = NULL;
      break; // Always true
    case 1:
      *rownum_cond = where_rownum_cond->argument_list()->head();
      break;
    default:
      if (where_rownum_cond->fix_fields(thd, NULL)) return true;
      *rownum_cond = where_rownum_cond;
  }
  return false;
}

/*
    Return: 1: fail, 0: succeed, -1: normal end because of rownum

    Function: deal with rownum in single table dml(delete and update)
    1. check if match the rownum condition
    2. if not match, return -1
    3. if match, increase rownum and return 0
*/
int deal_with_rownum_in_single_table_dml(THD *thd, SELECT_LEX *const select_lex,
                                      QEP_TAB &qep_tab)
{
  DBUG_ASSERT(NULL != thd && NULL != select_lex);
  bool match_rownum_cond;
  if (select_lex->with_rownum)
  {
    if (qep_tab.match_rownum_condition(thd, match_rownum_cond))
    {
      return 1;
    }
    if (!match_rownum_cond)
    {
      return -1;
    }
    select_lex->m_Rownum_controller.increase_rownum();
  }
  DBUG_ASSERT(!thd->is_error());
  return 0;
}
