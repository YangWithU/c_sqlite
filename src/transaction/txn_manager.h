#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/transaction/transaction.h"
#include "src/transaction/lock_manager.h"
#include "src/transaction/wal_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/common/hashmap.h"

/* Transaction manager: begin/commit/abort with WAL integration */
typedef struct {
    buffer_pool_manager_t* bpm;
    lock_manager_t*        lock_mgr;
    wal_manager_t*         wal_mgr;
    hashmap_t              active_txns;    /* txn_id -> transaction_t* */
    txn_id_t               next_txn_id;
} txn_manager_t;

/* Initialize transaction manager */
int  txn_manager_init(txn_manager_t* tm, buffer_pool_manager_t* bpm,
                       lock_manager_t* lm, wal_manager_t* wm);

/* Destroy transaction manager */
void txn_manager_destroy(txn_manager_t* tm);

/* Begin a new transaction */
transaction_t* txn_begin(txn_manager_t* tm, isolation_level_t level);

/* Commit a transaction */
int txn_commit(txn_manager_t* tm, transaction_t* txn);

/* Abort a transaction (rollback) */
int txn_abort(txn_manager_t* tm, transaction_t* txn);

/* Get a transaction by ID */
transaction_t* txn_get(txn_manager_t* tm, txn_id_t id);
