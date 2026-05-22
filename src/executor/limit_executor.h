#pragma once

#include "src/executor/executor.h"

/* Limit executor: passes through first N tuples, then stops */
executor_t* limit_executor_create(executor_context_t* ctx,
                                   executor_t* child,
                                   int limit, int offset,
                                   schema_t* output_schema);
