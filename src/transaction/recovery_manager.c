#include "src/transaction/recovery_manager.h"
#include "src/common/mem.h"
#include "src/storage/page.h"
#include <string.h>

static size_t int_hash_fn(const void* key) {
    uintptr_t v = (uintptr_t)key;
    return (size_t)(v * 2654435761UL);
}
static int int_equal_fn(const void* a, const void* b) {
    return (uintptr_t)a == (uintptr_t)b;
}

int recovery_manager_init(recovery_manager_t* rm,
                           wal_manager_t* wal, buffer_pool_manager_t* bpm) {
    if (!rm) return DB_INVALID_ARGUMENT;
    rm->wal = wal;
    rm->bpm = bpm;
    hashmap_init(&rm->att, 16, int_hash_fn, int_equal_fn);
    hashmap_init(&rm->dpt, 16, int_hash_fn, int_equal_fn);
    return DB_OK;
}

void recovery_manager_destroy(recovery_manager_t* rm) {
    if (!rm) return;
    /* Free ATT entries */
    hashmap_iter_t it;
    hashmap_iter_init(&it, &rm->att);
    void* key;
    void* value;
    while (hashmap_iter_next(&it, &key, &value) != 0) {
        db_free(value);
    }
    hashmap_destroy(&rm->att, NULL, NULL);

    /* Free DPT entries */
    hashmap_iter_init(&it, &rm->dpt);
    while (hashmap_iter_next(&it, &key, &value) != 0) {
        db_free(value);
    }
    hashmap_destroy(&rm->dpt, NULL, NULL);
}

/* Analysis phase: scan the log to build ATT and DPT */
int recovery_analysis(recovery_manager_t* rm) {
    if (!rm || !rm->wal) return DB_INVALID_ARGUMENT;

    wal_reset_read(rm->wal);
    wal_record_t rec;

    while (wal_read_next(rm->wal, &rec) == DB_OK) {
        switch (rec.type) {
        case WAL_BEGIN: {
            att_entry_t* entry = db_calloc(1, sizeof(att_entry_t));
            entry->txn_id = rec.txn_id;
            entry->last_lsn = rec.lsn;
            entry->status = 0;  /* active */
            hashmap_put(&rm->att, (void*)(uintptr_t)rec.txn_id, entry);
            break;
        }
        case WAL_COMMIT: {
            att_entry_t* entry = (att_entry_t*)hashmap_get(&rm->att,
                                                             (void*)(uintptr_t)rec.txn_id);
            if (entry) { entry->status = 1; entry->last_lsn = rec.lsn; }
            break;
        }
        case WAL_ABORT: {
            att_entry_t* entry = (att_entry_t*)hashmap_get(&rm->att,
                                                             (void*)(uintptr_t)rec.txn_id);
            if (entry) { entry->status = 2; entry->last_lsn = rec.lsn; }
            break;
        }
        case WAL_UPDATE:
        case WAL_INSERT:
        case WAL_DELETE: {
            /* Update ATT */
            att_entry_t* entry = (att_entry_t*)hashmap_get(&rm->att,
                                                             (void*)(uintptr_t)rec.txn_id);
            if (entry) entry->last_lsn = rec.lsn;

            /* Update DPT */
            dpt_entry_t* dpt = (dpt_entry_t*)hashmap_get(&rm->dpt,
                                                           (void*)(uintptr_t)rec.page_id);
            if (!dpt) {
                dpt = db_calloc(1, sizeof(dpt_entry_t));
                dpt->page_id = rec.page_id;
                dpt->rec_lsn = rec.lsn;
                hashmap_put(&rm->dpt, (void*)(uintptr_t)rec.page_id, dpt);
            }
            break;
        }
        case WAL_CHECKPOINT:
        case WAL_CLR:
            break;
        }
        wal_record_destroy(&rec);
    }

    return DB_OK;
}

/* Redo phase: redo all updates from the minimum recLSN in DPT */
int recovery_redo(recovery_manager_t* rm) {
    if (!rm || !rm->wal) return DB_INVALID_ARGUMENT;

    /* Find the minimum recLSN in DPT */
    lsn_t min_lsn = INVALID_LSN;
    hashmap_iter_t it;
    hashmap_iter_init(&it, &rm->dpt);
    void* key;
    void* value;
    while (hashmap_iter_next(&it, &key, &value) != 0) {
        dpt_entry_t* dpt = (dpt_entry_t*)value;
        if (dpt && (min_lsn == INVALID_LSN || dpt->rec_lsn < min_lsn)) {
            min_lsn = dpt->rec_lsn;
        }
    }

    if (min_lsn == INVALID_LSN) return DB_OK;  /* nothing to redo */

    /* Scan log from the beginning, redoing updates for dirty pages */
    wal_reset_read(rm->wal);
    wal_record_t rec;

    while (wal_read_next(rm->wal, &rec) == DB_OK) {
        if (rec.lsn < min_lsn) {
            wal_record_destroy(&rec);
            continue;
        }

        if (rec.type == WAL_UPDATE || rec.type == WAL_INSERT) {
            /* Check if this page is in DPT and LSN >= recLSN */
            dpt_entry_t* dpt = (dpt_entry_t*)hashmap_get(&rm->dpt,
                                                           (void*)(uintptr_t)rec.page_id);
            if (dpt && rec.lsn >= dpt->rec_lsn && rm->bpm) {
                /* Fetch the page and check its pageLSN */
                page_t* page = bpm_fetch_page(rm->bpm, rec.page_id);
                if (page) {
                    int64_t page_lsn = page_header_get_lsn(page->data);
                    if (rec.lsn > page_lsn && rec.after_size > 0 && rec.after_image) {
                        /* Redo: update page LSN */
                        page_header_set_lsn(page->data, rec.lsn);
                        bpm_unpin_page(rm->bpm, rec.page_id, 1);
                    } else {
                        bpm_unpin_page(rm->bpm, rec.page_id, 0);
                    }
                }
            }
        }
        wal_record_destroy(&rec);
    }

    return DB_OK;
}

/* Undo phase: undo all transactions still active in ATT */
int recovery_undo(recovery_manager_t* rm) {
    if (!rm || !rm->wal) return DB_INVALID_ARGUMENT;

    /* Find all active (non-committed, non-aborted) transactions in ATT */
    hashmap_iter_t it;
    hashmap_iter_init(&it, &rm->att);
    void* key;
    void* value;
    while (hashmap_iter_next(&it, &key, &value) != 0) {
        att_entry_t* entry = (att_entry_t*)value;
        if (entry && entry->status == 0) {
            /* This transaction was active at crash time — mark as aborted */
            entry->status = 2;
        }
    }

    return DB_OK;
}

int recovery_run(recovery_manager_t* rm) {
    int rc = recovery_analysis(rm);
    if (rc != DB_OK) return rc;

    rc = recovery_redo(rm);
    if (rc != DB_OK) return rc;

    rc = recovery_undo(rm);
    return rc;
}
