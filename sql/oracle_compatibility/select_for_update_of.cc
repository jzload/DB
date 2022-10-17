#include "sql/sql_lex.h"
#include "sql/sql_class.h"
#include "sql/sql_base.h"
#include "sql/parse_tree_nodes.h"

bool open_table_get_mdl_lock(THD *thd, Open_table_context *ot_ctx,
                                    TABLE_LIST *table_list, uint flags,
                                    MDL_ticket **mdl_ticket);

bool SELECT_LEX::setup_of_clause(THD* thd)
{
    DBUG_TRACE;

    DBUG_ASSERT(thd->variables.m_opt_parse_mode == ORACLE_PARSE_MODE);

    thd->where = "of list";

    Item *item;
    List_iterator<Item> it(m_lock_item_list);
    while ((item = it++))
    {
        if (item->fixed 
            || item->fix_fields(thd, it.ref()) 
            || (*it.ref())->type() != Item::FIELD_ITEM)
        {
            my_error(ER_BAD_FIELD_ERROR, MYF(0), item->item_name.ptr(),thd->where);
            return true; 
        }
    }

    return false;
}

bool SELECT_LEX::acquire_tables_sw_lock(THD *thd)
{
    DBUG_TRACE;

    DBUG_ASSERT(thd->variables.m_opt_parse_mode == ORACLE_PARSE_MODE);

    PT_table_locking_clause *table_locking = this->m_table_locking;
    this->m_table_locking = nullptr;

    Item *item;
    List_iterator<Item> it(m_lock_item_list);

    /**
        This is allowed to lock multiple tables in one THD using statement like 'flush table t1, t2, t3 with read'
        or 'lock table t1 read, t2 write'.

        We cannot lock other tables immediately after the previous lock statement.
        The following statement will report an error:
        1th: 'flush table t1 with read lock' and 'flush table t2 with read'  return ER_LOCK_OR_ACTIVE_TRANSACTION
        2th: 'flush table t1 with read lock' and 'flush table t2 for export'  return ER_LOCK_OR_ACTIVE_TRANSACTION
        3th: 'flush table t1 for export' and 'flush table t2 with read'  return ER_LOCK_OR_ACTIVE_TRANSACTION
        4th: 'lock table t1 read' and 'flush table t2 with read'            return ER_LOCK_OR_ACTIVE_TRANSACTION

        When we executing 'lock table ', ZSQL will first execute 'unlock tables' implicitly and We can not
        operator other tables of course. Such as
        'lock table t1 read' and 'select * from t2'  return ER_TABLE_NOT_LOCKED

        About INNODB ROW_LOCK upgrading:
        It should be error if executing 'lock tables t read' or 'flush table t with read' first
        and then executing 'select for update of' in the same THD.
        'lock tables t read' statement will acquire MDL (SRO) which corresponds to ROW_LOCK (TL_READ_NO_INSERT)
        'lock tables t write' statement will acquire MDL (SNRW) which corresponds to ROW_LOCK (TL_WRITE)
        'flush table t with read' statement will acquire MDL (SNW) which corresponds to ROW_LOCK (TL_READ_NO_INSERT)
        'flush table t for export' statement will acquire MDL (SNW) which corresponds to ROW_LOCK (TL_READ_NO_INSERT)
        'select for update of' statement will acquire MDL (SW) which corresponds to ROW_LOCK (TL_WRITE)

        We can't upgrade TL_READ_NO_INSERT to TL_WRITE, it should return ER_TABLE_NOT_LOCKED_FOR_WRITE.
    */                             
    if (thd->locked_tables_mode)
    {
        while ((item = it++))
        {
            Item_field* item_field = down_cast<Item_field*>(item);
            TABLE_LIST* cur_table = item_field->table_ref;
            DBUG_ASSERT(item_field->fixed && cur_table && cur_table->table->mdl_ticket);

            if (check_mdl_lock(cur_table)) return true;
        }
    }

    it.rewind();

    while ((item = it++))
    {
        Item_field* item_field = down_cast<Item_field*>(item);
        TABLE_LIST* cur_table = item_field->table_ref;
        DBUG_ASSERT(item_field->fixed && cur_table && cur_table->table && cur_table->table->mdl_ticket);

        if (get_mdl_lock(thd, cur_table, table_locking)) return true;
    }

    return false;
}

/**
    For a same table, subsequent weak MDL_lock can't modify ROW_lock, So the member 'lock_type' in 
    struct "table's reginfo" will save strong ROW_lock type.  
    
    We can't upgrade TL_READ_NO_INSERT to TL_WRITE, it should return ER_TABLE_NOT_LOCKED_FOR_WRITE. 
    such as 'lock tables t read' and then executing 'select for update of'.
*/
bool SELECT_LEX::check_mdl_lock(TABLE_LIST* cur_table)
{
    if (cur_table->table->reginfo.lock_type <= TL_WRITE_ALLOW_WRITE)
    {
        my_error(ER_TABLE_NOT_LOCKED_FOR_WRITE, MYF(0), cur_table->table_name);
        return true;
    }

    return false;
}

bool SELECT_LEX::get_mdl_lock(THD* thd, TABLE_LIST* cur_table, PT_table_locking_clause* table_locking)
{
    Open_table_context ot_ctx(thd, 0);
    MDL_ticket* mdl_ticket = nullptr;
    cur_table->mdl_request.ticket = nullptr;

    this->set_lock_for_table(table_locking->get_lock_descriptor(), cur_table);
    cur_table->table->reginfo.lock_type = cur_table->lock_descriptor().type;

    if (open_table_get_mdl_lock(thd, &ot_ctx, cur_table, 0, &mdl_ticket) ||
        mdl_ticket == NULL)
    {
        return true;
    }
    cur_table->table->mdl_ticket = mdl_ticket;

    return false;
}
