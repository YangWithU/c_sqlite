#pragma once

#include "src/common/types.h"
#include "src/common/hashmap.h"

/* Deadlock detector using wait-for graph + DFS cycle detection */
typedef struct {
    hashmap_t waits_for;  /* txn_id (void* key) -> txn_id (blocked on) */
} deadlock_detector_t;

int  deadlock_detector_init(deadlock_detector_t* dd);
void deadlock_detector_destroy(deadlock_detector_t* dd);

/* Add a waits-for edge: txn_a is waiting for txn_b */
void deadlock_add_edge(deadlock_detector_t* dd, txn_id_t txn_a, txn_id_t txn_b);

/* Remove all edges for a txn (when it finishes) */
void deadlock_remove_edges(deadlock_detector_t* dd, txn_id_t txn);

/* Check for deadlock. If found, returns the victim txn_id.
 * Returns INVALID_TXN_ID if no deadlock. */
txn_id_t deadlock_detect(deadlock_detector_t* dd);
