/*****************************************************************************

Copyright (c) 2005, 2019, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2012, Facebook Inc.
Copyright (c) 2021, JINZHUAN Information Technology Co., Ltd.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

/** @file page/zipdecompress.cc
 Page Decompression interface

 Created June 2005 by Marko Makela
 *******************************************************/

/** NOTE: The functions in this file should only use functions from
other files in library. The code in this file is used to make a library for
external tools. */

#include <stdarg.h>
#include <sys/types.h>
#include <zlib.h>

#include "btr0btr.h"
#include "mem0mem.h"
#include "page/zipdecompress.h"
#include "page0page.h"
#include "rem0rec.h"

/* Enable some extra debugging output.  This code can be enabled
independently of any UNIV_ debugging conditions. */
#if defined UNIV_DEBUG || defined UNIV_ZIP_DEBUG
#include <stdarg.h>

MY_ATTRIBUTE((format(printf, 1, 2)))
/** Report a failure to decompress or compress.
 @return number of characters printed */
int page_zip_fail_func(const char *fmt, /*!< in: printf(3) format string */
                       ...) /*!< in: arguments corresponding to fmt */
{
  int res;
  va_list ap;

  ut_print_timestamp(stderr);
  fputs("  InnoDB: ", stderr);
  va_start(ap, fmt);
  res = vfprintf(stderr, fmt, ap);
  va_end(ap);

  return (res);
}
/** Wrapper for page_zip_fail_func()
@param fmt_args in: printf(3) format string and arguments */
#define page_zip_fail(fmt_args) page_zip_fail_func fmt_args
#else                           /* UNIV_DEBUG || UNIV_ZIP_DEBUG */
/** Dummy wrapper for page_zip_fail_func()
@param fmt_args ignored: printf(3) format string and arguments */
#define page_zip_fail(fmt_args) /* empty */
#endif                          /* UNIV_DEBUG || UNIV_ZIP_DEBUG */

extern "C" {

/** Allocate memory for zlib. */
static void *page_zip_zalloc(void *opaque, /*!< in/out: memory heap */
                             uInt items, /*!< in: number of items to allocate */
                             uInt size)  /*!< in: size of an item in bytes */
{
  return (mem_heap_zalloc(static_cast<mem_heap_t *>(opaque), items * size));
}

/** Deallocate memory for zlib. */
static void page_zip_free(
    void *opaque MY_ATTRIBUTE((unused)),  /*!< in: memory heap */
    void *address MY_ATTRIBUTE((unused))) /*!< in: object to free */
{}

} /* extern "C" */

/** Deallocate the index information initialized by page_zip_fields_decode(). */
static void page_zip_fields_free(
    dict_index_t *index) /*!< in: dummy index to be freed */
{
  if (index) {
    dict_table_t *table = index->table;
#ifndef UNIV_HOTBACKUP
    dict_index_zip_pad_mutex_destroy(index);
#endif /* !UNIV_HOTBACKUP */
    mem_heap_free(index->heap);

    dict_mem_table_free(table);
  }
}

/** Configure the zlib allocator to use the given memory heap. */
void page_zip_set_alloc(void *stream,     /*!< in/out: zlib stream */
                        mem_heap_t *heap) /*!< in: memory heap to use */
{
  z_stream *strm = static_cast<z_stream *>(stream);

  strm->zalloc = page_zip_zalloc;
  strm->zfree = page_zip_free;
  strm->opaque = heap;
}

/** Populate the sparse page directory from the dense directory.
 @return true on success, false on failure */
static MY_ATTRIBUTE((warn_unused_result)) ibool page_zip_dir_decode(
    const page_zip_des_t *page_zip, /*!< in: dense page directory on
                                   compressed page */
    page_t *page,                   /*!< in: compact page with valid header;
                                    out: trailer and sparse page directory
                                    filled in */
    rec_t **recs,                   /*!< out: dense page directory sorted by
                                    ascending address (and heap_no) */
    ulint n_dense)                  /*!< in: number of user records, and
                                    size of recs[] */
{
  ulint i;
  ulint n_recs;
  byte *slot;

  n_recs = page_get_n_recs(page);

  if (UNIV_UNLIKELY(n_recs > n_dense)) {
    page_zip_fail(
        ("page_zip_dir_decode 1: %lu > %lu\n", (ulong)n_recs, (ulong)n_dense));
    return (FALSE);
  }

  /* Traverse the list of stored records in the sorting order,
  starting from the first user record. */

  slot = page + (UNIV_PAGE_SIZE - PAGE_DIR - PAGE_DIR_SLOT_SIZE);
  UNIV_PREFETCH_RW(slot);

  /* Zero out the page trailer. */
  memset(slot + PAGE_DIR_SLOT_SIZE, 0, PAGE_DIR);

  mach_write_to_2(slot, PAGE_NEW_INFIMUM);
  slot -= PAGE_DIR_SLOT_SIZE;
  UNIV_PREFETCH_RW(slot);

  /* Initialize the sparse directory and copy the dense directory. */
  for (i = 0; i < n_recs; i++) {
    ulint offs = page_zip_dir_get(page_zip, i);

    if (offs & PAGE_ZIP_DIR_SLOT_OWNED) {
      mach_write_to_2(slot, offs & PAGE_ZIP_DIR_SLOT_MASK);
      slot -= PAGE_DIR_SLOT_SIZE;
      UNIV_PREFETCH_RW(slot);
    }

    if (UNIV_UNLIKELY((offs & PAGE_ZIP_DIR_SLOT_MASK) <
                      PAGE_ZIP_START + REC_N_NEW_EXTRA_BYTES)) {
      page_zip_fail(("page_zip_dir_decode 2: %u %u %lx\n", (unsigned)i,
                     (unsigned)n_recs, (ulong)offs));
      return (FALSE);
    }

    recs[i] = page + (offs & PAGE_ZIP_DIR_SLOT_MASK);
  }

  mach_write_to_2(slot, PAGE_NEW_SUPREMUM);
  {
    const page_dir_slot_t *last_slot =
        page_dir_get_nth_slot(page, page_dir_get_n_slots(page) - 1);

    if (UNIV_UNLIKELY(slot != last_slot)) {
      page_zip_fail(("page_zip_dir_decode 3: %p != %p\n", (const void *)slot,
                     (const void *)last_slot));
      return (FALSE);
    }
  }

  /* Copy the rest of the dense directory. */
  for (; i < n_dense; i++) {
    ulint offs = page_zip_dir_get(page_zip, i);

    if (UNIV_UNLIKELY(offs & ~PAGE_ZIP_DIR_SLOT_MASK)) {
      page_zip_fail(("page_zip_dir_decode 4: %u %u %lx\n", (unsigned)i,
                     (unsigned)n_dense, (ulong)offs));
      return (FALSE);
    }

    recs[i] = page + offs;
  }

  std::sort(recs, recs + n_dense);
  return (TRUE);
}

