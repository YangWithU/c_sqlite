#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/parser/ast.h"
#include "src/storage/value.h"
#include "src/storage/schema.h"
#include "src/storage/tuple.h"

/* Evaluate an expression against a tuple and schema.
 * The result is placed in *out. Returns DB_OK or error code.
 * NULL semantics: arithmetic with NULL → NULL, comparisons with NULL → NULL,
 * AND/OR follow three-valued logic. */
int expr_evaluate(const expr_t* expr, const tuple_t* tuple, const schema_t* schema,
                  value_t* out);
