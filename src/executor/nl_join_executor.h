#pragma once

#include "src/executor/executor.h"
#include "src/parser/ast.h"

/* Nested-loop join executor */
executor_t* nl_join_executor_create(executor_context_t* ctx,
                                     executor_t* outer, executor_t* inner,
                                     token_type_t join_type, expr_t* condition,
                                     schema_t* output_schema);
