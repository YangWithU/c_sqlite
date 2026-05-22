#pragma once

#include "src/executor/executor.h"
#include "src/executor/bpm_heap.h"
#include "src/parser/ast.h"

typedef struct {
    bpm_heap_t*      heap;
    bpm_heap_iter_t  iter;
    expr_t*          predicate;
    schema_t*        table_schema;
    int              initialized;
} seq_scan_state_t;

executor_t* seq_scan_executor_create(executor_context_t* ctx,
                                     bpm_heap_t* heap, schema_t* table_schema,
                                     expr_t* predicate);
