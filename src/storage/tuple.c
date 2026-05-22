#include "src/storage/tuple.h"
#include "src/common/mem.h"
#include "src/common/macros.h"

#include <string.h>

/* ---- Static helpers (defined before first use) ---- */

/* Helper: check if col_idx is valid in the tuple */
static int col_idx_valid(const tuple_t* tuple, int32_t col_idx) {
    return tuple && col_idx >= 0 && col_idx < tuple->num_values;
}

/* Helper: check if a column value is NULL */
static int is_column_null(const tuple_t* tuple, int32_t col_idx) {
    if (!col_idx_valid(tuple, col_idx))
        return 1;
    return tuple->values[col_idx].type == TYPE_NULL;
}

/* ---- Public API ---- */

int tuple_create(tuple_t* tuple, int32_t num_values) {
    if (!tuple || num_values <= 0)
        return DB_INVALID_ARGUMENT;

    tuple->values = (value_t*)db_malloc(sizeof(value_t) * num_values);
    if (!tuple->values)
        return DB_OUT_OF_MEMORY;

    tuple->num_values = num_values;
    tuple->rid = INVALID_RID;

    /* Initialize all values to NULL */
    for (int32_t i = 0; i < num_values; i++) {
        tuple->values[i] = value_make_null();
    }

    return DB_OK;
}

void tuple_destroy(tuple_t* tuple) {
    if (tuple) {
        if (tuple->values) {
            for (int32_t i = 0; i < tuple->num_values; i++) {
                value_destroy(&tuple->values[i]);
            }
            db_free(tuple->values);
            tuple->values = NULL;
        }
        tuple->num_values = 0;
        tuple->rid = INVALID_RID;
    }
}

int tuple_set_value(tuple_t* tuple, int32_t col_idx, const value_t* val) {
    if (!tuple || !val)
        return DB_INVALID_ARGUMENT;
    if (col_idx < 0 || col_idx >= tuple->num_values)
        return DB_INVALID_ARGUMENT;

    /* Destroy the old value at this index */
    value_destroy(&tuple->values[col_idx]);

    /* Copy the new value (deep copy for VARCHAR) */
    tuple->values[col_idx] = value_copy(val);

    return DB_OK;
}

const value_t* tuple_get_value(const tuple_t* tuple, int32_t col_idx) {
    if (!tuple || col_idx < 0 || col_idx >= tuple->num_values)
        return NULL;
    return &tuple->values[col_idx];
}

uint32_t tuple_serialized_size(const tuple_t* tuple, const schema_t* schema) {
    if (!tuple || !schema)
        return 0;

    uint32_t size = schema->null_bitmap_size + schema->fixed_size;

    /* Variable-length fields: 4B length prefix + actual string length */
    for (int32_t i = 0; i < schema->num_columns; i++) {
        const column_def_t* col = &schema->columns[i];
        if (col->type == TYPE_VARCHAR) {
            if (col_idx_valid(tuple, i) && !is_column_null(tuple, i)) {
                const value_t* v = &tuple->values[i];
                uint32_t str_len = (v->type == TYPE_VARCHAR && v->val.varchar)
                                   ? (uint32_t)strlen(v->val.varchar) : 0;
                size += sizeof(uint32_t) + str_len;
            } else {
                size += sizeof(uint32_t);  /* just the length prefix (0) */
            }
        }
    }

    return size;
}

