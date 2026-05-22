#pragma once

#include "src/executor/executor.h"
#include "src/parser/ast.h"

/* Hash aggregate executor: groups tuples and computes aggregates */
executor_t* hash_agg_executor_create(executor_context_t* ctx,
                                      executor_t* child,
                                      expr_t** group_by, int group_count,
                                      expr_t** aggregates, int agg_count,
                                      schema_t* output_schema);
