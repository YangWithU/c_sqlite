#pragma once

#include "src/executor/executor.h"
#include "src/parser/ast.h"

/* Sort executor: materializes all child tuples, sorts them */
executor_t* sort_executor_create(executor_context_t* ctx,
                                  executor_t* child,
                                  order_by_item_t* items, int count,
                                  schema_t* output_schema);
