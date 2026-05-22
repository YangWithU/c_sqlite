#include "src/planner/value_bridge.h"
#include "src/common/mem.h"
#include "src/common/error.h"
#include <string.h>

value_t ast_value_to_storage(const ast_value_t* ast_val) {
    if (!ast_val) return value_make_null();

    switch (ast_val->type) {
    case VALUE_NULL:   return value_make_null();
    case VALUE_BOOL:   return value_make_boolean((bool)ast_val->bool_val);
    case VALUE_INT:    return value_make_integer(ast_val->int_val);
    case VALUE_FLOAT:  return value_make_float(ast_val->float_val);
    case VALUE_STRING: return value_make_varchar(ast_val->str_val);
    default:          return value_make_null();
    }
}

type_id_t ast_col_type_to_storage(col_data_type_t t) {
    switch (t) {
    case COL_TYPE_INTEGER: return TYPE_INTEGER;
    case COL_TYPE_FLOAT:   return TYPE_FLOAT;
    case COL_TYPE_VARCHAR: return TYPE_VARCHAR;
    case COL_TYPE_BOOLEAN: return TYPE_BOOLEAN;
    default:               return TYPE_NULL;
    }
}

void ast_columns_to_storage(const ast_column_def_t* ast_cols, int count,
                            column_def_t* out_cols, int* out_count) {
    if (!ast_cols || !out_cols || !out_count) {
        if (out_count) *out_count = 0;
        return;
    }

    for (int i = 0; i < count; i++) {
        memset(&out_cols[i], 0, sizeof(column_def_t));
        strncpy(out_cols[i].name, ast_cols[i].name, MAX_COLUMN_NAME - 1);
        out_cols[i].name[MAX_COLUMN_NAME - 1] = '\0';
        out_cols[i].type = ast_col_type_to_storage(ast_cols[i].data_type);
        out_cols[i].nullable = !ast_cols[i].not_null;
        out_cols[i].max_length = (ast_cols[i].data_type == COL_TYPE_VARCHAR)
                                 ? (uint32_t)ast_cols[i].varchar_len : 0;
    }

    *out_count = count;
}

int ast_build_schema(const ast_column_def_t* ast_cols, int count, schema_t* out_schema) {
    if (!ast_cols || !out_schema || count <= 0)
        return DB_INVALID_ARGUMENT;

    column_def_t* storage_cols = db_malloc(sizeof(column_def_t) * count);
    if (!storage_cols) return DB_OUT_OF_MEMORY;

    int out_count = 0;
    ast_columns_to_storage(ast_cols, count, storage_cols, &out_count);

    int rc = schema_create(out_schema, storage_cols, (int32_t)out_count);
    db_free(storage_cols);
    return rc;
}
