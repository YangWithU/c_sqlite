#include "src/executor/update_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"
#include <string.h>

static int update_init(executor_t* self) {
    update_state_t* state = (update_state_t*)self->state;
    state->done = 0;
    if (self->child_count > 0)
        return self->children[0]->init(self->children[0]);
    return DB_OK;
}

static int update_next(executor_t* self, tuple_t* out) {
    update_state_t* state = (update_state_t*)self->state;
    if (state->done) return DB_PAGE_NOT_FOUND;

    executor_t* child = self->children[0];
    tuple_t old_tuple;
    int rc;

    while ((rc = child->next(child, &old_tuple)) == DB_OK) {
        /* Apply assignments to build new tuple */
        tuple_t new_tuple;
        tuple_create(&new_tuple, state->table_schema->num_columns);

        /* Start with old values */
        for (int i = 0; i < state->table_schema->num_columns; i++) {
            const value_t* val = tuple_get_value(&old_tuple, i);
            if (val) {
                value_t copy = value_copy(val);
                tuple_set_value(&new_tuple, i, &copy);
                value_destroy(&copy);
            }
        }

        /* Apply assignments */
        for (int i = 0; i < state->assign_count; i++) {
            int col_idx = schema_find_column(state->table_schema,
                                             state->assignments[i].column);
            if (col_idx < 0) {
                tuple_destroy(&old_tuple);
                tuple_destroy(&new_tuple);
                return DB_UNKNOWN_COLUMN;
            }
            value_t val = value_make_null();
            rc = expr_evaluate(state->assignments[i].value, &old_tuple,
                               state->table_schema, &val);
            if (rc != DB_OK) {
                tuple_destroy(&old_tuple);
                tuple_destroy(&new_tuple);
                value_destroy(&val);
                return rc;
            }
            tuple_set_value(&new_tuple, col_idx, &val);
            value_destroy(&val);
        }

        /* Serialize new tuple */
        uint32_t max_size = schema_max_tuple_size(state->table_schema);
        char* buf = db_malloc(max_size);
        if (!buf) { tuple_destroy(&old_tuple); tuple_destroy(&new_tuple); return DB_OUT_OF_MEMORY; }

        uint32_t actual_size = 0;
        rc = tuple_serialize(&new_tuple, state->table_schema, buf, &actual_size);
        if (rc != DB_OK) {
            db_free(buf);
            tuple_destroy(&old_tuple);
            tuple_destroy(&new_tuple);
            return rc;
        }

        /* Write back to heap */
        rc = bpm_heap_update(state->heap, old_tuple.rid, buf, (uint16_t)actual_size);
        db_free(buf);
        tuple_destroy(&new_tuple);

        if (rc == DB_OK) {
            *out = old_tuple;
            return DB_OK;
        }
        tuple_destroy(&old_tuple);
    }

    state->done = 1;
    return DB_PAGE_NOT_FOUND;
}

static void update_close(executor_t* self) {
    if (self->child_count > 0)
        self->children[0]->close(self->children[0]);
}

static void update_destroy(executor_t* self) {
    update_state_t* state = (update_state_t*)self->state;
    if (state) { db_free(state); self->state = NULL; }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* update_executor_create(executor_context_t* ctx,
                                    bpm_heap_t* heap, schema_t* table_schema,
                                    assignment_t* assignments, int assign_count,
                                    executor_t* child) {
    update_state_t* state = db_calloc(1, sizeof(update_state_t));
    if (!state) return NULL;

    state->heap = heap;
    state->table_schema = table_schema;
    state->assignments = assignments;
    state->assign_count = assign_count;
    state->done = 0;

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state); return NULL; }

    exec->init = update_init;
    exec->next = update_next;
    exec->close = update_close;
    exec->destroy = update_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;

    executor_add_child(exec, child);
    return exec;
}
