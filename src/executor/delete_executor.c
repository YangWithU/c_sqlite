#include "src/executor/delete_executor.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"

static int delete_init(executor_t* self) {
    delete_state_t* state = (delete_state_t*)self->state;
    state->done = 0;
    if (self->child_count > 0)
        return self->children[0]->init(self->children[0]);
    return DB_OK;
}

static int delete_next(executor_t* self, tuple_t* out) {
    delete_state_t* state = (delete_state_t*)self->state;
    if (state->done) return DB_PAGE_NOT_FOUND;

    executor_t* child = self->children[0];
    tuple_t tuple;
    int rc;

    while ((rc = child->next(child, &tuple)) == DB_OK) {
        rc = bpm_heap_delete(state->heap, tuple.rid);
        if (rc == DB_OK) {
            *out = tuple;
            return DB_OK;
        }
        tuple_destroy(&tuple);
    }

    state->done = 1;
    return DB_PAGE_NOT_FOUND;
}

static void delete_close(executor_t* self) {
    if (self->child_count > 0)
        self->children[0]->close(self->children[0]);
}

static void delete_destroy(executor_t* self) {
    delete_state_t* state = (delete_state_t*)self->state;
    if (state) { db_free(state); self->state = NULL; }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* delete_executor_create(executor_context_t* ctx,
                                    bpm_heap_t* heap, schema_t* table_schema,
                                    executor_t* child) {
    delete_state_t* state = db_calloc(1, sizeof(delete_state_t));
    if (!state) return NULL;

    state->heap = heap;
    state->table_schema = table_schema;

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state); return NULL; }

    exec->init = delete_init;
    exec->next = delete_next;
    exec->close = delete_close;
    exec->destroy = delete_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;

    executor_add_child(exec, child);
    return exec;
}
