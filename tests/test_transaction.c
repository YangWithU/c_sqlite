#include "xtest.h"
#include "src/transaction/transaction.h"
#include "src/transaction/lock_manager.h"
#include "src/transaction/deadlock_detector.h"
#include "src/transaction/wal_manager.h"
#include "src/transaction/txn_manager.h"
#include "src/transaction/recovery_manager.h"
#include "src/storage/disk_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/catalog/catalog.h"
#include "src/common/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char temp_path[256];
static char wal_path[256];

static void cleanup(void) {
    if (temp_path[0]) unlink(temp_path);
    if (wal_path[0]) unlink(wal_path);
}

static const char* make_temp_path(void) {
    static int init = 0;
    if (!init) { atexit(cleanup); init = 1; }
    snprintf(temp_path, sizeof(temp_path), "/tmp/test_txn_%d.db", (int)getpid());
    snprintf(wal_path, sizeof(wal_path), "/tmp/test_txn_%d.wal", (int)getpid());
    return temp_path;
}

/* ---- Transaction Tests ---- */

TEST(txn, lifecycle) {
    transaction_t txn;
    txn_init(&txn, 1, ISOLATION_READ_COMMITTED);
    EXPECT_EQ(TXN_ACTIVE, txn_state(&txn));
    EXPECT_TRUE(txn_is_active(&txn));
    EXPECT_EQ(1, txn.txn_id);

    txn.state = TXN_COMMITTED;
    EXPECT_EQ(TXN_COMMITTED, txn_state(&txn));
    EXPECT_FALSE(txn_is_active(&txn));
}

TEST(txn, isolation_levels) {
    transaction_t txn;
    txn_init(&txn, 1, ISOLATION_READ_UNCOMMITTED);
    EXPECT_EQ(ISOLATION_READ_UNCOMMITTED, txn.isolation);

    txn_init(&txn, 2, ISOLATION_SERIALIZABLE);
    EXPECT_EQ(ISOLATION_SERIALIZABLE, txn.isolation);
}

/* ---- Lock Manager Tests ---- */

TEST(lock, compatible) {
    EXPECT_TRUE(lock_compatible(LOCK_SHARED, LOCK_SHARED));
    EXPECT_FALSE(lock_compatible(LOCK_SHARED, LOCK_EXCLUSIVE));
    EXPECT_FALSE(lock_compatible(LOCK_EXCLUSIVE, LOCK_SHARED));
    EXPECT_FALSE(lock_compatible(LOCK_EXCLUSIVE, LOCK_EXCLUSIVE));
    EXPECT_TRUE(lock_compatible(LOCK_INTENT_SHARED, LOCK_INTENT_EXCLUSIVE));
    EXPECT_FALSE(lock_compatible(LOCK_INTENT_SHARED, LOCK_EXCLUSIVE));
}

TEST(lock, acquire_release) {
    lock_manager_t lm;
    lock_manager_init(&lm);

    /* Txn 1 acquires S lock on resource 100 */
    int rc = lock_acquire(&lm, 1, 100, LOCK_SHARED);
    EXPECT_EQ(DB_OK, rc);

    /* Txn 2 can also acquire S lock on resource 100 */
    rc = lock_acquire(&lm, 2, 100, LOCK_SHARED);
    EXPECT_EQ(DB_OK, rc);

    /* Txn 3 tries X lock — should conflict */
    rc = lock_acquire(&lm, 3, 100, LOCK_EXCLUSIVE);
    EXPECT_EQ(DB_LOCK_CONFLICT, rc);

    /* Txn 1 releases — now check if waiting txn gets granted */
    lock_release_all(&lm, 1);
    lock_release_all(&lm, 2);

    /* Txn 3 retries — should succeed now */
    rc = lock_acquire(&lm, 3, 100, LOCK_EXCLUSIVE);
    EXPECT_EQ(DB_OK, rc);

    lock_release_all(&lm, 3);
    lock_manager_destroy(&lm);
}

TEST(lock, has_lock) {
    lock_manager_t lm;
    lock_manager_init(&lm);

    lock_acquire(&lm, 1, 50, LOCK_SHARED);
    EXPECT_TRUE(lock_has_lock(&lm, 1, 50));
    EXPECT_FALSE(lock_has_lock(&lm, 2, 50));

    lock_release_all(&lm, 1);
    EXPECT_FALSE(lock_has_lock(&lm, 1, 50));
    lock_manager_destroy(&lm);
}

/* ---- Deadlock Detector Tests ---- */

TEST(deadlock, no_cycle) {
    deadlock_detector_t dd;
    deadlock_detector_init(&dd);

    /* T1 waits for T2, T2 waits for T3 — no cycle */
    deadlock_add_edge(&dd, 1, 2);
    deadlock_add_edge(&dd, 2, 3);

    txn_id_t victim = deadlock_detect(&dd);
    EXPECT_EQ(INVALID_TXN_ID, victim);

    deadlock_detector_destroy(&dd);
}

TEST(deadlock, cycle_detected) {
    deadlock_detector_t dd;
    deadlock_detector_init(&dd);

    /* T1 waits for T2, T2 waits for T1 — cycle */
    deadlock_add_edge(&dd, 1, 2);
    deadlock_add_edge(&dd, 2, 1);

    txn_id_t victim = deadlock_detect(&dd);
    EXPECT_NE(INVALID_TXN_ID, victim);

    deadlock_remove_edges(&dd, victim);
    deadlock_detector_destroy(&dd);
}

/* ---- WAL Manager Tests ---- */

