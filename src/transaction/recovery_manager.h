#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/transaction/wal_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/common/hashmap.h"

/* Active Transaction Table — tracks txn status during recovery */
typedef struct {
    txn_id_t txn_id;
    lsn_t    last_lsn;
    int      status;  /* 0=active, 1=committed, 2=aborted */
} att_entry_t;

/* Dirty Page Table — tracks pages that need redo */
typedef struct {
    page_id_t page_id;
    lsn_t     rec_lsn;  /* LSN of first record that dirtied this page */
} dpt_entry_t;

/* Recovery manager — ARIES three-phase recovery */
typedef struct {
    wal_manager_t*        wal;
    buffer_pool_manager_t* bpm;
    hashmap_t             att;  /* txn_id -> att_entry_t* */
    hashmap_t             dpt;  /* page_id -> dpt_entry_t* */
} recovery_manager_t;

int  recovery_manager_init(recovery_manager_t* rm,
                            wal_manager_t* wal, buffer_pool_manager_t* bpm);
void recovery_manager_destroy(recovery_manager_t* rm);

/* Run the full ARIES recovery: Analysis → Redo → Undo */
int recovery_run(recovery_manager_t* rm);

/* Individual phases (exposed for testing) */
int recovery_analysis(recovery_manager_t* rm);
int recovery_redo(recovery_manager_t* rm);
int recovery_undo(recovery_manager_t* rm);
