#include "src/executor/hash_agg_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/value.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"
#include "src/common/hashmap.h"
#include <string.h>

/* Aggregate types */
typedef enum { AGG_COUNT, AGG_SUM, AGG_AVG, AGG_MIN, AGG_MAX } agg_kind_t;

typedef struct {
    int64_t count;
    value_t sum;    /* for SUM/AVG */
    value_t min;    /* for MIN */
    value_t max;    /* for MAX */
    int    has_value;
} agg_state_t;

typedef struct {
    tuple_t*  group_key;    /* the group-by key tuple */
    agg_state_t* agg_states;
    int        agg_count;
} agg_group_t;

typedef struct {
    expr_t**   group_by;
    int        group_count;
    expr_t**   aggregates;
    int        agg_count;
    agg_group_t* groups;
    int        group_capacity;
    int        group_used;
    int        emit_index;
    int        materialized;
} hash_agg_state_t;

static agg_kind_t get_agg_kind(const expr_t* expr) {
    if (expr->type == EXPR_FUNCTION_CALL) {
        if (strcmp(expr->func_call.name, "COUNT") == 0) return AGG_COUNT;
        if (strcmp(expr->func_call.name, "SUM") == 0) return AGG_SUM;
        if (strcmp(expr->func_call.name, "AVG") == 0) return AGG_AVG;
        if (strcmp(expr->func_call.name, "MIN") == 0) return AGG_MIN;
        if (strcmp(expr->func_call.name, "MAX") == 0) return AGG_MAX;
    }
    return AGG_COUNT;
}

static int hash_agg_init(executor_t* self) {
    hash_agg_state_t* state = (hash_agg_state_t*)self->state;
    state->materialized = 0;
    state->emit_index = 0;
    if (self->child_count > 0)
        return self->children[0]->init(self->children[0]);
    return DB_OK;
}

/* Simple group key comparison: compare all values in the key tuple */
static int group_key_equal(const tuple_t* a, const tuple_t* b) {
    if (a->num_values != b->num_values) return 0;
    for (int i = 0; i < a->num_values; i++) {
        const value_t* va = tuple_get_value(a, i);
        const value_t* vb = tuple_get_value(b, i);
        if (va->type != vb->type) return 0;
        switch (va->type) {
        case TYPE_INTEGER: if (va->val.int_val != vb->val.int_val) return 0; break;
        case TYPE_FLOAT:   if (va->val.float_val != vb->val.float_val) return 0; break;
        case TYPE_BOOLEAN: if (va->val.bool_val != vb->val.bool_val) return 0; break;
        case TYPE_VARCHAR: if (strcmp(va->val.varchar, vb->val.varchar) != 0) return 0; break;
        case TYPE_NULL: break;
        default: return 0;
        }
    }
    return 1;
}

static void update_agg(agg_state_t* agg, const value_t* val, agg_kind_t kind) {
    agg->count++;
    if (val->type == TYPE_NULL && kind == AGG_COUNT) return;
    if (val->type == TYPE_NULL) return;

    if (!agg->has_value) {
        agg->min = value_copy(val);
        agg->max = value_copy(val);
        agg->sum = value_copy(val);
        agg->has_value = 1;
        return;
    }

    /* Update min/max */
    if (kind == AGG_MIN || kind == AGG_MAX || kind == AGG_AVG || kind == AGG_SUM) {
        int cmp = value_compare(val, &agg->min);
        if (cmp < 0) { value_destroy(&agg->min); agg->min = value_copy(val); }
        cmp = value_compare(val, &agg->max);
        if (cmp > 0) { value_destroy(&agg->max); agg->max = value_copy(val); }
    }

    /* Update sum */
    if (kind == AGG_SUM || kind == AGG_AVG) {
        value_t result = value_make_null();
        value_add(&agg->sum, val, &result);
        value_destroy(&agg->sum);
        agg->sum = result;
    }
}

static int materialize(executor_t* self) {
    hash_agg_state_t* state = (hash_agg_state_t*)self->state;
    executor_t* child = self->children[0];
    tuple_t tuple;

    state->group_capacity = 64;
    state->groups = db_calloc(state->group_capacity, sizeof(agg_group_t));
    state->group_used = 0;

    while (child->next(child, &tuple) == DB_OK) {
        /* Compute group key */
        tuple_t key;
        tuple_create(&key, state->group_count > 0 ? state->group_count : 1);
        for (int i = 0; i < state->group_count; i++) {
            value_t val = value_make_null();
            expr_evaluate(state->group_by[i], &tuple, child->output_schema, &val);
            tuple_set_value(&key, i, &val);
            value_destroy(&val);
        }

        /* Find or create group */
        int found = -1;
        for (int g = 0; g < state->group_used; g++) {
            if (group_key_equal(state->groups[g].group_key, &key)) {
                found = g;
                break;
            }
        }

        if (found < 0) {
            if (state->group_used >= state->group_capacity) {
                state->group_capacity *= 2;
                state->groups = db_realloc(state->groups,
                                           state->group_capacity * sizeof(agg_group_t));
            }
            found = state->group_used++;
            state->groups[found].group_key = db_malloc(sizeof(tuple_t));
            tuple_create(state->groups[found].group_key, key.num_values);
            for (int i = 0; i < key.num_values; i++) {
                const value_t* v = tuple_get_value(&key, i);
                if (v) {
                    value_t copy = value_copy(v);
                    tuple_set_value(state->groups[found].group_key, i, &copy);
                    value_destroy(&copy);
                }
            }
            state->groups[found].agg_states = db_calloc(state->agg_count, sizeof(agg_state_t));
            state->groups[found].agg_count = state->agg_count;
        }

        /* Update aggregates for this group */
        for (int a = 0; a < state->agg_count; a++) {
            agg_kind_t kind = get_agg_kind(state->aggregates[a]);
            value_t val = value_make_null();
            /* For COUNT(*), don't evaluate expression */
            if (kind == AGG_COUNT && state->aggregates[a]->type == EXPR_FUNCTION_CALL &&
                state->aggregates[a]->func_call.is_star) {
                val = value_make_integer(1);
            } else if (state->aggregates[a]->type == EXPR_FUNCTION_CALL &&
                       state->aggregates[a]->func_call.arg_count > 0) {
                expr_evaluate(state->aggregates[a]->func_call.args[0], &tuple,
                             child->output_schema, &val);
            }
            update_agg(&state->groups[found].agg_states[a], &val, kind);
            value_destroy(&val);
        }

        tuple_destroy(&key);
        tuple_destroy(&tuple);
    }

    child->close(child);
    state->materialized = 1;
    return DB_OK;
}