/** Read the index information for the compressed page.
@param[in]	buf		index information
@param[in]	end		end of buf
@param[in]	trx_id_col	NULL for non-leaf pages; for leaf pages,
                                pointer to where to store the position of the
                                trx_id column
@param[in]	is_spatial	is spatial index or not
@return own: dummy index describing the page, or NULL on error */
static dict_index_t *page_zip_fields_decode(const byte *buf, const byte *end,
                                            ulint *trx_id_col,
                                            bool is_spatial) {
  const byte *b;
  ulint n;
  ulint i;
  ulint val;
  dict_table_t *table;
  dict_index_t *index;

  /* Determine the number of fields. */
  for (b = buf, n = 0; b < end; n++) {
    if (*b++ & 0x80) {
      b++; /* skip the second byte */
    }
  }

  n--; /* n_nullable or trx_id */

  if (UNIV_UNLIKELY(n > REC_MAX_N_FIELDS)) {
    page_zip_fail(("page_zip_fields_decode: n = %lu\n", (ulong)n));
    return (NULL);
  }

  if (UNIV_UNLIKELY(b > end)) {
    page_zip_fail(("page_zip_fields_decode: %p > %p\n", (const void *)b,
                   (const void *)end));
    return (NULL);
  }

  table = dict_mem_table_create("ZIP_DUMMY", DICT_HDR_SPACE, n, 0, 0,
                                DICT_TF_COMPACT, 0);
  index = dict_mem_index_create("ZIP_DUMMY", "ZIP_DUMMY", DICT_HDR_SPACE, 0, n);
  index->table = table;
  index->n_uniq = n;
  /* avoid ut_ad(index->cached) in dict_index_get_n_unique_in_tree */
  index->cached = TRUE;

  /* Initialize the fields. */
  for (b = buf, i = 0; i < n; i++) {
    ulint mtype;
    ulint len;

    val = *b++;

    if (UNIV_UNLIKELY(val & 0x80)) {
      /* fixed length > 62 bytes */
      val = (val & 0x7f) << 8 | *b++;
      len = val >> 1;
      mtype = DATA_FIXBINARY;
    } else if (UNIV_UNLIKELY(val >= 126)) {
      /* variable length with max > 255 bytes */
      len = 0x7fff;
      mtype = DATA_BINARY;
    } else if (val <= 1) {
      /* variable length with max <= 255 bytes */
      len = 0;
      mtype = DATA_BINARY;
    } else {
      /* fixed length < 62 bytes */
      len = val >> 1;
      mtype = DATA_FIXBINARY;
    }

    dict_mem_table_add_col(table, NULL, NULL, mtype,
                           val & 1 ? DATA_NOT_NULL : 0, len);

    /* The is_ascending flag does not matter during decompression,
    because we do not compare for "less than" or "greater than" */
    dict_index_add_col(index, table, table->get_col(i), 0, true);
  }

  val = *b++;
  if (UNIV_UNLIKELY(val & 0x80)) {
    val = (val & 0x7f) << 8 | *b++;
  }

  /* Decode the position of the trx_id column. */
  if (trx_id_col) {
    if (!val) {
      val = ULINT_UNDEFINED;
    } else if (UNIV_UNLIKELY(val >= n)) {
      page_zip_fields_free(index);
      index = NULL;
    } else {
      index->type = DICT_CLUSTERED;
    }

    *trx_id_col = val;
  } else {
    /* Decode the number of nullable fields. */
    if (UNIV_UNLIKELY(index->n_nullable > val)) {
      page_zip_fields_free(index);
      index = NULL;
    } else {
      index->n_nullable = val;
    }
  }

  ut_ad(b == end);

  if (is_spatial) {
    index->type |= DICT_SPATIAL;
  }

  index->n_instant_nullable = index->n_nullable;
  index->instant_cols =
      (index->is_clustered() && index->table->has_instant_cols());

  return (index);
}

/** Apply the modification log to a record containing externally stored
 columns.  Do not copy the fields that are stored separately.
 @return pointer to modification log, or NULL on failure */
static const byte *page_zip_apply_log_ext(
    rec_t *rec,           /*!< in/out: record */
    const ulint *offsets, /*!< in: rec_get_offsets(rec) */
    ulint trx_id_col,     /*!< in: position of of DB_TRX_ID */
    const byte *data,     /*!< in: modification log */
    const byte *end)      /*!< in: end of modification log */
{
  ulint i;
  ulint len;
  byte *next_out = rec;

  /* Check if there are any externally stored columns.
  For each externally stored column, skip the
  BTR_EXTERN_FIELD_REF. */

  for (i = 0; i < rec_offs_n_fields(offsets); i++) {
    byte *dst;

    if (UNIV_UNLIKELY(i == trx_id_col)) {
      /* Skip trx_id and roll_ptr */
      dst = rec_get_nth_field(rec, offsets, i, &len);
      if (UNIV_UNLIKELY(dst - next_out >= end - data) ||
          UNIV_UNLIKELY(len < (DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN)) ||
          rec_offs_nth_extern(offsets, i)) {
        page_zip_fail(
            ("page_zip_apply_log_ext:"
             " trx_id len %lu,"
             " %p - %p >= %p - %p\n",
             (ulong)len, (const void *)dst, (const void *)next_out,
             (const void *)end, (const void *)data));
        return (NULL);
      }

      memcpy(next_out, data, dst - next_out);
      data += dst - next_out;
      next_out = dst + (DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);
    } else if (rec_offs_nth_extern(offsets, i)) {
      dst = rec_get_nth_field(rec, offsets, i, &len);
      ut_ad(len >= BTR_EXTERN_FIELD_REF_SIZE);

      len += dst - next_out - BTR_EXTERN_FIELD_REF_SIZE;

      if (UNIV_UNLIKELY(data + len >= end)) {
        page_zip_fail(
            ("page_zip_apply_log_ext:"
             " ext %p+%lu >= %p\n",
             (const void *)data, (ulong)len, (const void *)end));
        return (NULL);
      }

      memcpy(next_out, data, len);
      data += len;
      next_out += len + BTR_EXTERN_FIELD_REF_SIZE;
    }
  }

  /* Copy the last bytes of the record. */
  len = rec_get_end(rec, offsets) - next_out;
  if (UNIV_UNLIKELY(data + len >= end)) {
    page_zip_fail(
        ("page_zip_apply_log_ext:"
         " last %p+%lu >= %p\n",
         (const void *)data, (ulong)len, (const void *)end));
    return (NULL);
  }
  memcpy(next_out, data, len);
  data += len;

  return (data);
}

/** Apply the modification log to an uncompressed page.
 Do not copy the fields that are stored separately.
 @return pointer to end of modification log, or NULL on failure */
