#include "src/executor/nl_join_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"
#include <string.h>

typedef struct {
    token_type_t join_type;
    expr_t*      condition;
    tuple_t      current_outer;
    int          has_outer;
    int          inner_exhausted;
    int          started;
} nl_join_state_t;

/* Combine two tuples: outer columns first, then inner columns */
static void combine_tuples(const tuple_t* outer, const tuple_t* inner,
                           const schema_t* outer_schema, const schema_t* inner_schema,
                           tuple_t* out) {
    int total = outer->num_values + inner->num_values;
    tuple_create(out, total);
    for (int i = 0; i < outer->num_values; i++) {
        const value_t* v = tuple_get_value(outer, i);
        if (v) {
            value_t copy = value_copy(v);
            tuple_set_value(out, i, &copy);
            value_destroy(&copy);
        }
    }
    for (int i = 0; i < inner->num_values; i++) {
        const value_t* v = tuple_get_value(inner, i);
        if (v) {
            value_t copy = value_copy(v);
            tuple_set_value(out, outer->num_values + i, &copy);
            value_destroy(&copy);
        }
    }
}

static int nl_join_init(executor_t* self) {
    nl_join_state_t* state = (nl_join_state_t*)self->state;
    state->has_outer = 0;
    state->inner_exhausted = 1;
    state->started = 0;
    if (self->child_count >= 2) {
        int rc = self->children[0]->init(self->children[0]);
        if (rc != DB_OK) return rc;
        return self->children[1]->init(self->children[1]);
    }
    return DB_INVALID_ARGUMENT;
}

static int nl_join_next(executor_t* self, tuple_t* out) {
    nl_join_state_t* state = (nl_join_state_t*)self->state;
    executor_t* outer_exec = self->children[0];
    executor_t* inner_exec = self->children[1];

    for (;;) {
        /* If no current outer tuple, get one */
        if (!state->has_outer) {
            if (outer_exec->next(outer_exec, &state->current_outer) != DB_OK)
                return DB_PAGE_NOT_FOUND;
            state->has_outer = 1;
            state->inner_exhausted = 0;
            /* Reset inner for new outer tuple */
            inner_exec->close(inner_exec);
            inner_exec->init(inner_exec);
        }

        /* Scan inner */
        tuple_t inner_tuple;
        while (inner_exec->next(inner_exec, &inner_tuple) == DB_OK) {
            tuple_t combined;
            combine_tuples(&state->current_outer, &inner_tuple,
                           outer_exec->output_schema, inner_exec->output_schema,
                           &combined);

            /* Evaluate join condition */
            if (state->condition) {
                value_t pred = value_make_null();
                int rc = expr_evaluate(state->condition, &combined,
                                       self->output_schema, &pred);
                int pass = (rc == DB_OK && pred.type == TYPE_BOOLEAN && pred.val.bool_val);
                value_destroy(&pred);
                if (!pass) {
                    tuple_destroy(&combined);
                    tuple_destroy(&inner_tuple);
                    continue;
                }
            }

            *out = combined;
            tuple_destroy(&inner_tuple);
            return DB_OK;
        }

        /* Inner exhausted for this outer — advance outer */
        tuple_destroy(&state->current_outer);
        state->has_outer = 0;
    }
}

static void nl_join_close(executor_t* self) {
    nl_join_state_t* state = (nl_join_state_t*)self->state;
    if (state->has_outer) {
        tuple_destroy(&state->current_outer);
        state->has_outer = 0;
    }
    if (self->child_count >= 2) {
        self->children[0]->close(self->children[0]);
        self->children[1]->close(self->children[1]);
    }
}

static void nl_join_destroy(executor_t* self) {
    nl_join_state_t* state = (nl_join_state_t*)self->state;
    if (state) { db_free(state); self->state = NULL; }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* nl_join_executor_create(executor_context_t* ctx,
                                     executor_t* outer, executor_t* inner,
                                     token_type_t join_type, expr_t* condition,
                                     schema_t* output_schema) {
    nl_join_state_t* state = db_calloc(1, sizeof(nl_join_state_t));
    if (!state) return NULL;

    state->join_type = join_type;
    state->condition = condition;
    state->has_outer = 0;

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state); return NULL; }

    exec->init = nl_join_init;
    exec->next = nl_join_next;
    exec->close = nl_join_close;
    exec->destroy = nl_join_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;
    exec->output_schema = output_schema;

    executor_add_child(exec, outer);
    executor_add_child(exec, inner);
    return exec;
}