static int hash_agg_next(executor_t* self, tuple_t* out) {
    hash_agg_state_t* state = (hash_agg_state_t*)self->state;

    if (!state->materialized) {
        int rc = materialize(self);
        if (rc != DB_OK) return rc;
    }

    if (state->emit_index >= state->group_used)
        return DB_PAGE_NOT_FOUND;

    int g = state->emit_index++;
    int total_cols = state->group_count + state->agg_count;
    tuple_create(out, total_cols);

    /* Copy group key */
    for (int i = 0; i < state->group_count; i++) {
        const value_t* v = tuple_get_value(state->groups[g].group_key, i);
        if (v) {
            value_t copy = value_copy(v);
            tuple_set_value(out, i, &copy);
            value_destroy(&copy);
        }
    }

    /* Produce aggregate results */
    for (int a = 0; a < state->agg_count; a++) {
        agg_kind_t kind = get_agg_kind(state->aggregates[a]);
        value_t val = value_make_null();
        agg_state_t* agg = &state->groups[g].agg_states[a];

        switch (kind) {
        case AGG_COUNT:
            val = value_make_integer(agg->count);
            break;
        case AGG_SUM:
            if (agg->has_value) val = value_copy(&agg->sum);
            break;
        case AGG_AVG:
            if (agg->has_value && agg->count > 0) {
                if (agg->sum.type == TYPE_INTEGER) {
                    val = value_make_float((double)agg->sum.val.int_val / agg->count);
                } else if (agg->sum.type == TYPE_FLOAT) {
                    val = value_make_float(agg->sum.val.float_val / agg->count);
                }
            }
            break;
        case AGG_MIN:
            if (agg->has_value) val = value_copy(&agg->min);
            break;
        case AGG_MAX:
            if (agg->has_value) val = value_copy(&agg->max);
            break;
        }

        tuple_set_value(out, state->group_count + a, &val);
        value_destroy(&val);
    }

    return DB_OK;
}

static void hash_agg_close(executor_t* self) {
    hash_agg_state_t* state = (hash_agg_state_t*)self->state;
    if (state->materialized) {
        for (int g = 0; g < state->group_used; g++) {
            if (state->groups[g].group_key) {
                tuple_destroy(state->groups[g].group_key);
                db_free(state->groups[g].group_key);
            }
            for (int a = 0; a < state->groups[g].agg_count; a++) {
                agg_state_t* agg = &state->groups[g].agg_states[a];
                if (agg->has_value) {
                    value_destroy(&agg->sum);
                    value_destroy(&agg->min);
                    value_destroy(&agg->max);
                }
            }
            db_free(state->groups[g].agg_states);
        }
        db_free(state->groups);
        state->groups = NULL;
        state->materialized = 0;
    }
}

static void hash_agg_destroy(executor_t* self) {
    hash_agg_state_t* state = (hash_agg_state_t*)self->state;
    if (state) {
        if (state->group_by) db_free(state->group_by);
        if (state->aggregates) db_free(state->aggregates);
        db_free(state);
        self->state = NULL;
    }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* hash_agg_executor_create(executor_context_t* ctx,
                                      executor_t* child,
                                      expr_t** group_by, int group_count,
                                      expr_t** aggregates, int agg_count,
                                      schema_t* output_schema) {
    hash_agg_state_t* state = db_calloc(1, sizeof(hash_agg_state_t));
    if (!state) return NULL;

    state->group_count = group_count;
    state->agg_count = agg_count;

    if (group_count > 0) {
        state->group_by = db_malloc(sizeof(expr_t*) * group_count);
        memcpy(state->group_by, group_by, sizeof(expr_t*) * group_count);
    }
    if (agg_count > 0) {
        state->aggregates = db_malloc(sizeof(expr_t*) * agg_count);
        memcpy(state->aggregates, aggregates, sizeof(expr_t*) * agg_count);
    }

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state); return NULL; }

    exec->init = hash_agg_init;
    exec->next = hash_agg_next;
    exec->close = hash_agg_close;
    exec->destroy = hash_agg_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;
    exec->output_schema = output_schema;

    executor_add_child(exec, child);
    return exec;
}
