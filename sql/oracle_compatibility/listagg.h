#ifndef SQL_ORACLE_COMPATIBILITY_LISTAGG_H_ABDKSGKDKSGKPS78512OYUITC
#define SQL_ORACLE_COMPATIBILITY_LISTAGG_H_ABDKSGKDKSGKPS78512OYUITC

#include "sql/item_sum.h"

class Item_func_listagg final : public Item_func_group_concat
{
    typedef Item_func_group_concat super;
public:
    Item_func_listagg(const POS& pos, PT_item_list* select_list, String* separator_arg,
        PT_order_list* opt_order_list, PT_window* w)
        : super(pos, false, select_list, opt_order_list, separator_arg, w)
    {
    }

    ~Item_func_listagg() override
    {
    }

    enum Sumfunctype sum_func() const override
    {
        return LISTAGG_FUNC;
    }

    const char* func_name() const override
    {
        return "listagg";
    }

    String* val_str(String* str) override;

    bool check_wf_semantics(THD* thd, SELECT_LEX* select,
        Window::Evaluation_requirements* reqs) override;

    void *clone(Parse_tree_transformer *ptt) const override;
};

#endif
