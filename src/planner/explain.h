#pragma once

#include "src/planner/physical_plan.h"

/* Print a physical plan tree in a human-readable format.
 * Uses indentation to show the tree structure. */
void explain_print_plan(const physical_node_t* plan);