TEST(wal, append_read) {
    const char* path = "/tmp/test_wal_append.wal";
    unlink(path);

    wal_manager_t wm;
    wal_manager_init(&wm, path);

    /* Append a BEGIN record */
    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.type = WAL_BEGIN;
    lsn_t lsn1 = wal_append(&wm, &rec);
    EXPECT_EQ(1, lsn1);

    /* Append an UPDATE record */
    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.prev_lsn = lsn1;
    rec.type = WAL_UPDATE;
    rec.page_id = 5;
    rec.offset = 3;
    const char* before = "old_data";
    const char* after = "new_data";
    rec.before_image = (char*)before;
    rec.before_size = 8;
    rec.after_image = (char*)after;
    rec.after_size = 8;
    lsn_t lsn2 = wal_append(&wm, &rec);
    EXPECT_EQ(2, lsn2);

    /* Append COMMIT */
    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.prev_lsn = lsn2;
    rec.type = WAL_COMMIT;
    wal_append(&wm, &rec);

    wal_flush(&wm, 3);

    /* Read back */
    wal_reset_read(&wm);
    wal_record_t read_rec;
    EXPECT_EQ(DB_OK, wal_read_next(&wm, &read_rec));
    EXPECT_EQ(1, read_rec.lsn);
    EXPECT_EQ(WAL_BEGIN, read_rec.type);
    EXPECT_EQ(1, read_rec.txn_id);
    wal_record_destroy(&read_rec);

    EXPECT_EQ(DB_OK, wal_read_next(&wm, &read_rec));
    EXPECT_EQ(2, read_rec.lsn);
    EXPECT_EQ(WAL_UPDATE, read_rec.type);
    EXPECT_EQ(5, read_rec.page_id);
    wal_record_destroy(&read_rec);

    wal_manager_destroy(&wm);
    unlink(path);
}

/* ---- Transaction Manager Tests ---- */

TEST(txn_manager, begin_commit) {
    const char* path = "/tmp/test_txn_mgr.db";
    const char* wal = "/tmp/test_txn_mgr.wal";
    unlink(path);
    unlink(wal);

    disk_manager_t dm;
    buffer_pool_manager_t bpm;
    disk_manager_open(&dm, path);
    bpm_init(&bpm, 64, &dm);

    lock_manager_t lm;
    lock_manager_init(&lm);

    wal_manager_t wm;
    wal_manager_init(&wm, wal);

    txn_manager_t tm;
    txn_manager_init(&tm, &bpm, &lm, &wm);

    transaction_t* txn = txn_begin(&tm, ISOLATION_READ_COMMITTED);
    EXPECT_NOT_NULL(txn);
    EXPECT_TRUE(txn_is_active(txn));
    EXPECT_EQ(1, txn->txn_id);

    int rc = txn_commit(&tm, txn);
    EXPECT_EQ(DB_OK, rc);
    /* txn is freed by commit */

    txn_manager_destroy(&tm);
    wal_manager_destroy(&wm);
    lock_manager_destroy(&lm);
    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(path);
    unlink(wal);
}

TEST(txn_manager, abort) {
    const char* path = "/tmp/test_txn_abort.db";
    const char* wal = "/tmp/test_txn_abort.wal";
    unlink(path);
    unlink(wal);

    disk_manager_t dm;
    buffer_pool_manager_t bpm;
    disk_manager_open(&dm, path);
    bpm_init(&bpm, 64, &dm);

    lock_manager_t lm;
    lock_manager_init(&lm);

    wal_manager_t wm;
    wal_manager_init(&wm, wal);

    txn_manager_t tm;
    txn_manager_init(&tm, &bpm, &lm, &wm);

    transaction_t* txn = txn_begin(&tm, ISOLATION_READ_COMMITTED);
    EXPECT_NOT_NULL(txn);

    int rc = txn_abort(&tm, txn);
    EXPECT_EQ(DB_OK, rc);

    txn_manager_destroy(&tm);
    wal_manager_destroy(&wm);
    lock_manager_destroy(&lm);
    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(path);
    unlink(wal);
}

/* ---- Recovery Manager Tests ---- */

TEST(recovery, analysis_phase) {
    const char* path = "/tmp/test_recovery.db";
    const char* wal_path2 = "/tmp/test_recovery.wal";
    unlink(path);
    unlink(wal_path2);

    disk_manager_t dm;
    buffer_pool_manager_t bpm;
    disk_manager_open(&dm, path);
    bpm_init(&bpm, 64, &dm);

    /* Write some WAL records */
    wal_manager_t wm;
    wal_manager_init(&wm, wal_path2);

    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.type = WAL_BEGIN;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.type = WAL_UPDATE;
    rec.page_id = 10;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 2;
    rec.type = WAL_BEGIN;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.type = WAL_COMMIT;
    wal_append(&wm, &rec);

    wal_flush(&wm, 4);

    /* Run recovery analysis */
    recovery_manager_t rm;
    recovery_manager_init(&rm, &wm, &bpm);
    int rc = recovery_analysis(&rm);
    EXPECT_EQ(DB_OK, rc);

    /* Verify ATT: txn 1 committed, txn 2 active */
    att_entry_t* e1 = (att_entry_t*)hashmap_get(&rm.att, (void*)(uintptr_t)1);
    att_entry_t* e2 = (att_entry_t*)hashmap_get(&rm.att, (void*)(uintptr_t)2);
    EXPECT_NOT_NULL(e1);
    EXPECT_NOT_NULL(e2);
    if (e1) EXPECT_EQ(1, e1->status);  /* committed */
    if (e2) EXPECT_EQ(0, e2->status);  /* active (needs undo) */

    /* Verify DPT: page 10 is dirty */
    dpt_entry_t* dpt = (dpt_entry_t*)hashmap_get(&rm.dpt, (void*)(uintptr_t)10);
    EXPECT_NOT_NULL(dpt);

    recovery_manager_destroy(&rm);
    wal_manager_destroy(&wm);
    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(path);
    unlink(wal_path2);
}