static const byte *page_zip_apply_log(
    const byte *data, /*!< in: modification log */
    ulint size,       /*!< in: maximum length of the log, in bytes */
    rec_t **recs,     /*!< in: dense page directory,
                      sorted by address (indexed by
                      heap_no - PAGE_HEAP_NO_USER_LOW) */
    ulint n_dense,    /*!< in: size of recs[] */
    ulint trx_id_col, /*!< in: column number of trx_id in the index,
                   or ULINT_UNDEFINED if none */
    ulint heap_status,
    /*!< in: heap_no and status bits for
    the next record to uncompress */
    dict_index_t *index, /*!< in: index of the page */
    ulint *offsets)      /*!< in/out: work area for
                         rec_get_offsets_reverse() */
{
  const byte *const end = data + size;

  for (;;) {
    ulint val;
    rec_t *rec;
    ulint len;
    ulint hs;

    val = *data++;
    if (UNIV_UNLIKELY(!val)) {
      return (data - 1);
    }
    if (val & 0x80) {
      val = (val & 0x7f) << 8 | *data++;
      if (UNIV_UNLIKELY(!val)) {
        page_zip_fail(
            ("page_zip_apply_log:"
             " invalid val %x%x\n",
             data[-2], data[-1]));
        return (NULL);
      }
    }
    if (UNIV_UNLIKELY(data >= end)) {
      page_zip_fail(("page_zip_apply_log: %p >= %p\n", (const void *)data,
                     (const void *)end));
      return (NULL);
    }
    if (UNIV_UNLIKELY((val >> 1) > n_dense)) {
      page_zip_fail(
          ("page_zip_apply_log: %lu>>1 > %lu\n", (ulong)val, (ulong)n_dense));
      return (NULL);
    }

    /* Determine the heap number and status bits of the record. */
    rec = recs[(val >> 1) - 1];

    hs = ((val >> 1) + 1) << REC_HEAP_NO_SHIFT;
    hs |= heap_status & ((1 << REC_HEAP_NO_SHIFT) - 1);

    /* This may either be an old record that is being
    overwritten (updated in place, or allocated from
    the free list), or a new record, with the next
    available_heap_no. */
    if (UNIV_UNLIKELY(hs > heap_status)) {
      page_zip_fail(
          ("page_zip_apply_log: %lu > %lu\n", (ulong)hs, (ulong)heap_status));
      return (NULL);
    } else if (hs == heap_status) {
      /* A new record was allocated from the heap. */
      if (UNIV_UNLIKELY(val & 1)) {
        /* Only existing records may be cleared. */
        page_zip_fail(
            ("page_zip_apply_log:"
             " attempting to create"
             " deleted rec %lu\n",
             (ulong)hs));
        return (NULL);
      }
      heap_status += 1 << REC_HEAP_NO_SHIFT;
    }

    mach_write_to_2(rec - REC_NEW_HEAP_NO, hs);

    if (val & 1) {
      /* Clear the data bytes of the record. */
      mem_heap_t *heap = NULL;
      ulint *offs;
      offs = rec_get_offsets(rec, index, offsets, ULINT_UNDEFINED, &heap);
      memset(rec, 0, rec_offs_data_size(offs));

      if (UNIV_LIKELY_NULL(heap)) {
        mem_heap_free(heap);
      }
      continue;
    }

#if REC_STATUS_NODE_PTR != TRUE
#error "REC_STATUS_NODE_PTR != TRUE"
#endif
    rec_get_offsets_reverse(data, index, hs & REC_STATUS_NODE_PTR, offsets);
    rec_offs_make_valid(rec, index, offsets);

    /* Copy the extra bytes (backwards). */
    {
      byte *start = rec_get_start(rec, offsets);
      byte *b = rec - REC_N_NEW_EXTRA_BYTES;
      while (b != start) {
        *--b = *data++;
      }
    }

    /* Copy the data bytes. */
    if (UNIV_UNLIKELY(rec_offs_any_extern(offsets))) {
      /* Non-leaf nodes should not contain any
      externally stored columns. */
      if (UNIV_UNLIKELY(hs & REC_STATUS_NODE_PTR)) {
        page_zip_fail(
            ("page_zip_apply_log:"
             " %lu&REC_STATUS_NODE_PTR\n",
             (ulong)hs));
        return (NULL);
      }

      data = page_zip_apply_log_ext(rec, offsets, trx_id_col, data, end);

      if (UNIV_UNLIKELY(!data)) {
        return (NULL);
      }
    } else if (UNIV_UNLIKELY(hs & REC_STATUS_NODE_PTR)) {
      len = rec_offs_data_size(offsets) - REC_NODE_PTR_SIZE;
      /* Copy the data bytes, except node_ptr. */
      if (UNIV_UNLIKELY(data + len >= end)) {
        page_zip_fail(
            ("page_zip_apply_log:"
             " node_ptr %p+%lu >= %p\n",
             (const void *)data, (ulong)len, (const void *)end));
        return (NULL);
      }
      memcpy(rec, data, len);
      data += len;
    } else if (UNIV_LIKELY(trx_id_col == ULINT_UNDEFINED)) {
      len = rec_offs_data_size(offsets);

      /* Copy all data bytes of
      a record in a secondary index. */
      if (UNIV_UNLIKELY(data + len >= end)) {
        page_zip_fail(
            ("page_zip_apply_log:"
             " sec %p+%lu >= %p\n",
             (const void *)data, (ulong)len, (const void *)end));
        return (NULL);
      }

      memcpy(rec, data, len);
      data += len;
    } else {
      /* Skip DB_TRX_ID and DB_ROLL_PTR. */
      ulint l = rec_get_nth_field_offs(offsets, trx_id_col, &len);
      byte *b;

      if (UNIV_UNLIKELY(data + l >= end) ||
          UNIV_UNLIKELY(len < (DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN))) {
        page_zip_fail(
            ("page_zip_apply_log:"
             " trx_id %p+%lu >= %p\n",
             (const void *)data, (ulong)l, (const void *)end));
        return (NULL);
      }

      /* Copy any preceding data bytes. */
      memcpy(rec, data, l);
      data += l;

      /* Copy any bytes following DB_TRX_ID, DB_ROLL_PTR. */
      b = rec + l + (DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);
      len = rec_get_end(rec, offsets) - b;
      if (UNIV_UNLIKELY(data + len >= end)) {
        page_zip_fail(
            ("page_zip_apply_log:"
             " clust %p+%lu >= %p\n",
             (const void *)data, (ulong)len, (const void *)end));
        return (NULL);
      }
      memcpy(b, data, len);
      data += len;
    }
  }
}

/** Set the heap_no in a record, and skip the fixed-size record header
 that is not included in the d_stream.
 @return true on success, false if d_stream does not end at rec */
static ibool page_zip_decompress_heap_no(
    z_stream *d_stream, /*!< in/out: compressed page stream */
    rec_t *rec,         /*!< in/out: record */
    ulint &heap_status) /*!< in/out: heap_no and status bits */
{
  if (d_stream->next_out != rec - REC_N_NEW_EXTRA_BYTES) {
    /* n_dense has grown since the page was last compressed. */
    return (FALSE);
  }

  /* Skip the REC_N_NEW_EXTRA_BYTES. */
  d_stream->next_out = rec;

  /* Set heap_no and the status bits. */
  mach_write_to_2(rec - REC_NEW_HEAP_NO, heap_status);
  heap_status += 1 << REC_HEAP_NO_SHIFT;

  /* Clear the info bits, to make sure later assertion saying
  that this record is not instant can pass in rec_init_offsets() */
  rec[-REC_N_NEW_EXTRA_BYTES] = 0;

  return (TRUE);
}

/** Decompress the records of a node pointer page.
 @return true on success, false on failure */
