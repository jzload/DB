#include "sql/sql_class.h"
#include "sql/sql_list.h"
#include "sql/item.h"
#include "sql/item_row.h"
#include "sql/table.h"
#include "my_bitmap.h"
#include "sql/oracle_compatibility/update_set_multicol.h"
/****************************************************************************
 *  check row_item 's sub_item must be field item
 * *************************************************************************/
/**
  @param[in] item_row        the item row shoud to check
  @returns false if success, true if error
  @note The row item just use in update ,select and other do not do anything
*/
bool check_row_item (Item *item) {
  DBUG_ASSERT(item);
  Item *sub_item = nullptr;
  Item_row *item_row = down_cast<Item_row *>(item);
  uint n = item_row->cols();
  for (uint i = 0; i < n; i++) {
    sub_item = item_row->element_index(i);
    if (sub_item->type() != Item::FIELD_ITEM && sub_item->type() != Item::REF_ITEM) {
      my_error(ER_KEY_COLUMN_DOES_NOT_EXITS,  MYF(0), "expression");
      return true;
    }
  }
  return false;
}
/****************************************************************************
 *  put the value to field
 * *************************************************************************/
 /**
  @param thd                                  Thread handler.
  @param table                                Table reference.
  @param bitmap                               Bitmap over fields to fill.
  @param value                                the value to put
  @param fld                                  the fld where the value to put in
  @param insert_into_fields_bitmap            Bitmap for fields that is set
                                              in put_value_to_field.

  @param raise_autoinc_has_expl_non_null_val  Set corresponding flag in TABLE
                                              object to true if non-NULL value
                                              is explicitly assigned to
                                              auto-increment field.
  @return Operation status
      @retval false   OK
      @retval true    Error occurred
*/
inline bool put_value_to_field(THD *thd, TABLE *table, Item *value, Item *fld,
                        MY_BITMAP *bitmap, MY_BITMAP *insert_into_fields_bitmap,
                        bool raise_autoinc_has_expl_non_null_val) {
  Item_field *const field = fld->field_for_view_update();
  DBUG_ASSERT(field != NULL && field->table_ref->table == table);

  Field *const rfield = field->field;
  /* If bitmap over wanted fields are set, skip non marked fields. */
  if (bitmap && !bitmap_is_set(bitmap, rfield->field_index)) return false;

  bitmap_set_bit(table->fields_set_during_insert, rfield->field_index);
  if (insert_into_fields_bitmap)
    bitmap_set_bit(insert_into_fields_bitmap, rfield->field_index);

  /* Generated columns will be filled after all base columns are done. */
  if (rfield->is_gcol()) return false;

  if (raise_autoinc_has_expl_non_null_val &&
      rfield == table->next_number_field)
    table->autoinc_field_has_explicit_non_null_value = true;

  (void)value->save_in_field(rfield, false);
  if (thd->is_error()) return true;
  return false;
}

/****************************************************************************
 *  fill all the fields with values
 * *************************************************************************/
 /**
  @param thd                                  Thread handler.
  @param table                                Table reference.
  @param fields_iter                          fields list iterator
  @param val_iter                             values list iterator
  @param bitmap                               Bitmap over fields to fill.
  @param insert_into_fields_bitmap            Bitmap for fields that is set
                                              in put_value_to_field.

  @param raise_autoinc_has_expl_non_null_val  Set corresponding flag in TABLE
                                              object to true if non-NULL value
                                              is explicitly assigned to
                                              auto-increment field.
  @return Operation status
      @retval false   OK
      @retval true    Error occurred
*/
bool fill_fields(THD *thd, TABLE *table, List<Item> &fields, List<Item> &values,
                 MY_BITMAP *bitmap, MY_BITMAP *insert_into_fields_bitmap,
                 bool raise_autoinc_has_expl_non_null_val){
  Item *fld;
  LIST_ITER fields_iter(fields), val_iter(values);
  while ((fld = fields_iter++)) {
    switch (fld->type()) {
      case Item::ROW_ITEM: {
        Item_row *const item_row = down_cast<Item_row *>(fld);
        Item *const value = val_iter++;
        uint n = item_row->cols();
        if (n != value->cols()) {
          my_error(ER_OPERAND_COLUMNS, MYF(0), value->cols());
          return true;
        }
        value->bring_value();
        Item *fld_in = NULL;
        Item *fld_val = NULL;
        for (uint i = 0; i < n; i++) {
            fld_in = item_row->element_index(i);
            fld_val = value->element_index(i);
            if (put_value_to_field(thd, table, fld_val, fld_in, bitmap,
                                   insert_into_fields_bitmap,
                                   raise_autoinc_has_expl_non_null_val)) {
              return  true;
            }
        }
      }
      break;
      case Item::FIELD_ITEM:
      default: {
        Item *const value = val_iter++;
        if (put_value_to_field(thd, table, value, fld, bitmap,
                               insert_into_fields_bitmap,
                               raise_autoinc_has_expl_non_null_val)) {
          return  true;
        }
      }
      break;
    }
  }
  return false;
}