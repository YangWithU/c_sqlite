#pragma once

#include "src/executor/executor.h"
#include "src/executor/bpm_heap.h"
#include "src/parser/ast.h"

typedef struct {
    bpm_heap_t*    heap;
    schema_t*      table_schema;
    int            done;
} delete_state_t;

executor_t* delete_executor_create(executor_context_t* ctx,
                                    bpm_heap_t* heap, schema_t* table_schema,
                                    executor_t* child);
