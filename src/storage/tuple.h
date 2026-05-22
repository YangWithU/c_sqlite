#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/storage/value.h"
#include "src/storage/schema.h"
#include <stdint.h>

/* ========================================================================
 * Tuple: an ordered collection of Values conforming to a Schema.
 *
 * Disk format:
 *   [NULL bitmap (ceil(N/8) bytes)]
 *   [Fixed-length fields (contiguous, in column order)]
 *   [Variable-length fields: each preceded by 4B length prefix (uint32)]
 *
 * The NULL bitmap has one bit per column. Bit i = 1 means column i is NULL.
 * ======================================================================== */

typedef struct {
    value_t*  values;    /* array of values, one per column */
    int32_t   num_values;
    rid_t     rid;       /* record identifier (page_id, slot_id) */
} tuple_t;

/* Create a tuple with space for num_values values (all initialized to NULL). */
int  tuple_create(tuple_t* tuple, int32_t num_values);

/* Destroy a tuple, freeing all value resources and the values array. */
void tuple_destroy(tuple_t* tuple);

/* Set the value at column index. The tuple takes ownership (caller should
 * not destroy the value separately). Any previous value at that index is
 * destroyed first. */
int  tuple_set_value(tuple_t* tuple, int32_t col_idx, const value_t* val);

/* Get the value at column index. Returns a pointer into the tuple's own
 * value array (do not free). Returns NULL on error. */
const value_t* tuple_get_value(const tuple_t* tuple, int32_t col_idx);

/* Serialize a tuple into a byte buffer according to its schema.
 * Returns DB_OK on success. buf must be large enough (>= schema_max_tuple_size).
 * *out_size is set to the actual number of bytes written. */
int  tuple_serialize(const tuple_t* tuple, const schema_t* schema,
                     char* buf, uint32_t* out_size);

/* Deserialize a tuple from a byte buffer according to the schema.
 * Creates a new tuple with values populated from the buffer. */
int  tuple_deserialize(tuple_t* tuple, const schema_t* schema,
                       const char* buf, uint32_t buf_size);

/* Return the actual serialized size for this tuple (depends on VARCHAR lengths). */
uint32_t tuple_serialized_size(const tuple_t* tuple, const schema_t* schema);
