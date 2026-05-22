#include "src/executor/project_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"
#include <string.h>

typedef struct {
    expr_t** expressions;
    int      expr_count;
} project_state_t;

static int project_init(executor_t* self) {
    if (self->child_count < 1) return DB_INVALID_ARGUMENT;
    return self->children[0]->init(self->children[0]);
}

static int project_next(executor_t* self, tuple_t* out) {
    project_state_t* state = (project_state_t*)self->state;
    executor_t* child = self->children[0];
    tuple_t child_tuple;

    if (child->next(child, &child_tuple) != DB_OK)
        return DB_PAGE_NOT_FOUND;

    /* Get the child's schema to evaluate expressions */
    schema_t* child_schema = child->output_schema;

    tuple_create(out, state->expr_count);
    for (int i = 0; i < state->expr_count; i++) {
        value_t val = value_make_null();
        expr_evaluate(state->expressions[i], &child_tuple, child_schema, &val);
        tuple_set_value(out, i, &val);
        value_destroy(&val);
    }

    tuple_destroy(&child_tuple);
    return DB_OK;
}

static void project_close(executor_t* self) {
    if (self->child_count > 0)
        self->children[0]->close(self->children[0]);
}

static void project_destroy(executor_t* self) {
    project_state_t* state = (project_state_t*)self->state;
    if (state) {
        if (state->expressions) db_free(state->expressions);
        db_free(state);
        self->state = NULL;
    }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* project_executor_create(executor_context_t* ctx,
                                     executor_t* child,
                                     expr_t** expressions, int expr_count,
                                     schema_t* output_schema) {
    project_state_t* state = db_calloc(1, sizeof(project_state_t));
    if (!state) return NULL;

    state->expr_count = expr_count;
    state->expressions = db_malloc(sizeof(expr_t*) * expr_count);
    if (!state->expressions) { db_free(state); return NULL; }
    memcpy(state->expressions, expressions, sizeof(expr_t*) * expr_count);

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state->expressions); db_free(state); return NULL; }

    exec->init = project_init;
    exec->next = project_next;
    exec->close = project_close;
    exec->destroy = project_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;
    exec->output_schema = output_schema;

    executor_add_child(exec, child);
    return exec;
}