int tuple_serialize(const tuple_t* tuple, const schema_t* schema,
                    char* buf, uint32_t* out_size) {
    if (!tuple || !schema || !buf || !out_size)
        return DB_INVALID_ARGUMENT;

    uint32_t offset = 0;

    /* 1. NULL bitmap */
    uint32_t bitmap_size = schema->null_bitmap_size;
    memset(buf + offset, 0, bitmap_size);
    for (int32_t i = 0; i < schema->num_columns; i++) {
        if (is_column_null(tuple, i)) {
            buf[offset + i / 8] |= (1 << (i % 8));
        }
    }
    offset += bitmap_size;

    /* 2. Fixed-length fields */
    for (int32_t i = 0; i < schema->num_columns; i++) {
        const column_def_t* col = &schema->columns[i];
        if (col->type == TYPE_VARCHAR)
            continue;  /* variable fields go later */
        if (is_column_null(tuple, i))
            continue;  /* skip NULL values in fixed area */

        const value_t* v = &tuple->values[i];

        switch (col->type) {
        case TYPE_INTEGER:
            memcpy(buf + offset + col->fixed_offset, &v->val.int_val, sizeof(int64_t));
            break;
        case TYPE_FLOAT:
            memcpy(buf + offset + col->fixed_offset, &v->val.float_val, sizeof(double));
            break;
        case TYPE_BOOLEAN:
            memcpy(buf + offset + col->fixed_offset, &v->val.bool_val, sizeof(bool));
            break;
        default:
            break;
        }
    }
    offset += schema->fixed_size;

    /* 3. Variable-length fields with 4B length prefix */
    for (int32_t i = 0; i < schema->num_columns; i++) {
        const column_def_t* col = &schema->columns[i];
        if (col->type != TYPE_VARCHAR)
            continue;

        if (is_column_null(tuple, i)) {
            /* NULL varchar: write length = 0 */
            uint32_t len = 0;
            memcpy(buf + offset, &len, sizeof(uint32_t));
            offset += sizeof(uint32_t);
        } else {
            const value_t* v = &tuple->values[i];
            uint32_t str_len = (v->type == TYPE_VARCHAR && v->val.varchar)
                               ? (uint32_t)strlen(v->val.varchar) : 0;
            memcpy(buf + offset, &str_len, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (str_len > 0 && v->val.varchar) {
                memcpy(buf + offset, v->val.varchar, str_len);
                offset += str_len;
            }
        }
    }

    *out_size = offset;
    return DB_OK;
}

int tuple_deserialize(tuple_t* tuple, const schema_t* schema,
                      const char* buf, uint32_t buf_size) {
    if (!tuple || !schema || !buf)
        return DB_INVALID_ARGUMENT;

    int rc = tuple_create(tuple, schema->num_columns);
    if (rc != DB_OK)
        return rc;

    uint32_t offset = 0;

    /* 1. Read NULL bitmap */
    const char* bitmap = buf + offset;
    uint32_t bitmap_size = schema->null_bitmap_size;
    offset += bitmap_size;

    /* 2. Read fixed-length fields */
    for (int32_t i = 0; i < schema->num_columns; i++) {
        const column_def_t* col = &schema->columns[i];
        if (col->type == TYPE_VARCHAR)
            continue;

        /* Check if column is NULL via bitmap */
        if (bitmap[i / 8] & (1 << (i % 8))) {
            tuple->values[i] = value_make_null();
            continue;
        }

        switch (col->type) {
        case TYPE_INTEGER: {
            int64_t int_val;
            memcpy(&int_val, buf + offset + col->fixed_offset, sizeof(int64_t));
            tuple->values[i] = value_make_integer(int_val);
            break;
        }
        case TYPE_FLOAT: {
            double float_val;
            memcpy(&float_val, buf + offset + col->fixed_offset, sizeof(double));
            tuple->values[i] = value_make_float(float_val);
            break;
        }
        case TYPE_BOOLEAN: {
            bool bool_val;
            memcpy(&bool_val, buf + offset + col->fixed_offset, sizeof(bool));
            tuple->values[i] = value_make_boolean(bool_val);
            break;
        }
        default:
            break;
        }
    }
    offset += schema->fixed_size;

    /* 3. Read variable-length fields */
    for (int32_t i = 0; i < schema->num_columns; i++) {
        const column_def_t* col = &schema->columns[i];
        if (col->type != TYPE_VARCHAR)
            continue;

        /* Check if column is NULL via bitmap */
        if (bitmap[i / 8] & (1 << (i % 8))) {
            tuple->values[i] = value_make_null();
            /* Still need to read past the length prefix */
            uint32_t len;
            if (offset + sizeof(uint32_t) > buf_size)
                return DB_IO_ERROR;
            memcpy(&len, buf + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (len > 0)
                offset += len;
            continue;
        }

        if (offset + sizeof(uint32_t) > buf_size)
            return DB_IO_ERROR;

        uint32_t str_len;
        memcpy(&str_len, buf + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (str_len > 0) {
            if (offset + str_len > buf_size)
                return DB_IO_ERROR;
            /* Create a temporary null-terminated string */
            char* tmp = (char*)db_malloc(str_len + 1);
            if (!tmp)
                return DB_OUT_OF_MEMORY;
            memcpy(tmp, buf + offset, str_len);
            tmp[str_len] = '\0';
            tuple->values[i] = value_make_varchar(tmp);
            db_free(tmp);
            offset += str_len;
        } else {
            tuple->values[i] = value_make_varchar("");
        }
    }

    return DB_OK;
}
