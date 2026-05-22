#pragma once

#include "src/executor/executor.h"
#include "src/parser/ast.h"

/* Project executor: evaluates select-list expressions to produce output tuples */
executor_t* project_executor_create(executor_context_t* ctx,
                                    executor_t* child,
                                    expr_t** expressions, int expr_count,
                                    schema_t* output_schema);
