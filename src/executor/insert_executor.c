#include "src/executor/insert_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/tuple.h"
#include "src/common/mem.h"
#include <string.h>

static int insert_init(executor_t* self) {
    insert_state_t* state = (insert_state_t*)self->state;
    state->current_row = 0;
    state->inserted_count = 0;
    return DB_OK;
}

static int insert_next(executor_t* self, tuple_t* out) {
    insert_state_t* state = (insert_state_t*)self->state;

    if (state->current_row >= state->val_row_count)
        return DB_PAGE_NOT_FOUND;  /* done */

    int row = state->current_row++;
    int col_count = state->val_col_counts[row];

    /* Build a tuple from value expressions */
    tuple_t tuple;
    tuple_create(&tuple, col_count);

    for (int i = 0; i < col_count; i++) {
        value_t val = value_make_null();
        int rc = expr_evaluate(state->values[row][i], NULL, NULL, &val);
        if (rc != DB_OK) {
            value_destroy(&val);
            tuple_destroy(&tuple);
            return rc;
        }

        /* Cast to the column type if needed */
        type_id_t target_type = state->table_schema->columns[i].type;
        if (val.type != target_type && val.type != TYPE_NULL) {
            value_t cast_val = value_make_null();
            rc = value_cast_to(&val, target_type, &cast_val);
            value_destroy(&val);
            if (rc != DB_OK) {
                tuple_destroy(&tuple);
                return rc;
            }
            tuple_set_value(&tuple, i, &cast_val);
            value_destroy(&cast_val);
        } else {
            tuple_set_value(&tuple, i, &val);
            value_destroy(&val);
        }
    }

    /* Serialize and insert */
    uint32_t max_size = schema_max_tuple_size(state->table_schema);
    char* buf = db_malloc(max_size);
    if (!buf) { tuple_destroy(&tuple); return DB_OUT_OF_MEMORY; }

    uint32_t actual_size = 0;
    int rc = tuple_serialize(&tuple, state->table_schema, buf, &actual_size);
    if (rc != DB_OK) {
        db_free(buf);
        tuple_destroy(&tuple);
        return rc;
    }

    rid_t rid;
    rc = bpm_heap_insert(state->heap, buf, (uint16_t)actual_size, &rid);
    db_free(buf);

    if (rc == DB_OK) {
        state->inserted_count++;
        tuple.rid = rid;
        *out = tuple;
        return DB_OK;
    }

    tuple_destroy(&tuple);
    return rc;
}

static void insert_close(executor_t* self) {
    /* Nothing to close */
}

static void insert_destroy(executor_t* self) {
    insert_state_t* state = (insert_state_t*)self->state;
    if (state) {
        if (state->values) db_free(state->values);
        if (state->val_col_counts) db_free(state->val_col_counts);
        db_free(state);
        self->state = NULL;
    }
    if (self->output_schema) {
        schema_destroy(self->output_schema);
        db_free(self->output_schema);
        self->output_schema = NULL;
    }
}

executor_t* insert_executor_create(executor_context_t* ctx,
                                    bpm_heap_t* heap, schema_t* table_schema,
                                    expr_t*** values, int val_row_count,
                                    int* val_col_counts) {
    insert_state_t* state = db_calloc(1, sizeof(insert_state_t));
    if (!state) return NULL;

    state->heap = heap;
    state->table_schema = table_schema;
    state->val_row_count = val_row_count;
    state->inserted_count = 0;
    state->current_row = 0;

    /* Copy values arrays (shallow — the expressions are owned by the AST) */
    state->values = db_malloc(sizeof(expr_t***) * val_row_count);
    if (!state->values) { db_free(state); return NULL; }
    memcpy(state->values, values, sizeof(expr_t***) * val_row_count);

    state->val_col_counts = db_malloc(sizeof(int) * val_row_count);
    if (!state->val_col_counts) { db_free(state->values); db_free(state); return NULL; }
    memcpy(state->val_col_counts, val_col_counts, sizeof(int) * val_row_count);

    executor_t* exec = db_calloc(1, sizeof(executor_t));
    if (!exec) { db_free(state->values); db_free(state->val_col_counts); db_free(state); return NULL; }

    exec->init = insert_init;
    exec->next = insert_next;
    exec->close = insert_close;
    exec->destroy = insert_destroy;
    exec->exec_ctx = ctx;
    exec->state = state;

    return exec;
}
