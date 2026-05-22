#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/config.h"
#include "src/storage/value.h"
#include "src/storage/schema.h"
#include "src/parser/ast.h"  /* now uses ast_value_t / ast_column_def_t */

/* Convert an AST literal value to a storage value.
 * For VALUE_STRING -> TYPE_VARCHAR (copies the string via value_make_varchar).
 * The caller must call value_destroy on the result. */
value_t ast_value_to_storage(const ast_value_t* ast_val);

/* Map parser column data type to storage type ID. */
type_id_t ast_col_type_to_storage(col_data_type_t t);

/* Convert an array of AST column definitions to storage column definitions.
 * out_cols must point to an array of at least count column_def_t (storage).
 * out_count is set to the number of output columns. */
void ast_columns_to_storage(const ast_column_def_t* ast_cols, int count,
                            column_def_t* out_cols, int* out_count);

/* Build a storage schema_t from AST column definitions.
 * Caller must call schema_destroy on the result. */
int ast_build_schema(const ast_column_def_t* ast_cols, int count, schema_t* out_schema);
