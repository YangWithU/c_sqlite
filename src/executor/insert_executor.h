#pragma once

#include "src/executor/executor.h"
#include "src/executor/bpm_heap.h"
#include "src/parser/ast.h"

/* Insert executor: evaluates value expressions, inserts tuples into heap */
typedef struct {
    bpm_heap_t*    heap;
    schema_t*      table_schema;
    expr_t***      values;         /* array of rows, each row is array of exprs */
    int            val_row_count;
    int*           val_col_counts;
    int            current_row;
    int            inserted_count;
} insert_state_t;

executor_t* insert_executor_create(executor_context_t* ctx,
                                    bpm_heap_t* heap, schema_t* table_schema,
                                    expr_t*** values, int val_row_count,
                                    int* val_col_counts);
