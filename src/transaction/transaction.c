#include "src/transaction/transaction.h"

void txn_init(transaction_t* txn, txn_id_t id, isolation_level_t level) {
    if (!txn) return;
    txn->txn_id = id;
    txn->state = TXN_ACTIVE;
    txn->isolation = level;
    txn->last_lsn = INVALID_LSN;
    txn->undo_next_lsn = INVALID_LSN;
}

txn_state_t txn_state(const transaction_t* txn) {
    return txn ? txn->state : TXN_INVALID;
}

int txn_is_active(const transaction_t* txn) {
    return txn && txn->state == TXN_ACTIVE;
}
