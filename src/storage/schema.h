#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/config.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_VARCHAR_LEN  1024

/* Column definition */
typedef struct {
    char      name[MAX_COLUMN_NAME];
    type_id_t type;
    bool      nullable;       /* true = NULL allowed */
    uint32_t  max_length;     /* only for VARCHAR, 0 for other types */
    uint32_t  fixed_offset;   /* byte offset within fixed-field area (set by schema_build) */
    int32_t   var_index;      /* index among variable-length columns (-1 if fixed) */
} column_def_t;

/* Schema — an ordered list of column definitions with precomputed sizes */
typedef struct {
    column_def_t* columns;
    int32_t       num_columns;
    uint32_t      fixed_size;       /* total bytes of fixed-length fields */
    uint32_t      var_cols;         /* number of variable-length (VARCHAR) columns */
    uint32_t      null_bitmap_size; /* ceil(num_columns / 8) */
} schema_t;

/* Create a schema from an array of column definitions.
 * Calls schema_build to compute offsets and sizes. */
int  schema_create(schema_t* schema, const column_def_t* cols, int32_t num_columns);

/* Free schema resources. */
void schema_destroy(schema_t* schema);

/* Compute fixed offsets, var indices, and aggregate sizes.
 * Called automatically by schema_create, but can be called again after modifications. */
int  schema_build(schema_t* schema);

/* Return the fixed-size of a single column (0 for VARCHAR). */
uint32_t schema_column_fixed_size(type_id_t type);

/* Return the max serialized size for a column (for VARCHAR: max_length + 4 for length prefix). */
uint32_t schema_column_max_serialized_size(const column_def_t* col);

/* Total max serialized tuple size = null_bitmap + fixed + variable (with 4B length prefix each). */
uint32_t schema_max_tuple_size(const schema_t* schema);

/* Find a column by name; returns column index or -1. */
int  schema_find_column(const schema_t* schema, const char* name);
