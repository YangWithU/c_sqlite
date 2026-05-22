#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * Tagged-union Value type.
 *
 *   type_id_t tag  determines which union field is active:
 *     TYPE_INTEGER  -> val.int_val   (int64_t)
 *     TYPE_FLOAT    -> val.float_val (double)
 *     TYPE_VARCHAR  -> val.varchar   (char*, separately malloc'd)
 *     TYPE_BOOLEAN  -> val.bool_val  (bool)
 *     TYPE_NULL     -> (no data)
 * ======================================================================== */

typedef struct {
    type_id_t type;
    union {
        int64_t  int_val;
        double   float_val;
        char*    varchar;
        bool     bool_val;
    } val;
} value_t;

/* ---------- Constructors ---------- */

value_t value_make_integer(int64_t v);
value_t value_make_float(double v);
value_t value_make_varchar(const char* s);   /* copies the string */
value_t value_make_boolean(bool v);
value_t value_make_null(void);

/* Deep copy — caller must value_destroy the copy. */
value_t value_copy(const value_t* v);

/* Free any heap allocation (VARCHAR string).  Does NOT free the value_t itself. */
void value_destroy(value_t* v);

/* ---------- Comparison ----------
 * Returns -1, 0, +1.  Returns negative error for incompatible types. */
int value_compare(const value_t* a, const value_t* b);

/* ---------- Arithmetic ----------
 * Result is placed in *out.  Returns DB_OK or DB_TYPE_MISMATCH. */
int value_add(const value_t* a, const value_t* b, value_t* out);
int value_sub(const value_t* a, const value_t* b, value_t* out);
int value_mul(const value_t* a, const value_t* b, value_t* out);
int value_div(const value_t* a, const value_t* b, value_t* out);

/* ---------- Type cast ----------
 * Cast value to target type.  Returns DB_OK or DB_TYPE_MISMATCH. */
int value_cast_to(const value_t* v, type_id_t target, value_t* out);

/* ---------- Utilities ---------- */

const char* type_id_name(type_id_t t);
