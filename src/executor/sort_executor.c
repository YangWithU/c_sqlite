#include "src/executor/sort_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/tuple.h"
#include "src/storage/value.h"
#include "src/common/mem.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    order_by_item_t* items;
    int              count;
    tuple_t*         tuples;
    int              tuple_count;
    int              tuple_capacity;
    int              emit_index;
    int              materialized;
} sort_state_t;

/* qsort comparator context */
typedef struct {
    const sort_state_t* state;
    const schema_t*     schema;
} sort_ctx_t;

static sort_ctx_t g_sort_ctx;

static int sort_compare(const void* a, const void* b) {
    const tuple_t* ta = (const tuple_t*)a;
    const tuple_t* tb = (const tuple_t*)b;
    const sort_state_t* st = g_sort_ctx.state;
    const schema_t* schema = g_sort_ctx.schema;

    for (int i = 0; i < st->count; i++) {
        value_t va = value_make_null(), vb = value_make_null();
        expr_evaluate(st->items[i].expr, ta, schema, &va);
        expr_evaluate(st->items[i].expr, tb, schema, &vb);

        int cmp;
        if (va.type == TYPE_NULL && vb.type == TYPE_NULL) cmp = 0;
        else if (va.type == TYPE_NULL) cmp = 1;  /* NULL LAST */
        else if (vb.type == TYPE_NULL) cmp = -1;
        else cmp = value_compare(&va, &vb);

        value_destroy(&va);
        value_destroy(&vb);

        if (cmp != 0) {
            return st->items[i].ascending ? cmp : -cmp;
        }
    }
    return 0;
}

static int sort_init(executor_t* self) {
    sort_state_t* state = (sort_state_t*)self->state;
    state->materialized = 0;
    state->emit_index = 0;
    state->tuple_count = 0;
    state->tuple_capacity = 0;
    state->tuples = NULL;
    if (self->child_count > 0)
        return self->children[0]->init(self->children[0]);
    return DB_OK;
}

static int sort_next(executor_t* self, tuple_t* out) {
    sort_state_t* state = (sort_state_t*)self->state;

    if (!state->materialized) {
        /* Materialize all child tuples */
        executor_t* child = self->children[0];
        state->tuple_capacity = 64;
        state->tuples = db_malloc(sizeof(tuple_t) * state->tuple_capacity);
        state->tuple_count = 0;

        tuple_t tuple;
        while (child->next(child, &tuple) == DB_OK) {
            if (state->tuple_count >= state->tuple_capacity) {
                state->tuple_capacity *= 2;
                state->tuples = db_realloc(state->tuples,
                                            sizeof(tuple_t) * state->tuple_capacity);
            }
            state->tuples[state->tuple_count++] = tuple;
        }
        child->close(child);

        /* Sort */
        if (state->tuple_count > 0) {
            g_sort_ctx.state = state;
            g_sort_ctx.schema = self->output_schema;
            qsort(state->tuples, state->tuple_count, sizeof(tuple_t), sort_compare);
        }

        state->materialized = 1;
        state->emit_index = 0;
    }

    if (state->emit_index >= state->tuple_count)
        return DB_PAGE_NOT_FOUND;

    *out = state->tuples[state->emit_index++];
    return DB_OK;
}

static void sort_close(executor_t* self) {
    sort_state_t* state = (sort_state_t*)self->state;
    if (state->materialized && state->tuples) {
        for (int i = 0; i < state->tuple_count; i++) {
            tuple_destroy(&state->tuples[i]);
        }
        db_free(state->tuples);
        state->tuples = NULL;
        state->materialized = 0;
    }
}

static void sort_destroy(executor_t* self) {
    sort_state_t* state = (sort_state_t*)self->state;
    if (state) {
        if (state->items) db_free(state->items);
        db_free(state);
        self->state = NULL;
    }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* sort_executor_create(executor_context_t* ctx,
                                  executor_t* child,
                                  order_by_item_t* items, int count,
                                  schema_t* output_schema) {
    sort_state_t* state = db_calloc(1, sizeof(sort_state_t));
    if (!state) return NULL;

    state->count = count;
    state->items = db_malloc(sizeof(order_by_item_t) * count);
    memcpy(state->items, items, sizeof(order_by_item_t) * count);

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state->items); db_free(state); return NULL; }

    exec->init = sort_init;
    exec->next = sort_next;
    exec->close = sort_close;
    exec->destroy = sort_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;
    exec->output_schema = output_schema;

    executor_add_child(exec, child);
    return exec;
}
