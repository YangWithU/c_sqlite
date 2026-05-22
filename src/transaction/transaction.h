#pragma once

#include "src/common/types.h"
#include "src/common/error.h"

/* Transaction states */
typedef enum {
    TXN_INVALID,
    TXN_ACTIVE,
    TXN_COMMITTED,
    TXN_ABORTED,
} txn_state_t;

/* Isolation levels */
typedef enum {
    ISOLATION_READ_UNCOMMITTED,
    ISOLATION_READ_COMMITTED,
    ISOLATION_REPEATABLE_READ,
    ISOLATION_SERIALIZABLE,
} isolation_level_t;

/* Transaction */
typedef struct transaction {
    txn_id_t          txn_id;
    txn_state_t       state;
    isolation_level_t isolation;
    lsn_t             last_lsn;     /* LSN of last log record for this txn */
    lsn_t             undo_next_lsn; /* next LSN to undo during rollback */
} transaction_t;

/* Initialize a transaction */
void txn_init(transaction_t* txn, txn_id_t id, isolation_level_t level);

/* Get the transaction's current state */
txn_state_t txn_state(const transaction_t* txn);

/* Check if the transaction is active (can still do work) */
int txn_is_active(const transaction_t* txn);