static ibool page_zip_decompress_node_ptrs(
    page_zip_des_t *page_zip, /*!< in/out: compressed page */
    z_stream *d_stream,       /*!< in/out: compressed page stream */
    rec_t **recs,             /*!< in: dense page directory
                              sorted by address */
    ulint n_dense,            /*!< in: size of recs[] */
    dict_index_t *index,      /*!< in: the index of the page */
    ulint *offsets,           /*!< in/out: temporary offsets */
    mem_heap_t *heap)         /*!< in: temporary memory heap */
{
  ulint heap_status = REC_STATUS_NODE_PTR | PAGE_HEAP_NO_USER_LOW
                                                << REC_HEAP_NO_SHIFT;
  ulint slot;
  const byte *storage;

  /* Subtract the space reserved for uncompressed data. */
  d_stream->avail_in -=
      static_cast<uInt>(n_dense * (PAGE_ZIP_DIR_SLOT_SIZE + REC_NODE_PTR_SIZE));

  /* Decompress the records in heap_no order. */
  for (slot = 0; slot < n_dense; slot++) {
    rec_t *rec = recs[slot];

    d_stream->avail_out =
        static_cast<uInt>(rec - REC_N_NEW_EXTRA_BYTES - d_stream->next_out);

    ut_ad(d_stream->avail_out < UNIV_PAGE_SIZE - PAGE_ZIP_START - PAGE_DIR);
    switch (inflate(d_stream, Z_SYNC_FLUSH)) {
      case Z_STREAM_END:
        page_zip_decompress_heap_no(d_stream, rec, heap_status);
        goto zlib_done;
      case Z_OK:
      case Z_BUF_ERROR:
        if (!d_stream->avail_out) {
          break;
        }
        /* fall through */
      default:
        page_zip_fail(
            ("page_zip_decompress_node_ptrs:"
             " 1 inflate(Z_SYNC_FLUSH)=%s\n",
             d_stream->msg));
        goto zlib_error;
    }

    if (!page_zip_decompress_heap_no(d_stream, rec, heap_status)) {
      ut_ad(0);
    }

    /* Read the offsets. The status bits are needed here. */
    offsets = rec_get_offsets(rec, index, offsets, ULINT_UNDEFINED, &heap);

    /* Non-leaf nodes should not have any externally
    stored columns. */
    ut_ad(!rec_offs_any_extern(offsets));

    /* Decompress the data bytes, except node_ptr. */
    d_stream->avail_out =
        static_cast<uInt>(rec_offs_data_size(offsets) - REC_NODE_PTR_SIZE);

    switch (inflate(d_stream, Z_SYNC_FLUSH)) {
      case Z_STREAM_END:
        goto zlib_done;
      case Z_OK:
      case Z_BUF_ERROR:
        if (!d_stream->avail_out) {
          break;
        }
        /* fall through */
      default:
        page_zip_fail(
            ("page_zip_decompress_node_ptrs:"
             " 2 inflate(Z_SYNC_FLUSH)=%s\n",
             d_stream->msg));
        goto zlib_error;
    }

    /* Clear the node pointer in case the record
    will be deleted and the space will be reallocated
    to a smaller record. */
    memset(d_stream->next_out, 0, REC_NODE_PTR_SIZE);
    d_stream->next_out += REC_NODE_PTR_SIZE;

    ut_ad(d_stream->next_out == rec_get_end(rec, offsets));
  }

  /* Decompress any trailing garbage, in case the last record was
  allocated from an originally longer space on the free list. */
  d_stream->avail_out =
      static_cast<uInt>(page_header_get_field(page_zip->data, PAGE_HEAP_TOP) -
                        page_offset(d_stream->next_out));
  if (UNIV_UNLIKELY(d_stream->avail_out >
                    UNIV_PAGE_SIZE - PAGE_ZIP_START - PAGE_DIR)) {
    page_zip_fail(
        ("page_zip_decompress_node_ptrs:"
         " avail_out = %u\n",
         d_stream->avail_out));
    goto zlib_error;
  }

  if (UNIV_UNLIKELY(inflate(d_stream, Z_FINISH) != Z_STREAM_END)) {
    page_zip_fail(
        ("page_zip_decompress_node_ptrs:"
         " inflate(Z_FINISH)=%s\n",
         d_stream->msg));
  zlib_error:
    inflateEnd(d_stream);
    return (FALSE);
  }

  /* Note that d_stream->avail_out > 0 may hold here
  if the modification log is nonempty. */

zlib_done:
  if (UNIV_UNLIKELY(inflateEnd(d_stream) != Z_OK)) {
    ut_error;
  }

  {
    page_t *page = page_align(d_stream->next_out);

    /* Clear the unused heap space on the uncompressed page. */
    memset(d_stream->next_out, 0,
           page_dir_get_nth_slot(page, page_dir_get_n_slots(page) - 1) -
               d_stream->next_out);
  }

#ifdef UNIV_DEBUG
  page_zip->m_start = PAGE_DATA + d_stream->total_in;
#endif /* UNIV_DEBUG */

  /* Apply the modification log. */
  {
    const byte *mod_log_ptr;
    mod_log_ptr = page_zip_apply_log(d_stream->next_in, d_stream->avail_in + 1,
                                     recs, n_dense, ULINT_UNDEFINED,
                                     heap_status, index, offsets);

    if (UNIV_UNLIKELY(!mod_log_ptr)) {
      return (FALSE);
    }
    page_zip->m_end = mod_log_ptr - page_zip->data;
    page_zip->m_nonempty = mod_log_ptr != d_stream->next_in;
  }

  if (UNIV_UNLIKELY(page_zip_get_trailer_len(page_zip, index->is_clustered()) +
                        page_zip->m_end >=
                    page_zip_get_size(page_zip))) {
    page_zip_fail(
        ("page_zip_decompress_node_ptrs:"
         " %lu + %lu >= %lu, %lu\n",
         (ulong)page_zip_get_trailer_len(page_zip, index->is_clustered()),
         (ulong)page_zip->m_end, (ulong)page_zip_get_size(page_zip),
         (ulong)index->is_clustered()));
    return (FALSE);
  }

  /* Restore the uncompressed columns in heap_no order. */
  storage = page_zip_dir_start_low(page_zip, n_dense);

  for (slot = 0; slot < n_dense; slot++) {
    rec_t *rec = recs[slot];

    offsets = rec_get_offsets(rec, index, offsets, ULINT_UNDEFINED, &heap);
    /* Non-leaf nodes should not have any externally
    stored columns. */
    ut_ad(!rec_offs_any_extern(offsets));
    storage -= REC_NODE_PTR_SIZE;

    memcpy(rec_get_end(rec, offsets) - REC_NODE_PTR_SIZE, storage,
           REC_NODE_PTR_SIZE);
  }

  return (TRUE);
}

/** Decompress the records of a leaf node of a secondary index.
 @return true on success, false on failure */
