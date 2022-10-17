#include "listagg.h"

String* Item_func_listagg::val_str(String* str) 
{
    if (m_is_window_function)
    {
        if (wf_common_init()) return str;

        if (add()) return str;

        set_wf_args();
    }

    return Item_func_group_concat::val_str(str);
}

bool Item_func_listagg::check_wf_semantics(THD* thd, SELECT_LEX* select,
    Window::Evaluation_requirements* reqs)
{
    if (Item_sum::check_wf_semantics(thd, select, reqs)) return true;

    return setup(thd);
}
