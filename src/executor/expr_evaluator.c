#include "src/executor/expr_evaluator.h"
#include "src/planner/value_bridge.h"
#include "src/common/mem.h"
#include <string.h>
#include <math.h>

/* Check if value is truthy (non-NULL and non-zero/non-false/non-empty) */
static int is_truthy(const value_t* v) {
    if (v->type == TYPE_NULL) return 0;
    switch (v->type) {
    case TYPE_INTEGER: return v->val.int_val != 0;
    case TYPE_FLOAT:   return v->val.float_val != 0.0;
    case TYPE_BOOLEAN: return v->val.bool_val;
    case TYPE_VARCHAR: return v->val.varchar && v->val.varchar[0] != '\0';
    default: return 0;
    }
}

int expr_evaluate(const expr_t* expr, const tuple_t* tuple, const schema_t* schema,
                  value_t* out) {
    if (!expr || !out) return DB_INVALID_ARGUMENT;

    switch (expr->type) {
    case EXPR_LITERAL: {
        *out = ast_value_to_storage(&expr->literal.value);
        return DB_OK;
    }

    case EXPR_COLUMN_REF: {
        if (!tuple || !schema) return DB_INVALID_ARGUMENT;
        int col_idx = schema_find_column(schema, expr->column_ref.column);
        if (col_idx < 0) return DB_UNKNOWN_COLUMN;
        const value_t* val = tuple_get_value(tuple, col_idx);
        if (!val) return DB_UNKNOWN_COLUMN;
        *out = value_copy(val);
        return DB_OK;
    }

    case EXPR_BINARY: {
        value_t left = value_make_null(), right = value_make_null();
        int rc = expr_evaluate(expr->binary.left, tuple, schema, &left);
        if (rc != DB_OK) return rc;
        rc = expr_evaluate(expr->binary.right, tuple, schema, &right);
        if (rc != DB_OK) { value_destroy(&left); return rc; }

        /* NULL propagation for arithmetic */
        if (left.type == TYPE_NULL || right.type == TYPE_NULL) {
            /* For AND/OR, use three-valued logic */
            if (expr->binary.op == TK_AND) {
                if (left.type == TYPE_BOOLEAN && !left.val.bool_val) {
                    *out = value_make_boolean(false);
                } else if (right.type == TYPE_BOOLEAN && !right.val.bool_val) {
                    *out = value_make_boolean(false);
                } else {
                    *out = value_make_null();
                }
                value_destroy(&left); value_destroy(&right);
                return DB_OK;
            }
            if (expr->binary.op == TK_OR) {
                if ((left.type == TYPE_BOOLEAN && left.val.bool_val) ||
                    (right.type == TYPE_BOOLEAN && right.val.bool_val)) {
                    *out = value_make_boolean(true);
                } else {
                    *out = value_make_null();
                }
                value_destroy(&left); value_destroy(&right);
                return DB_OK;
            }
            if (expr->binary.op == TK_EQUAL || expr->binary.op == TK_NOT_EQUAL) {
                /* NULL = anything → NULL (SQL standard) */
                *out = value_make_null();
                value_destroy(&left); value_destroy(&right);
                return DB_OK;
            }
            *out = value_make_null();
            value_destroy(&left); value_destroy(&right);
            return DB_OK;
        }

        switch (expr->binary.op) {
        case TK_PLUS:  rc = value_add(&left, &right, out); break;
        case TK_MINUS: rc = value_sub(&left, &right, out); break;
        case TK_STAR:  rc = value_mul(&left, &right, out); break;
        case TK_SLASH: rc = value_div(&left, &right, out); break;
        case TK_LESS: case TK_LESS_EQUAL: case TK_GREATER: case TK_GREATER_EQUAL:
        case TK_EQUAL: case TK_NOT_EQUAL: {
            int cmp = value_compare(&left, &right);
            if (cmp < -1) { *out = value_make_null(); rc = DB_OK; break; }
            int result = 0;
            switch (expr->binary.op) {
            case TK_LESS:          result = cmp < 0; break;
            case TK_LESS_EQUAL:    result = cmp <= 0; break;
            case TK_GREATER:       result = cmp > 0; break;
            case TK_GREATER_EQUAL: result = cmp >= 0; break;
            case TK_EQUAL:         result = cmp == 0; break;
            case TK_NOT_EQUAL:     result = cmp != 0; break;
            default: result = 0; break;
            }
            *out = value_make_boolean((bool)result);
            rc = DB_OK;
            break;
        }
        case TK_AND:
            *out = value_make_boolean(is_truthy(&left) && is_truthy(&right));
            rc = DB_OK;
            break;
        case TK_OR:
            *out = value_make_boolean(is_truthy(&left) || is_truthy(&right));
            rc = DB_OK;
            break;
        default:
            *out = value_make_null();
            rc = DB_OK;
            break;
        }
        value_destroy(&left); value_destroy(&right);
        return rc;
    }

    case EXPR_UNARY: {
        value_t operand = value_make_null();
        int rc = expr_evaluate(expr->unary.operand, tuple, schema, &operand);
        if (rc != DB_OK) return rc;

        switch (expr->unary.op) {
        case TK_MINUS:
            if (operand.type == TYPE_INTEGER) {
                *out = value_make_integer(-operand.val.int_val);
            } else if (operand.type == TYPE_FLOAT) {
                *out = value_make_float(-operand.val.float_val);
            } else {
                *out = value_make_null();
            }
            break;
        case TK_NOT:
            if (operand.type == TYPE_NULL) {
                *out = value_make_null();
            } else {
                *out = value_make_boolean(!is_truthy(&operand));
            }
            break;
        default:
            *out = value_make_null();
            break;
        }
        value_destroy(&operand);
        return DB_OK;
    }

    case EXPR_IS_NULL: {
        value_t operand = value_make_null();
        int rc = expr_evaluate(expr->is_null.operand, tuple, schema, &operand);
        if (rc != DB_OK) return rc;
        int result = (operand.type == TYPE_NULL);
        if (expr->is_null.is_not) result = !result;
        *out = value_make_boolean((bool)result);
        value_destroy(&operand);
        return DB_OK;
    }

    case EXPR_IN_EXPR: {
        value_t left = value_make_null();
        int rc = expr_evaluate(expr->in_expr.left, tuple, schema, &left);
        if (rc != DB_OK) return rc;
        if (left.type == TYPE_NULL) {
            *out = value_make_null();
            value_destroy(&left);
            return DB_OK;
        }
        int found = 0;
        for (int i = 0; i < expr->in_expr.list_count; i++) {
            value_t item = value_make_null();
            rc = expr_evaluate(expr->in_expr.list[i], tuple, schema, &item);
            if (rc != DB_OK) { value_destroy(&left); return rc; }
            if (item.type != TYPE_NULL && value_compare(&left, &item) == 0) {
                found = 1;
                value_destroy(&item);
                break;
            }
            value_destroy(&item);
        }
        if (expr->in_expr.is_not) found = !found;
        *out = value_make_boolean((bool)found);
        value_destroy(&left);
        return DB_OK;
    }

    case EXPR_BETWEEN: {
        value_t v = value_make_null(), low = value_make_null(), high = value_make_null();
        int rc = expr_evaluate(expr->between.expr, tuple, schema, &v);
        if (rc != DB_OK) return rc;
        rc = expr_evaluate(expr->between.low, tuple, schema, &low);
        if (rc != DB_OK) { value_destroy(&v); return rc; }
        rc = expr_evaluate(expr->between.high, tuple, schema, &high);
        if (rc != DB_OK) { value_destroy(&v); value_destroy(&low); return rc; }

        if (v.type == TYPE_NULL || low.type == TYPE_NULL || high.type == TYPE_NULL) {
            *out = value_make_null();
        } else {
            int cl = value_compare(&v, &low);
            int ch = value_compare(&v, &high);
            int result = (cl >= 0 && ch <= 0);
            if (expr->between.is_not) result = !result;
            *out = value_make_boolean((bool)result);
        }
        value_destroy(&v); value_destroy(&low); value_destroy(&high);
        return DB_OK;
    }

    case EXPR_LIKE: {
        value_t v = value_make_null();
        int rc = expr_evaluate(expr->like.expr, tuple, schema, &v);
        if (rc != DB_OK) return rc;
        if (v.type == TYPE_NULL) {
            *out = value_make_null();
            value_destroy(&v);
            return DB_OK;
        }
        /* Simple LIKE: only % and _ wildcards, no regex for now */
        /* For a basic implementation, treat pattern as a simple substring match
         * if it contains %. Full LIKE semantics can be added later. */
        const char* str = (v.type == TYPE_VARCHAR) ? v.val.varchar : "";
        const char* pat = expr->like.pattern;
        int match = 0;

        /* Quick implementation: if pattern is just "%text%", use strstr */
        if (pat[0] == '%' && pat[strlen(pat)-1] == '%') {
            char inner[256];
            strncpy(inner, pat + 1, strlen(pat) - 2);
            inner[strlen(pat) - 2] = '\0';
            match = (strstr(str, inner) != NULL);
        } else if (strcmp(pat, str) == 0) {
            match = 1;
        }

        if (expr->like.is_not) match = !match;
        *out = value_make_boolean((bool)match);
        value_destroy(&v);
        return DB_OK;
    }

    case EXPR_FUNCTION_CALL: {
        /* Aggregate functions are handled by HashAggregate executor.
         * Here we just return NULL for non-aggregate context. */
        *out = value_make_null();
        return DB_OK;
    }

    case EXPR_SUBQUERY: {
        /* Subqueries need the full execution engine — return NULL for now */
        *out = value_make_null();
        return DB_OK;
    }

    default:
        *out = value_make_null();
        return DB_OK;
    }
}
