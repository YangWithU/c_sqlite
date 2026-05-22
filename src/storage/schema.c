#include "src/storage/schema.h"
#include "src/common/mem.h"

#include <string.h>
#include <stdlib.h>

int schema_create(schema_t* schema, const column_def_t* cols, int32_t num_columns) {
    if (!schema || !cols || num_columns <= 0)
        return DB_INVALID_ARGUMENT;

    schema->columns = (column_def_t*)db_malloc(sizeof(column_def_t) * num_columns);
    if (!schema->columns)
        return DB_OUT_OF_MEMORY;

    memcpy(schema->columns, cols, sizeof(column_def_t) * num_columns);
    schema->num_columns = num_columns;

    return schema_build(schema);
}

void schema_destroy(schema_t* schema) {
    if (schema) {
        if (schema->columns) {
            db_free(schema->columns);
            schema->columns = NULL;
        }
        schema->num_columns = 0;
    }
}

int schema_build(schema_t* schema) {
    if (!schema || !schema->columns || schema->num_columns <= 0)
        return DB_INVALID_ARGUMENT;

    uint32_t fixed_offset = 0;
    int32_t var_count = 0;

    for (int32_t i = 0; i < schema->num_columns; i++) {
        column_def_t* col = &schema->columns[i];
        if (col->type == TYPE_VARCHAR) {
            col->fixed_offset = 0;  /* not in fixed area */
            col->var_index = var_count++;
        } else {
            col->fixed_offset = fixed_offset;
            col->var_index = -1;
            fixed_offset += schema_column_fixed_size(col->type);
        }
    }

    schema->fixed_size = fixed_offset;
    schema->var_cols = (uint32_t)var_count;
    schema->null_bitmap_size = (schema->num_columns + 7) / 8;

    return DB_OK;
}

uint32_t schema_column_fixed_size(type_id_t type) {
    switch (type) {
    case TYPE_INTEGER: return sizeof(int64_t);   /* 8 */
    case TYPE_FLOAT:   return sizeof(double);     /* 8 */
    case TYPE_BOOLEAN: return sizeof(bool);       /* 1 */
    case TYPE_NULL:    return 0;
    case TYPE_VARCHAR: return 0;  /* variable length, not in fixed area */
    default:           return 0;
    }
}

uint32_t schema_column_max_serialized_size(const column_def_t* col) {
    if (!col)
        return 0;
    if (col->type == TYPE_VARCHAR)
        return col->max_length + sizeof(uint32_t);  /* data + 4B length prefix */
    return schema_column_fixed_size(col->type);
}

uint32_t schema_max_tuple_size(const schema_t* schema) {
    if (!schema)
        return 0;
    uint32_t size = schema->null_bitmap_size + schema->fixed_size;
    for (int32_t i = 0; i < schema->num_columns; i++) {
        if (schema->columns[i].type == TYPE_VARCHAR) {
            size += schema->columns[i].max_length + sizeof(uint32_t);
        }
    }
    return size;
}

int schema_find_column(const schema_t* schema, const char* name) {
    if (!schema || !name)
        return -1;
    for (int32_t i = 0; i < schema->num_columns; i++) {
        if (strcmp(schema->columns[i].name, name) == 0)
            return i;
    }
    return -1;
}
