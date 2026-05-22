#include "src/storage/value.h"
#include "src/common/mem.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

/* ---------- Constructors ---------- */

value_t value_make_integer(int64_t v) {
    value_t val;
    val.type = TYPE_INTEGER;
    val.val.int_val = v;
    return val;
}

value_t value_make_float(double v) {
    value_t val;
    val.type = TYPE_FLOAT;
    val.val.float_val = v;
    return val;
}

value_t value_make_varchar(const char* s) {
    value_t val;
    val.type = TYPE_VARCHAR;
    if (s) {
        size_t len = strlen(s) + 1;
        val.val.varchar = (char*)db_malloc(len);
        memcpy(val.val.varchar, s, len);
    } else {
        val.val.varchar = NULL;
    }
    return val;
}

value_t value_make_boolean(bool v) {
    value_t val;
    val.type = TYPE_BOOLEAN;
    val.val.bool_val = v;
    return val;
}

value_t value_make_null(void) {
    value_t val;
    val.type = TYPE_NULL;
    val.val.int_val = 0;
    return val;
}

value_t value_copy(const value_t* v) {
    if (!v) return value_make_null();
    if (v->type == TYPE_VARCHAR) {
        return value_make_varchar(v->val.varchar);
    }
    return *v;  /* shallow copy is fine for non-VARCHAR */
}

void value_destroy(value_t* v) {
    if (v && v->type == TYPE_VARCHAR && v->val.varchar) {
        db_free(v->val.varchar);
        v->val.varchar = NULL;
    }
}

/* ---------- Comparison ---------- */

int value_compare(const value_t* a, const value_t* b) {
    if (!a || !b) return DB_INVALID_ARGUMENT;

    /* NULL comparisons: NULL is always least */
    if (a->type == TYPE_NULL && b->type == TYPE_NULL) return 0;
    if (a->type == TYPE_NULL) return -1;
    if (b->type == TYPE_NULL) return 1;

    /* Same type comparison */
    if (a->type == b->type) {
        switch (a->type) {
        case TYPE_INTEGER:
            if (a->val.int_val < b->val.int_val) return -1;
            if (a->val.int_val > b->val.int_val) return 1;
            return 0;
        case TYPE_FLOAT:
            if (a->val.float_val < b->val.float_val) return -1;
            if (a->val.float_val > b->val.float_val) return 1;
            return 0;
        case TYPE_VARCHAR:
            if (!a->val.varchar && !b->val.varchar) return 0;
            if (!a->val.varchar) return -1;
            if (!b->val.varchar) return 1;
            return strcmp(a->val.varchar, b->val.varchar);
        case TYPE_BOOLEAN:
            if (a->val.bool_val == b->val.bool_val) return 0;
            return a->val.bool_val ? 1 : -1;
        default:
            return DB_TYPE_MISMATCH;
        }
    }

    /* Cross-type: INTEGER <-> FLOAT */
    if ((a->type == TYPE_INTEGER || a->type == TYPE_FLOAT) &&
        (b->type == TYPE_INTEGER || b->type == TYPE_FLOAT)) {
        double va = (a->type == TYPE_INTEGER) ? (double)a->val.int_val : a->val.float_val;
        double vb = (b->type == TYPE_INTEGER) ? (double)b->val.int_val : b->val.float_val;
        if (va < vb) return -1;
        if (va > vb) return 1;
        return 0;
    }

    return DB_TYPE_MISMATCH;
}

/* ---------- Arithmetic helpers ---------- */

/* Coerce a pair of values for arithmetic:
 * NULL -> error, INTEGER/INTEGER -> int, anything with FLOAT -> float.
 * Returns the coerced type (TYPE_INTEGER or TYPE_FLOAT) or negative error. */
static int coerce_arithmetic(const value_t* a, const value_t* b,
                             double* da, double* db, type_id_t* result_type) {
    if (a->type == TYPE_NULL || b->type == TYPE_NULL)
        return DB_TYPE_MISMATCH;
    if (a->type != TYPE_INTEGER && a->type != TYPE_FLOAT)
        return DB_TYPE_MISMATCH;
    if (b->type != TYPE_INTEGER && b->type != TYPE_FLOAT)
        return DB_TYPE_MISMATCH;

    *da = (a->type == TYPE_INTEGER) ? (double)a->val.int_val : a->val.float_val;
    *db = (b->type == TYPE_INTEGER) ? (double)b->val.int_val : b->val.float_val;

    *result_type = (a->type == TYPE_FLOAT || b->type == TYPE_FLOAT)
                       ? TYPE_FLOAT : TYPE_INTEGER;
    return DB_OK;
}

int value_add(const value_t* a, const value_t* b, value_t* out) {
    double da, db;
    type_id_t rt;
    int rc = coerce_arithmetic(a, b, &da, &db, &rt);
    if (rc != DB_OK) return rc;
    if (rt == TYPE_INTEGER)
        *out = value_make_integer((int64_t)da + (int64_t)db);
    else
        *out = value_make_float(da + db);
    return DB_OK;
}