static ibool page_zip_decompress_sec(
    page_zip_des_t *page_zip, /*!< in/out: compressed page */
    z_stream *d_stream,       /*!< in/out: compressed page stream */
    rec_t **recs,             /*!< in: dense page directory
                              sorted by address */
    ulint n_dense,            /*!< in: size of recs[] */
    dict_index_t *index,      /*!< in: the index of the page */
    ulint *offsets)           /*!< in/out: temporary offsets */
{
  ulint heap_status = REC_STATUS_ORDINARY | PAGE_HEAP_NO_USER_LOW
                                                << REC_HEAP_NO_SHIFT;
  ulint slot;

  ut_a(!index->is_clustered());

  /* Subtract the space reserved for uncompressed data. */
  d_stream->avail_in -= static_cast<uint>(n_dense * PAGE_ZIP_DIR_SLOT_SIZE);

  for (slot = 0; slot < n_dense; slot++) {
    rec_t *rec = recs[slot];

    /* Decompress everything up to this record. */
    d_stream->avail_out =
        static_cast<uint>(rec - REC_N_NEW_EXTRA_BYTES - d_stream->next_out);

    if (UNIV_LIKELY(d_stream->avail_out)) {
      switch (inflate(d_stream, Z_SYNC_FLUSH)) {
        case Z_STREAM_END:
          page_zip_decompress_heap_no(d_stream, rec, heap_status);
          goto zlib_done;
        case Z_OK:
        case Z_BUF_ERROR:
          if (!d_stream->avail_out) {
            break;
          }
          /* fall through */
        default:
          page_zip_fail(
              ("page_zip_decompress_sec:"
               " inflate(Z_SYNC_FLUSH)=%s\n",
               d_stream->msg));
          goto zlib_error;
      }
    }

    if (!page_zip_decompress_heap_no(d_stream, rec, heap_status)) {
      ut_ad(0);
    }
  }

  /* Decompress the data of the last record and any trailing garbage,
  in case the last record was allocated from an originally longer space
  on the free list. */
  d_stream->avail_out =
      static_cast<uInt>(page_header_get_field(page_zip->data, PAGE_HEAP_TOP) -
                        page_offset(d_stream->next_out));
  if (UNIV_UNLIKELY(d_stream->avail_out >
                    UNIV_PAGE_SIZE - PAGE_ZIP_START - PAGE_DIR)) {
    page_zip_fail(
        ("page_zip_decompress_sec:"
         " avail_out = %u\n",
         d_stream->avail_out));
    goto zlib_error;
  }

  if (UNIV_UNLIKELY(inflate(d_stream, Z_FINISH) != Z_STREAM_END)) {
    page_zip_fail(
        ("page_zip_decompress_sec:"
         " inflate(Z_FINISH)=%s\n",
         d_stream->msg));
  zlib_error:
    inflateEnd(d_stream);
    return (FALSE);
  }

  /* Note that d_stream->avail_out > 0 may hold here
  if the modification log is nonempty. */

zlib_done:
  if (UNIV_UNLIKELY(inflateEnd(d_stream) != Z_OK)) {
    ut_error;
  }

  {
    page_t *page = page_align(d_stream->next_out);

    /* Clear the unused heap space on the uncompressed page. */
    memset(d_stream->next_out, 0,
           page_dir_get_nth_slot(page, page_dir_get_n_slots(page) - 1) -
               d_stream->next_out);
  }

#ifdef UNIV_DEBUG
  page_zip->m_start = PAGE_DATA + d_stream->total_in;
#endif /* UNIV_DEBUG */

  /* Apply the modification log. */
  {
    const byte *mod_log_ptr;
    mod_log_ptr = page_zip_apply_log(d_stream->next_in, d_stream->avail_in + 1,
                                     recs, n_dense, ULINT_UNDEFINED,
                                     heap_status, index, offsets);

    if (UNIV_UNLIKELY(!mod_log_ptr)) {
      return (FALSE);
    }
    page_zip->m_end = mod_log_ptr - page_zip->data;
    page_zip->m_nonempty = mod_log_ptr != d_stream->next_in;
  }

  if (UNIV_UNLIKELY(page_zip_get_trailer_len(page_zip, FALSE) +
                        page_zip->m_end >=
                    page_zip_get_size(page_zip))) {
    page_zip_fail(("page_zip_decompress_sec: %lu + %lu >= %lu\n",
                   (ulong)page_zip_get_trailer_len(page_zip, FALSE),
                   (ulong)page_zip->m_end, (ulong)page_zip_get_size(page_zip)));
    return (FALSE);
  }

  /* There are no uncompressed columns on leaf pages of
  secondary indexes. */

  return (TRUE);
}

/** Initialize the REC_N_NEW_EXTRA_BYTES of each record.
 @return true on success, false on failure */
static ibool page_zip_set_extra_bytes(
    const page_zip_des_t *page_zip, /*!< in: compressed page */
    page_t *page,                   /*!< in/out: uncompressed page */
    ulint info_bits)                /*!< in: REC_INFO_MIN_REC_FLAG or 0 */
{
  ulint n;
  ulint i;
  ulint n_owned = 1;
  ulint offs;
  rec_t *rec;

  n = page_get_n_recs(page);
  rec = page + PAGE_NEW_INFIMUM;

  for (i = 0; i < n; i++) {
    offs = page_zip_dir_get(page_zip, i);

    if (offs & PAGE_ZIP_DIR_SLOT_DEL) {
      info_bits |= REC_INFO_DELETED_FLAG;
    }
    if (UNIV_UNLIKELY(offs & PAGE_ZIP_DIR_SLOT_OWNED)) {
      info_bits |= n_owned;
      n_owned = 1;
    } else {
      n_owned++;
    }
    offs &= PAGE_ZIP_DIR_SLOT_MASK;
    if (UNIV_UNLIKELY(offs < PAGE_ZIP_START + REC_N_NEW_EXTRA_BYTES)) {
      page_zip_fail(
          ("page_zip_set_extra_bytes 1:"
           " %u %u %lx\n",
           (unsigned)i, (unsigned)n, (ulong)offs));
      return (FALSE);
    }

    rec_set_next_offs_new(rec, offs);
    rec = page + offs;
    rec[-REC_N_NEW_EXTRA_BYTES] = (byte)info_bits;
    info_bits = 0;
  }

  /* Set the next pointer of the last user record. */
  rec_set_next_offs_new(rec, PAGE_NEW_SUPREMUM);

  /* Set n_owned of the supremum record. */
  page[PAGE_NEW_SUPREMUM - REC_N_NEW_EXTRA_BYTES] = (byte)n_owned;

  /* The dense directory excludes the infimum and supremum records. */
  n = page_dir_get_n_heap(page) - PAGE_HEAP_NO_USER_LOW;

  if (i >= n) {
    if (UNIV_LIKELY(i == n)) {
      return (TRUE);
    }

    page_zip_fail(
        ("page_zip_set_extra_bytes 2: %u != %u\n", (unsigned)i, (unsigned)n));
    return (FALSE);
  }

  offs = page_zip_dir_get(page_zip, i);

  /* Set the extra bytes of deleted records on the free list. */
  for (;;) {
    if (UNIV_UNLIKELY(!offs) || UNIV_UNLIKELY(offs & ~PAGE_ZIP_DIR_SLOT_MASK)) {
      page_zip_fail(("page_zip_set_extra_bytes 3: %lx\n", (ulong)offs));
      return (FALSE);
    }

    rec = page + offs;
    rec[-REC_N_NEW_EXTRA_BYTES] = 0; /* info_bits and n_owned */

    if (++i == n) {
      break;
    }

    offs = page_zip_dir_get(page_zip, i);
    rec_set_next_offs_new(rec, offs);
  }

  /* Terminate the free list. */
  rec[-REC_N_NEW_EXTRA_BYTES] = 0; /* info_bits and n_owned */
  rec_set_next_offs_new(rec, 0);

  return (TRUE);
}

