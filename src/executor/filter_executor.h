#pragma once

#include "src/executor/executor.h"
#include "src/parser/ast.h"

/* Filter executor: passes through child tuples satisfying a predicate */
executor_t* filter_executor_create(executor_context_t* ctx,
                                    executor_t* child, expr_t* predicate,
                                    schema_t* schema);
