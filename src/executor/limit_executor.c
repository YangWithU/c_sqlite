#include "src/executor/limit_executor.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"

typedef struct {
    int limit;
    int offset;
    int emitted;
    int skipped;
} limit_state_t;

static int limit_init(executor_t* self) {
    limit_state_t* state = (limit_state_t*)self->state;
    state->emitted = 0;
    state->skipped = 0;
    if (self->child_count > 0)
        return self->children[0]->init(self->children[0]);
    return DB_OK;
}

static int limit_next(executor_t* self, tuple_t* out) {
    limit_state_t* state = (limit_state_t*)self->state;
    executor_t* child = self->children[0];

    if (state->limit >= 0 && state->emitted >= state->limit)
        return DB_PAGE_NOT_FOUND;

    /* Skip offset rows */
    while (state->skipped < state->offset) {
        tuple_t tmp;
        if (child->next(child, &tmp) != DB_OK)
            return DB_PAGE_NOT_FOUND;
        tuple_destroy(&tmp);
        state->skipped++;
    }

    if (child->next(child, out) != DB_OK)
        return DB_PAGE_NOT_FOUND;

    state->emitted++;
    return DB_OK;
}

static void limit_close(executor_t* self) {
    if (self->child_count > 0)
        self->children[0]->close(self->children[0]);
}

static void limit_destroy(executor_t* self) {
    limit_state_t* state = (limit_state_t*)self->state;
    if (state) { db_free(state); self->state = NULL; }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* limit_executor_create(executor_context_t* ctx,
                                   executor_t* child,
                                   int limit, int offset,
                                   schema_t* output_schema) {
    limit_state_t* state = db_calloc(1, sizeof(limit_state_t));
    if (!state) return NULL;
    state->limit = limit;
    state->offset = offset;

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state); return NULL; }

    exec->init = limit_init;
    exec->next = limit_next;
    exec->close = limit_close;
    exec->destroy = limit_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;
    exec->output_schema = output_schema;

    executor_add_child(exec, child);
    return exec;
}
