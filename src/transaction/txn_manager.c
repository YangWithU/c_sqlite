#include "src/transaction/txn_manager.h"
#include "src/common/mem.h"
#include <string.h>

static size_t int_hash_fn(const void* key) {
    uintptr_t v = (uintptr_t)key;
    return (size_t)(v * 2654435761UL);
}
static int int_equal_fn(const void* a, const void* b) {
    return (uintptr_t)a == (uintptr_t)b;
}

int txn_manager_init(txn_manager_t* tm, buffer_pool_manager_t* bpm,
                      lock_manager_t* lm, wal_manager_t* wm) {
    if (!tm) return DB_INVALID_ARGUMENT;
    tm->bpm = bpm;
    tm->lock_mgr = lm;
    tm->wal_mgr = wm;
    tm->next_txn_id = 1;
    hashmap_init(&tm->active_txns, 16, int_hash_fn, int_equal_fn);
    return DB_OK;
}

void txn_manager_destroy(txn_manager_t* tm) {
    if (!tm) return;
    /* Free all active transactions using iterator */
    hashmap_iter_t it;
    hashmap_iter_init(&it, &tm->active_txns);
    void* key;
    void* value;
    while (hashmap_iter_next(&it, &key, &value) != 0) {
        transaction_t* txn = (transaction_t*)value;
        if (txn) {
            if (tm->lock_mgr) lock_release_all(tm->lock_mgr, txn->txn_id);
            db_free(txn);
        }
    }
    hashmap_destroy(&tm->active_txns, NULL, NULL);
}

transaction_t* txn_begin(txn_manager_t* tm, isolation_level_t level) {
    if (!tm) return NULL;

    transaction_t* txn = db_malloc(sizeof(transaction_t));
    if (!txn) return NULL;

    txn_init(txn, tm->next_txn_id++, level);
    hashmap_put(&tm->active_txns, (void*)(uintptr_t)txn->txn_id, txn);

    /* Write BEGIN log record */
    if (tm->wal_mgr) {
        wal_record_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.txn_id = txn->txn_id;
        rec.type = WAL_BEGIN;
        txn->last_lsn = wal_append(tm->wal_mgr, &rec);
    }

    return txn;
}

int txn_commit(txn_manager_t* tm, transaction_t* txn) {
    if (!tm || !txn) return DB_INVALID_ARGUMENT;
    if (txn->state != TXN_ACTIVE) return DB_TXN_ABORTED;

    /* Write COMMIT log record */
    if (tm->wal_mgr) {
        wal_record_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.txn_id = txn->txn_id;
        rec.prev_lsn = txn->last_lsn;
        rec.type = WAL_COMMIT;
        txn->last_lsn = wal_append(tm->wal_mgr, &rec);
        wal_flush(tm->wal_mgr, txn->last_lsn);
    }

    /* Release all locks */
    if (tm->lock_mgr) {
        lock_release_all(tm->lock_mgr, txn->txn_id);
    }

    txn->state = TXN_COMMITTED;
    hashmap_remove(&tm->active_txns, (void*)(uintptr_t)txn->txn_id, NULL, free);
    return DB_OK;
}

int txn_abort(txn_manager_t* tm, transaction_t* txn) {
    if (!tm || !txn) return DB_INVALID_ARGUMENT;
    if (txn->state != TXN_ACTIVE) return DB_TXN_ABORTED;

    /* Write ABORT log record */
    if (tm->wal_mgr) {
        wal_record_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.txn_id = txn->txn_id;
        rec.prev_lsn = txn->last_lsn;
        rec.type = WAL_ABORT;
        txn->last_lsn = wal_append(tm->wal_mgr, &rec);
        wal_flush(tm->wal_mgr, txn->last_lsn);
    }

    /* Release all locks */
    if (tm->lock_mgr) {
        lock_release_all(tm->lock_mgr, txn->txn_id);
    }

    txn->state = TXN_ABORTED;
    hashmap_remove(&tm->active_txns, (void*)(uintptr_t)txn->txn_id, NULL, free);
    return DB_OK;
}

transaction_t* txn_get(txn_manager_t* tm, txn_id_t id) {
    if (!tm) return NULL;
    return (transaction_t*)hashmap_get(&tm->active_txns, (void*)(uintptr_t)id);
}
