#include "src/executor/seq_scan_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"

static int seq_scan_init(executor_t* self) {
    seq_scan_state_t* state = (seq_scan_state_t*)self->state;
    return bpm_heap_iter_init(&state->iter, state->heap);
}

static int seq_scan_next(executor_t* self, tuple_t* out) {
    seq_scan_state_t* state = (seq_scan_state_t*)self->state;

    rid_t rid;
    const char* data;
    uint16_t size;
    while (bpm_heap_iter_next(&state->iter, &rid, &data, &size) == DB_OK) {
        tuple_t tuple;
        int rc = tuple_deserialize(&tuple, state->table_schema, data, size);
        if (rc != DB_OK) continue;

        tuple.rid = rid;

        if (state->predicate) {
            value_t pred_result = value_make_null();
            rc = expr_evaluate(state->predicate, &tuple, state->table_schema, &pred_result);
            int pass = (rc == DB_OK && pred_result.type == TYPE_BOOLEAN && pred_result.val.bool_val);
            value_destroy(&pred_result);
            if (!pass) {
                tuple_destroy(&tuple);
                continue;
            }
        }

        *out = tuple;
        return DB_OK;
    }

    return DB_PAGE_NOT_FOUND;  /* end of stream */
}

static void seq_scan_close(executor_t* self) {
    seq_scan_state_t* state = (seq_scan_state_t*)self->state;
    bpm_heap_iter_destroy(&state->iter);
}

static void seq_scan_destroy(executor_t* self) {
    seq_scan_state_t* state = (seq_scan_state_t*)self->state;
    if (state) {
        /* Note: heap and table_schema are owned by caller */
        db_free(state);
        self->state = NULL;
    }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* seq_scan_executor_create(executor_context_t* ctx,
                                      bpm_heap_t* heap, schema_t* table_schema,
                                      expr_t* predicate) {
    seq_scan_state_t* state = db_calloc(1, sizeof(seq_scan_state_t));
    if (!state) return NULL;

    state->heap = heap;
    state->predicate = predicate;
    state->table_schema = table_schema;

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state); return NULL; }

    exec->init = seq_scan_init;
    exec->next = seq_scan_next;
    exec->close = seq_scan_close;
    exec->destroy = seq_scan_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;

    return exec;
}