/** Decompress a record of a leaf node of a clustered index that contains
 externally stored columns.
 @return true on success */
static ibool page_zip_decompress_clust_ext(
    z_stream *d_stream,   /*!< in/out: compressed page stream */
    rec_t *rec,           /*!< in/out: record */
    const ulint *offsets, /*!< in: rec_get_offsets(rec) */
    ulint trx_id_col)     /*!< in: position of of DB_TRX_ID */
{
  ulint i;

  for (i = 0; i < rec_offs_n_fields(offsets); i++) {
    ulint len;
    byte *dst;

    if (UNIV_UNLIKELY(i == trx_id_col)) {
      /* Skip trx_id and roll_ptr */
      dst = rec_get_nth_field(rec, offsets, i, &len);
      if (UNIV_UNLIKELY(len < DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN)) {
        page_zip_fail(
            ("page_zip_decompress_clust_ext:"
             " len[%lu] = %lu\n",
             (ulong)i, (ulong)len));
        return (FALSE);
      }

      if (rec_offs_nth_extern(offsets, i)) {
        page_zip_fail(
            ("page_zip_decompress_clust_ext:"
             " DB_TRX_ID at %lu is ext\n",
             (ulong)i));
        return (FALSE);
      }

      d_stream->avail_out = static_cast<uInt>(dst - d_stream->next_out);

      switch (inflate(d_stream, Z_SYNC_FLUSH)) {
        case Z_STREAM_END:
        case Z_OK:
        case Z_BUF_ERROR:
          if (!d_stream->avail_out) {
            break;
          }
          /* fall through */
        default:
          page_zip_fail(
              ("page_zip_decompress_clust_ext:"
               " 1 inflate(Z_SYNC_FLUSH)=%s\n",
               d_stream->msg));
          return (FALSE);
      }

      ut_ad(d_stream->next_out == dst);

      /* Clear DB_TRX_ID and DB_ROLL_PTR in order to
      avoid uninitialized bytes in case the record
      is affected by page_zip_apply_log(). */
      memset(dst, 0, DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);

      d_stream->next_out += DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN;
    } else if (rec_offs_nth_extern(offsets, i)) {
      dst = rec_get_nth_field(rec, offsets, i, &len);
      ut_ad(len >= BTR_EXTERN_FIELD_REF_SIZE);
      dst += len - BTR_EXTERN_FIELD_REF_SIZE;

      d_stream->avail_out = static_cast<uInt>(dst - d_stream->next_out);
      switch (inflate(d_stream, Z_SYNC_FLUSH)) {
        case Z_STREAM_END:
        case Z_OK:
        case Z_BUF_ERROR:
          if (!d_stream->avail_out) {
            break;
          }
          /* fall through */
        default:
          page_zip_fail(
              ("page_zip_decompress_clust_ext:"
               " 2 inflate(Z_SYNC_FLUSH)=%s\n",
               d_stream->msg));
          return (FALSE);
      }

      ut_ad(d_stream->next_out == dst);

      /* Clear the BLOB pointer in case
      the record will be deleted and the
      space will not be reused.  Note that
      the final initialization of the BLOB
      pointers (copying from "externs"
      or clearing) will have to take place
      only after the page modification log
      has been applied.  Otherwise, we
      could end up with an uninitialized
      BLOB pointer when a record is deleted,
      reallocated and deleted. */
      memset(d_stream->next_out, 0, BTR_EXTERN_FIELD_REF_SIZE);
      d_stream->next_out += BTR_EXTERN_FIELD_REF_SIZE;
    }
  }

  return (TRUE);
}

/** Compress the records of a leaf node of a clustered index.
 @return true on success, false on failure */
