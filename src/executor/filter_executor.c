#include "src/executor/filter_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"

typedef struct {
    expr_t* predicate;
} filter_state_t;

static int filter_init(executor_t* self) {
    if (self->child_count < 1) return DB_INVALID_ARGUMENT;
    return self->children[0]->init(self->children[0]);
}

static int filter_next(executor_t* self, tuple_t* out) {
    filter_state_t* state = (filter_state_t*)self->state;
    executor_t* child = self->children[0];
    tuple_t tuple;

    while (child->next(child, &tuple) == DB_OK) {
        value_t pred = value_make_null();
        int rc = expr_evaluate(state->predicate, &tuple, self->output_schema, &pred);
        int pass = (rc == DB_OK && pred.type == TYPE_BOOLEAN && pred.val.bool_val);
        value_destroy(&pred);
        if (pass) {
            *out = tuple;
            return DB_OK;
        }
        tuple_destroy(&tuple);
    }
    return DB_PAGE_NOT_FOUND;
}

static void filter_close(executor_t* self) {
    if (self->child_count > 0)
        self->children[0]->close(self->children[0]);
}

static void filter_destroy(executor_t* self) {
    filter_state_t* state = (filter_state_t*)self->state;
    db_free(state);
    self->state = NULL;
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* filter_executor_create(executor_context_t* ctx,
                                    executor_t* child, expr_t* predicate,
                                    schema_t* schema) {
    filter_state_t* state = db_calloc(1, sizeof(filter_state_t));
    if (!state) return NULL;
    state->predicate = predicate;

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state); return NULL; }

    exec->init = filter_init;
    exec->next = filter_next;
    exec->close = filter_close;
    exec->destroy = filter_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;
    exec->output_schema = schema;

    executor_add_child(exec, child);
    return exec;
}
