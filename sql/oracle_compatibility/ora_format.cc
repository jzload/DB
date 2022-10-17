#include "m_ctype.h"           // CHARSET_INFO
#include "sql/sql_class.h"     // THD
#include "sql/current_thd.h"   // current_thd
#include "sql/oracle_compatibility/ora_format.h"


/*
 * Fast sequential search, use index for data selection which
 * go to seq. cycle (it is very fast for unwanted strings)
 * (can't be used binary search in format parsing)
 *
 * str(in):
 * kw(in): KeyWord, ie, DTF_keywords
 * index(in): KeyWords index, ie, DTF_index
 *
 * return:
 *   key word or NULL
 */
const KeyWord *index_seq_search(const char *str, const KeyWord *kw,
                                const int *index)
{
  int            poz;

  if (!KeyWord_INDEX_FILTER(*str)) {
    return NULL;
  }

  if ((poz = *(index + (*str - ' '))) > -1) {
    const KeyWord *k = kw + poz;

    do {
      if (strncasecmp(str, k->name, k->len) == 0) {
        return k;
      }

      k++;
      if (!k->name) {
        return NULL;
      }
    } while (*str == *k->name);
  }

  return NULL;
}

/* Search the str in KeySuffix.
 *
 * str(in):
 * suf(in): key suffix, ie, DTF_suff
 * type(in): suffix type
 *
 * return:
 *   KeySuffix or NULL
 */
const KeySuffix *suff_search(const char *str, const KeySuffix *suf,
                             SuffixType type)
{
  const KeySuffix *s;

  for (s = suf; s->name != NULL; s++) {
    if (s->type != type) {
      continue;
    }

    if (strncmp(str, s->name, s->len) == 0){
      return s;
    }
  }

  return NULL;
}