static ibool page_zip_decompress_clust(
    page_zip_des_t *page_zip, /*!< in/out: compressed page */
    z_stream *d_stream,       /*!< in/out: compressed page stream */
    rec_t **recs,             /*!< in: dense page directory
                              sorted by address */
    ulint n_dense,            /*!< in: size of recs[] */
    dict_index_t *index,      /*!< in: the index of the page */
    ulint trx_id_col,         /*!< index of the trx_id column */
    ulint *offsets,           /*!< in/out: temporary offsets */
    mem_heap_t *heap)         /*!< in: temporary memory heap */
{
  int err;
  ulint slot;
  ulint heap_status = REC_STATUS_ORDINARY | PAGE_HEAP_NO_USER_LOW
                                                << REC_HEAP_NO_SHIFT;
  const byte *storage;
  const byte *externs;

  ut_a(index->is_clustered());

  /* Subtract the space reserved for uncompressed data. */
  d_stream->avail_in -=
      static_cast<uInt>(n_dense) * (PAGE_ZIP_CLUST_LEAF_SLOT_SIZE);

  /* Decompress the records in heap_no order. */
  for (slot = 0; slot < n_dense; slot++) {
    rec_t *rec = recs[slot];

    d_stream->avail_out =
        static_cast<uInt>(rec - REC_N_NEW_EXTRA_BYTES - d_stream->next_out);

    ut_ad(d_stream->avail_out < UNIV_PAGE_SIZE - PAGE_ZIP_START - PAGE_DIR);
    err = inflate(d_stream, Z_SYNC_FLUSH);
    switch (err) {
      case Z_STREAM_END:
        page_zip_decompress_heap_no(d_stream, rec, heap_status);
        goto zlib_done;
      case Z_OK:
      case Z_BUF_ERROR:
        if (UNIV_LIKELY(!d_stream->avail_out)) {
          break;
        }
        /* fall through */
      default:
        page_zip_fail(
            ("page_zip_decompress_clust:"
             " 1 inflate(Z_SYNC_FLUSH)=%s\n",
             d_stream->msg));
        goto zlib_error;
    }

    if (!page_zip_decompress_heap_no(d_stream, rec, heap_status)) {
      ut_ad(0);
    }

    /* Read the offsets. The status bits are needed here. */
    offsets = rec_get_offsets(rec, index, offsets, ULINT_UNDEFINED, &heap);

    /* This is a leaf page in a clustered index. */

    /* Check if there are any externally stored columns.
    For each externally stored column, restore the
    BTR_EXTERN_FIELD_REF separately. */

    if (rec_offs_any_extern(offsets)) {
      if (UNIV_UNLIKELY(!page_zip_decompress_clust_ext(d_stream, rec, offsets,
                                                       trx_id_col))) {
        goto zlib_error;
      }
    } else {
      /* Skip trx_id and roll_ptr */
      ulint len;
      byte *dst = rec_get_nth_field(rec, offsets, trx_id_col, &len);
      if (UNIV_UNLIKELY(len < DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN)) {
        page_zip_fail(
            ("page_zip_decompress_clust:"
             " len = %lu\n",
             (ulong)len));
        goto zlib_error;
      }

      d_stream->avail_out = static_cast<uInt>(dst - d_stream->next_out);

      switch (inflate(d_stream, Z_SYNC_FLUSH)) {
        case Z_STREAM_END:
        case Z_OK:
        case Z_BUF_ERROR:
          if (!d_stream->avail_out) {
            break;
          }
          /* fall through */
        default:
          page_zip_fail(
              ("page_zip_decompress_clust:"
               " 2 inflate(Z_SYNC_FLUSH)=%s\n",
               d_stream->msg));
          goto zlib_error;
      }

      ut_ad(d_stream->next_out == dst);

      /* Clear DB_TRX_ID and DB_ROLL_PTR in order to
      avoid uninitialized bytes in case the record
      is affected by page_zip_apply_log(). */
      memset(dst, 0, DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);

      d_stream->next_out += DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN;
    }

    /* Decompress the last bytes of the record. */
    d_stream->avail_out =
        static_cast<uInt>(rec_get_end(rec, offsets) - d_stream->next_out);

    switch (inflate(d_stream, Z_SYNC_FLUSH)) {
      case Z_STREAM_END:
      case Z_OK:
      case Z_BUF_ERROR:
        if (!d_stream->avail_out) {
          break;
        }
        /* fall through */
      default:
        page_zip_fail(
            ("page_zip_decompress_clust:"
             " 3 inflate(Z_SYNC_FLUSH)=%s\n",
             d_stream->msg));
        goto zlib_error;
    }
  }

  /* Decompress any trailing garbage, in case the last record was
  allocated from an originally longer space on the free list. */
  d_stream->avail_out =
      static_cast<uInt>(page_header_get_field(page_zip->data, PAGE_HEAP_TOP) -
                        page_offset(d_stream->next_out));
  if (UNIV_UNLIKELY(d_stream->avail_out >
                    UNIV_PAGE_SIZE - PAGE_ZIP_START - PAGE_DIR)) {
    page_zip_fail(
        ("page_zip_decompress_clust:"
         " avail_out = %u\n",
         d_stream->avail_out));
    goto zlib_error;
  }

  if (UNIV_UNLIKELY(inflate(d_stream, Z_FINISH) != Z_STREAM_END)) {
    page_zip_fail(
        ("page_zip_decompress_clust:"
         " inflate(Z_FINISH)=%s\n",
         d_stream->msg));
  zlib_error:
    inflateEnd(d_stream);
    return (FALSE);
  }

  /* Note that d_stream->avail_out > 0 may hold here
  if the modification log is nonempty. */

zlib_done:
  if (UNIV_UNLIKELY(inflateEnd(d_stream) != Z_OK)) {
    ut_error;
  }

  {
    page_t *page = page_align(d_stream->next_out);

    /* Clear the unused heap space on the uncompressed page. */
    memset(d_stream->next_out, 0,
           page_dir_get_nth_slot(page, page_dir_get_n_slots(page) - 1) -
               d_stream->next_out);
  }

#ifdef UNIV_DEBUG
  page_zip->m_start = PAGE_DATA + d_stream->total_in;
#endif /* UNIV_DEBUG */

  /* Apply the modification log. */
  {
    const byte *mod_log_ptr;
    mod_log_ptr =
        page_zip_apply_log(d_stream->next_in, d_stream->avail_in + 1, recs,
                           n_dense, trx_id_col, heap_status, index, offsets);

    if (UNIV_UNLIKELY(!mod_log_ptr)) {
      return (FALSE);
    }
    page_zip->m_end = mod_log_ptr - page_zip->data;
    page_zip->m_nonempty = mod_log_ptr != d_stream->next_in;
  }

  if (UNIV_UNLIKELY(page_zip_get_trailer_len(page_zip, TRUE) +
                        page_zip->m_end >=
                    page_zip_get_size(page_zip))) {
    page_zip_fail(("page_zip_decompress_clust: %lu + %lu >= %lu\n",
                   (ulong)page_zip_get_trailer_len(page_zip, TRUE),
                   (ulong)page_zip->m_end, (ulong)page_zip_get_size(page_zip)));
    return (FALSE);
  }

  storage = page_zip_dir_start_low(page_zip, n_dense);

  externs = storage - n_dense * (DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);

  /* Restore the uncompressed columns in heap_no order. */

  for (slot = 0; slot < n_dense; slot++) {
    ulint i;
    ulint len;
    byte *dst;
    rec_t *rec = recs[slot];
    ibool exists = !page_zip_dir_find_free(page_zip, page_offset(rec));
    offsets = rec_get_offsets(rec, index, offsets, ULINT_UNDEFINED, &heap);

    dst = rec_get_nth_field(rec, offsets, trx_id_col, &len);
    ut_ad(len >= DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);
    storage -= DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN;
    memcpy(dst, storage, DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);

    /* Check if there are any externally stored
    columns in this record.  For each externally
    stored column, restore or clear the
    BTR_EXTERN_FIELD_REF. */
    if (!rec_offs_any_extern(offsets)) {
      continue;
    }

    for (i = 0; i < rec_offs_n_fields(offsets); i++) {
      if (!rec_offs_nth_extern(offsets, i)) {
        continue;
      }
      dst = rec_get_nth_field(rec, offsets, i, &len);

      if (UNIV_UNLIKELY(len < BTR_EXTERN_FIELD_REF_SIZE)) {
        page_zip_fail(
            ("page_zip_decompress_clust:"
             " %lu < 20\n",
             (ulong)len));
        return (FALSE);
      }

      dst += len - BTR_EXTERN_FIELD_REF_SIZE;

      if (UNIV_LIKELY(exists)) {
        /* Existing record:
        restore the BLOB pointer */
        externs -= BTR_EXTERN_FIELD_REF_SIZE;

        if (UNIV_UNLIKELY(externs < page_zip->data + page_zip->m_end)) {
          page_zip_fail(
              ("page_zip_"
               "decompress_clust:"
               " %p < %p + %lu\n",
               (const void *)externs, (const void *)page_zip->data,
               (ulong)page_zip->m_end));
          return (FALSE);
        }

        memcpy(dst, externs, BTR_EXTERN_FIELD_REF_SIZE);

        page_zip->n_blobs++;
      } else {
        /* Deleted record:
        clear the BLOB pointer */
        memset(dst, 0, BTR_EXTERN_FIELD_REF_SIZE);
      }
    }
  }

  return (TRUE);
}

/** Decompress a page.  This function should tolerate errors on the compressed
 page.  Instead of letting assertions fail, it will return FALSE if an
 inconsistency is detected.
 @return true on success, false on failure */
