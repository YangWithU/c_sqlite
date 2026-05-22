#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/parser/ast.h"
#include "src/catalog/catalog.h"
#include "src/planner/logical_plan.h"
#include "src/planner/physical_plan.h"

/* Query planner: converts AST statements into logical and physical plan trees. */
typedef struct {
    catalog_t* catalog;
} planner_t;

/* Initialize the planner with a catalog for metadata lookups. */
int  planner_init(planner_t* p, catalog_t* cat);
void planner_destroy(planner_t* p);

/* Convert an AST statement into a logical plan tree.
 * Returns the plan root, or NULL for DDL statements (which are handled
 * directly by the execution engine) or on error. */
logical_node_t* planner_plan_logical(planner_t* p, const stmt_t* stmt);

/* Convert a logical plan into a physical plan.
 * Applies RBO optimization rules:
 *   1. MatchIndex: equalities on indexed columns -> IndexScan
 *   2. PushDownFilter: move filters closer to scans
 *   3. MergeFilterScan: combine Filter+Scan
 * Returns the physical plan root, or NULL on error. */
physical_node_t* planner_plan_physical(planner_t* p, const logical_node_t* logical);

/* Convenience: plan logical then physical in one step. */
physical_node_t* planner_plan(planner_t* p, const stmt_t* stmt);
