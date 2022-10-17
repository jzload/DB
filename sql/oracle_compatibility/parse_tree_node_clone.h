#ifndef PARSE_TREE_NODE_CLONE_H
#define PARSE_TREE_NODE_CLONE_H

#include "sql/sql_list.h"
class Parse_tree_transformer;
class MEM_ROOT;
class String;

class base_list_iterator_const {
 protected:
  const base_list *list;
  list_node* const *el;
  list_node* const *prev;
  const list_node *current;

 public:
  base_list_iterator_const() : list(0), el(0), prev(0), current(0) {}

  base_list_iterator_const(const base_list &list_par) { init(list_par); }

  inline void init(const base_list &list_par) {
    list = &list_par;
    el = &list_par.first;
    prev = 0;
    current = 0;
  }

  inline void *next(void) {
    prev = el;
    current = *el;
    el = &current->next;
    return current->info;
  }
  inline void *next_fast(void) {
    list_node *tmp;
    tmp = *el;
    el = &tmp->next;
    return tmp->info;
  }
  inline void rewind(void) { el = &list->first; }
  inline void* const *ref(void)  // Get reference pointer
  {
    return &current->info;
  }
  inline bool is_last(void) const { return el == list->last; }
  inline bool is_before_first() const { return current == NULL; }
};

template <class T>
class List_iterator_const : public base_list_iterator_const {
 public:
  List_iterator_const(const List<T> &a) : base_list_iterator_const(a) {}
  List_iterator_const() : base_list_iterator_const() {}
  inline void init(const List<T> &a) { base_list_iterator_const::init(a); }
  inline const T *operator++(int) { return (T *)base_list_iterator_const::next(); }
  inline void rewind(void) { base_list_iterator_const::rewind(); }
  inline const T **ref(void) { return (T **)base_list_iterator_const::ref(); }
};

template<class T>
List<T> *clone_list_with_ptt(MEM_ROOT* mem_root, const List<T> *src,
                    Parse_tree_transformer *ptt) {
  List<T> *ret = new(mem_root) List<T>;
  if (!ret)
    return nullptr;
  List_iterator_const<T> it(*src);
  const T *obj;
  T *obj_clone;
  while ((obj = it++)) {
    if (!(obj_clone = static_cast<T*>(obj->clone(ptt))))
      return nullptr;
    if (ret->push_back(obj_clone))
      return nullptr;
  }
  return ret;
}

List<String> *clone_String_list(MEM_ROOT* mem_root, const List<String> *src);

#endif /* PARSE_TREE_NODE_CLONE_H */