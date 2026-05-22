#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/vector.h"
#include "src/storage/schema.h"
#include "src/storage/tuple.h"
#include "src/parser/ast.h"
#include "src/planner/planner.h"
#include "src/catalog/catalog.h"
#include "src/buffer/buffer_pool_manager.h"

/* Execution engine: parse → plan → optimize → build executor tree → execute.
 *
 * DDL (CREATE TABLE, DROP TABLE, CREATE INDEX, DROP INDEX) goes directly to catalog.
 * DML (SELECT, INSERT, UPDATE, DELETE) goes through planner + executor.
 * EXPLAIN: plan + optimize, then print instead of execute. */

typedef struct {
    planner_t*              planner;
    catalog_t*              catalog;
    buffer_pool_manager_t*  bpm;
} execution_engine_t;

/* Initialize the engine. Returns DB_OK. */
int  execution_engine_init(execution_engine_t* engine, catalog_t* catalog,
                           buffer_pool_manager_t* bpm);

/* Destroy the engine. */
void execution_engine_destroy(execution_engine_t* engine);

/* Execute a parsed statement.
 * For SELECT: fills out_tuples with result tuples, sets *out_schema.
 * For INSERT/UPDATE/DELETE: sets *out_affected to the number of rows affected.
 * For DDL: performs the operation directly.
 * For EXPLAIN: prints the plan.
 * Returns DB_OK or error code. */
int execution_engine_execute(execution_engine_t* engine, const stmt_t* stmt,
                             vector_t* out_tuples, schema_t** out_schema,
                             int* out_affected);