int value_sub(const value_t* a, const value_t* b, value_t* out) {
    double da, db;
    type_id_t rt;
    int rc = coerce_arithmetic(a, b, &da, &db, &rt);
    if (rc != DB_OK) return rc;
    if (rt == TYPE_INTEGER)
        *out = value_make_integer((int64_t)da - (int64_t)db);
    else
        *out = value_make_float(da - db);
    return DB_OK;
}

int value_mul(const value_t* a, const value_t* b, value_t* out) {
    double da, db;
    type_id_t rt;
    int rc = coerce_arithmetic(a, b, &da, &db, &rt);
    if (rc != DB_OK) return rc;
    if (rt == TYPE_INTEGER)
        *out = value_make_integer((int64_t)da * (int64_t)db);
    else
        *out = value_make_float(da * db);
    return DB_OK;
}

int value_div(const value_t* a, const value_t* b, value_t* out) {
    double da, db;
    type_id_t rt;
    int rc = coerce_arithmetic(a, b, &da, &db, &rt);
    if (rc != DB_OK) return rc;
    if (rt == TYPE_INTEGER) {
        if ((int64_t)db == 0) return DB_INVALID_ARGUMENT;
        *out = value_make_integer((int64_t)da / (int64_t)db);
    } else {
        if (db == 0.0) return DB_INVALID_ARGUMENT;
        *out = value_make_float(da / db);
    }
    return DB_OK;
}

/* ---------- Type cast ---------- */

int value_cast_to(const value_t* v, type_id_t target, value_t* out) {
    if (v->type == target) {
        *out = value_copy(v);
        return DB_OK;
    }
    if (v->type == TYPE_NULL) {
        *out = value_make_null();
        return DB_OK;
    }

    switch (target) {
    case TYPE_INTEGER:
        if (v->type == TYPE_FLOAT) {
            *out = value_make_integer((int64_t)v->val.float_val);
            return DB_OK;
        }
        if (v->type == TYPE_BOOLEAN) {
            *out = value_make_integer(v->val.bool_val ? 1 : 0);
            return DB_OK;
        }
        if (v->type == TYPE_VARCHAR) {
            /* Attempt string-to-integer conversion */
            if (!v->val.varchar) return DB_TYPE_MISMATCH;
            char* end;
            int64_t iv = strtoll(v->val.varchar, &end, 10);
            if (*end != '\0' && *end != '.') return DB_TYPE_MISMATCH;
            *out = value_make_integer(iv);
            return DB_OK;
        }
        break;

    case TYPE_FLOAT:
        if (v->type == TYPE_INTEGER) {
            *out = value_make_float((double)v->val.int_val);
            return DB_OK;
        }
        if (v->type == TYPE_BOOLEAN) {
            *out = value_make_float(v->val.bool_val ? 1.0 : 0.0);
            return DB_OK;
        }
        if (v->type == TYPE_VARCHAR) {
            if (!v->val.varchar) return DB_TYPE_MISMATCH;
            char* end;
            double dv = strtod(v->val.varchar, &end);
            if (*end != '\0') return DB_TYPE_MISMATCH;
            *out = value_make_float(dv);
            return DB_OK;
        }
        break;

    case TYPE_BOOLEAN:
        if (v->type == TYPE_INTEGER) {
            *out = value_make_boolean(v->val.int_val != 0);
            return DB_OK;
        }
        if (v->type == TYPE_FLOAT) {
            *out = value_make_boolean(v->val.float_val != 0.0);
            return DB_OK;
        }
        break;

    case TYPE_VARCHAR:
        {
            char buf[64];
            switch (v->type) {
            case TYPE_INTEGER:
                snprintf(buf, sizeof(buf), "%lld", (long long)v->val.int_val);
                *out = value_make_varchar(buf);
                return DB_OK;
            case TYPE_FLOAT:
                snprintf(buf, sizeof(buf), "%g", v->val.float_val);
                *out = value_make_varchar(buf);
                return DB_OK;
            case TYPE_BOOLEAN:
                *out = value_make_varchar(v->val.bool_val ? "true" : "false");
                return DB_OK;
            default:
                break;
            }
        }
        break;

    case TYPE_NULL:
        *out = value_make_null();
        return DB_OK;

    default:
        break;
    }

    return DB_TYPE_MISMATCH;
}

/* ---------- Utilities ---------- */

const char* type_id_name(type_id_t t) {
    switch (t) {
    case TYPE_INTEGER: return "INTEGER";
    case TYPE_FLOAT:   return "FLOAT";
    case TYPE_VARCHAR: return "VARCHAR";
    case TYPE_BOOLEAN: return "BOOLEAN";
    case TYPE_NULL:    return "NULL";
    default:           return "UNKNOWN";
    }
}