ibool page_zip_decompress_low(
    page_zip_des_t *page_zip, /*!< in: data, ssize;
                             out: m_start, m_end, m_nonempty, n_blobs */
    page_t *page,             /*!< out: uncompressed page, may be trashed */
    ibool all)                /*!< in: TRUE=decompress the whole page;
                              FALSE=verify but do not copy some
                              page header fields that should not change
                              after page creation */
{
  z_stream d_stream;
  dict_index_t *index = NULL;
  rec_t **recs;  /*!< dense page directory, sorted by address */
  ulint n_dense; /* number of user records on the page */
  ulint trx_id_col = ULINT_UNDEFINED;
  mem_heap_t *heap;
  ulint *offsets;

  ut_ad(page_zip_simple_validate(page_zip));
  UNIV_MEM_ASSERT_W(page, UNIV_PAGE_SIZE);
  UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));

  /* The dense directory excludes the infimum and supremum records. */
  n_dense = page_dir_get_n_heap(page_zip->data) - PAGE_HEAP_NO_USER_LOW;
  if (UNIV_UNLIKELY(n_dense * PAGE_ZIP_DIR_SLOT_SIZE >=
                    page_zip_get_size(page_zip))) {
    page_zip_fail(("page_zip_decompress 1: %lu %lu\n", (ulong)n_dense,
                   (ulong)page_zip_get_size(page_zip)));
    return (FALSE);
  }

  heap = mem_heap_create(n_dense * (3 * sizeof *recs) + UNIV_PAGE_SIZE);

  recs = static_cast<rec_t **>(mem_heap_alloc(heap, n_dense * sizeof *recs));

  if (all) {
    /* Copy the page header. */
    memcpy(page, page_zip->data, PAGE_DATA);
  } else {
    /* Check that the bytes that we skip are identical. */
#if defined UNIV_DEBUG || defined UNIV_ZIP_DEBUG
    ut_a(!memcmp(FIL_PAGE_TYPE + page, FIL_PAGE_TYPE + page_zip->data,
                 PAGE_HEADER - FIL_PAGE_TYPE));
    ut_a(!memcmp(PAGE_HEADER + PAGE_LEVEL + page,
                 PAGE_HEADER + PAGE_LEVEL + page_zip->data,
                 PAGE_DATA - (PAGE_HEADER + PAGE_LEVEL)));
#endif /* UNIV_DEBUG || UNIV_ZIP_DEBUG */

    /* Copy the mutable parts of the page header. */
    memcpy(page, page_zip->data, FIL_PAGE_TYPE);
    memcpy(PAGE_HEADER + page, PAGE_HEADER + page_zip->data,
           PAGE_LEVEL - PAGE_N_DIR_SLOTS);

#if defined UNIV_DEBUG || defined UNIV_ZIP_DEBUG
    /* Check that the page headers match after copying. */
    ut_a(!memcmp(page, page_zip->data, PAGE_DATA));
#endif /* UNIV_DEBUG || UNIV_ZIP_DEBUG */
  }

#ifdef UNIV_ZIP_DEBUG
  /* Clear the uncompressed page, except the header. */
  memset(PAGE_DATA + page, 0x55, UNIV_PAGE_SIZE - PAGE_DATA);
#endif /* UNIV_ZIP_DEBUG */
  UNIV_MEM_INVALID(PAGE_DATA + page, UNIV_PAGE_SIZE - PAGE_DATA);

  /* Copy the page directory. */
  if (UNIV_UNLIKELY(!page_zip_dir_decode(page_zip, page, recs, n_dense))) {
  zlib_error:
    mem_heap_free(heap);
    return (FALSE);
  }

  /* Copy the infimum and supremum records. */
  memcpy(page + (PAGE_NEW_INFIMUM - REC_N_NEW_EXTRA_BYTES), infimum_extra,
         sizeof infimum_extra);
  if (page_is_empty(page)) {
    rec_set_next_offs_new(page + PAGE_NEW_INFIMUM, PAGE_NEW_SUPREMUM);
  } else {
    rec_set_next_offs_new(
        page + PAGE_NEW_INFIMUM,
        page_zip_dir_get(page_zip, 0) & PAGE_ZIP_DIR_SLOT_MASK);
  }
  memcpy(page + PAGE_NEW_INFIMUM, infimum_data, sizeof infimum_data);
  memcpy(page + (PAGE_NEW_SUPREMUM - REC_N_NEW_EXTRA_BYTES + 1),
         supremum_extra_data, sizeof supremum_extra_data);

  page_zip_set_alloc(&d_stream, heap);

  d_stream.next_in = page_zip->data + PAGE_DATA;
  /* Subtract the space reserved for
  the page header and the end marker of the modification log. */
  d_stream.avail_in =
      static_cast<uInt>(page_zip_get_size(page_zip) - (PAGE_DATA + 1));
  d_stream.next_out = page + PAGE_ZIP_START;
  d_stream.avail_out = UNIV_PAGE_SIZE - PAGE_ZIP_START;

  if (UNIV_UNLIKELY(inflateInit2(&d_stream, UNIV_PAGE_SIZE_SHIFT) != Z_OK)) {
    ut_error;
  }

  /* Decode the zlib header and the index information. */
  if (UNIV_UNLIKELY(inflate(&d_stream, Z_BLOCK) != Z_OK)) {
    page_zip_fail(
        ("page_zip_decompress:"
         " 1 inflate(Z_BLOCK)=%s\n",
         d_stream.msg));
    goto zlib_error;
  }

  if (UNIV_UNLIKELY(inflate(&d_stream, Z_BLOCK) != Z_OK)) {
    page_zip_fail(
        ("page_zip_decompress:"
         " 2 inflate(Z_BLOCK)=%s\n",
         d_stream.msg));
    goto zlib_error;
  }

  index = page_zip_fields_decode(page + PAGE_ZIP_START, d_stream.next_out,
                                 page_is_leaf(page) ? &trx_id_col : NULL,
                                 fil_page_get_type(page) == FIL_PAGE_RTREE);

  if (UNIV_UNLIKELY(!index)) {
    goto zlib_error;
  }

  /* Decompress the user records. */
  page_zip->n_blobs = 0;
  d_stream.next_out = page + PAGE_ZIP_START;

  {
    /* Pre-allocate the offsets for rec_get_offsets_reverse(). */
    ulint n = 1 + 1 /* node ptr */ + REC_OFFS_HEADER_SIZE +
              dict_index_get_n_fields(index);

    offsets = static_cast<ulint *>(mem_heap_alloc(heap, n * sizeof(ulint)));

    *offsets = n;
  }

  /* Decompress the records in heap_no order. */
  if (!page_is_leaf(page)) {
    /* This is a node pointer page. */
    ulint info_bits;

    if (UNIV_UNLIKELY(!page_zip_decompress_node_ptrs(
            page_zip, &d_stream, recs, n_dense, index, offsets, heap))) {
      goto err_exit;
    }

    info_bits = mach_read_from_4(page + FIL_PAGE_PREV) == FIL_NULL
                    ? REC_INFO_MIN_REC_FLAG
                    : 0;

    if (UNIV_UNLIKELY(!page_zip_set_extra_bytes(page_zip, page, info_bits))) {
      goto err_exit;
    }
  } else if (UNIV_LIKELY(trx_id_col == ULINT_UNDEFINED)) {
    /* This is a leaf page in a secondary index. */
    if (UNIV_UNLIKELY(!page_zip_decompress_sec(page_zip, &d_stream, recs,
                                               n_dense, index, offsets))) {
      goto err_exit;
    }

    if (UNIV_UNLIKELY(!page_zip_set_extra_bytes(page_zip, page, 0))) {
    err_exit:
      page_zip_fields_free(index);
      mem_heap_free(heap);
      return (FALSE);
    }
  } else {
    /* This is a leaf page in a clustered index. */
    if (UNIV_UNLIKELY(!page_zip_decompress_clust(page_zip, &d_stream, recs,
                                                 n_dense, index, trx_id_col,
                                                 offsets, heap))) {
      goto err_exit;
    }

    if (UNIV_UNLIKELY(!page_zip_set_extra_bytes(page_zip, page, 0))) {
      goto err_exit;
    }
  }

  ut_a(page_is_comp(page));
  UNIV_MEM_ASSERT_RW(page, UNIV_PAGE_SIZE);

  page_zip_fields_free(index);
  mem_heap_free(heap);

  return (TRUE);
}
