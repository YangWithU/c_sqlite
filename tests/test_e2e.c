#include "xtest.h"
#include "src/executor/execution_engine.h"
#include "src/transaction/txn_manager.h"
#include "src/transaction/lock_manager.h"
#include "src/transaction/deadlock_detector.h"
#include "src/transaction/wal_manager.h"
#include "src/transaction/recovery_manager.h"
#include "src/storage/disk_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/catalog/catalog.h"
#include "src/parser/lexer.h"
#include "src/parser/parser.h"
#include "src/parser/ast.h"
#include "src/storage/tuple.h"
#include "src/storage/value.h"
#include "src/tools/sql_runner.h"
#include "src/common/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- Helpers ---- */

static char temp_db[256];
static char temp_wal[256];

static void e2e_cleanup(void) {
    if (temp_db[0]) unlink(temp_db);
    if (temp_wal[0]) unlink(temp_wal);
}

static const char* make_db_path(void) {
    static int init = 0;
    if (!init) { atexit(e2e_cleanup); init = 1; }
    snprintf(temp_db, sizeof(temp_db), "/tmp/test_e2e_%d.db", (int)getpid());
    snprintf(temp_wal, sizeof(temp_wal), "/tmp/test_e2e_%d.wal", (int)getpid());
    return temp_db;
}

typedef struct {
    disk_manager_t        dm;
    buffer_pool_manager_t bpm;
    catalog_t             cat;
    execution_engine_t    engine;
    lock_manager_t        lm;
    wal_manager_t         wm;
    txn_manager_t         tm;
    int                   use_txn;  /* 1 = include txn components */
} e2e_env_t;

static int e2e_setup(e2e_env_t* env, int use_txn) {
    const char* db_path = make_db_path();
    const char* wal_path = temp_wal;
    unlink(db_path);
    unlink(wal_path);

    if (disk_manager_open(&env->dm, db_path) != DB_OK) return -1;
    if (bpm_init(&env->bpm, 64, &env->dm) != DB_OK) return -1;
    if (catalog_init(&env->cat, &env->bpm) != DB_OK) return -1;
    if (execution_engine_init(&env->engine, &env->cat, &env->bpm) != DB_OK) return -1;

    env->use_txn = use_txn;
    if (use_txn) {
        lock_manager_init(&env->lm);
        wal_manager_init(&env->wm, wal_path);
        txn_manager_init(&env->tm, &env->bpm, &env->lm, &env->wm);
    }

    return 0;
}

static void e2e_teardown(e2e_env_t* env) {
    if (env->use_txn) {
        txn_manager_destroy(&env->tm);
        wal_manager_destroy(&env->wm);
        lock_manager_destroy(&env->lm);
    }
    execution_engine_destroy(&env->engine);
    catalog_destroy(&env->cat);
    bpm_destroy(&env->bpm);
    disk_manager_close(&env->dm);
    unlink(make_db_path());
    unlink(temp_wal);
}

static stmt_t* parse_sql(const char* sql) {
    lexer_t lex;
    lexer_init(&lex, sql);
    token_t* tokens = NULL;
    int count = 0;
    if (lexer_tokenize(&lex, &tokens, &count) != 0) return NULL;
    parser_t parser;
    parser_init(&parser, tokens, count);
    stmt_t* s = parser_parse(&parser);
    db_free(tokens);
    return s;
}

static int exec_sql(e2e_env_t* env, const char* sql,
                    vector_t* tuples, schema_t** schema, int* affected) {
    stmt_t* stmt = parse_sql(sql);
    if (!stmt) return DB_INTERNAL_ERROR;
    int rc = execution_engine_execute(&env->engine, stmt, tuples, schema, affected);
    ast_destroy_stmt(stmt);
    return rc;
}

static void free_tuples(vector_t* tuples) {
    for (size_t i = 0; i < vector_size(tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(tuples);
}

/* ---- E2E Tests ---- */

TEST(e2e, create_insert_select) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    /* Create table */
    EXPECT_EQ(DB_OK, exec_sql(&env,
        "CREATE TABLE users (id INTEGER, name VARCHAR(64), age INTEGER)", NULL, NULL, NULL));

    /* Insert rows */
    EXPECT_EQ(DB_OK, exec_sql(&env, "INSERT INTO users VALUES (1, 'Alice', 30)", NULL, NULL, NULL));
    EXPECT_EQ(DB_OK, exec_sql(&env, "INSERT INTO users VALUES (2, 'Bob', 25)", NULL, NULL, NULL));
    EXPECT_EQ(DB_OK, exec_sql(&env, "INSERT INTO users VALUES (3, 'Charlie', 35)", NULL, NULL, NULL));

    /* Select all */
    vector_t tuples;
    schema_t* schema = NULL;
    vector_init(&tuples, sizeof(tuple_t));
    int rc = exec_sql(&env, "SELECT * FROM users", &tuples, &schema, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(3, (int)vector_size(&tuples));
    free_tuples(&tuples);
    if (schema) { schema_destroy(schema); db_free(schema); }

    e2e_teardown(&env);
}

TEST(e2e, select_where) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    exec_sql(&env, "CREATE TABLE t (id INTEGER, val INTEGER)", NULL, NULL, NULL);
    for (int i = 1; i <= 10; i++) {
        char sql[128];
        snprintf(sql, sizeof(sql), "INSERT INTO t VALUES (%d, %d)", i, i * 10);
        exec_sql(&env, sql, NULL, NULL, NULL);
    }

    /* Filter: val > 50 */
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    int rc = exec_sql(&env, "SELECT * FROM t WHERE val > 50", &tuples, NULL, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(5, (int)vector_size(&tuples));  /* 60, 70, 80, 90, 100 */
    free_tuples(&tuples);

    /* Filter: id = 5 */
    vector_init(&tuples, sizeof(tuple_t));
    rc = exec_sql(&env, "SELECT * FROM t WHERE id = 5", &tuples, NULL, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(1, (int)vector_size(&tuples));
    free_tuples(&tuples);

    e2e_teardown(&env);
}

TEST(e2e, update_and_verify) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    exec_sql(&env, "CREATE TABLE t (id INTEGER, score INTEGER)", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO t VALUES (1, 90)", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO t VALUES (2, 85)", NULL, NULL, NULL);

    /* Update */
    int affected = 0;
    EXPECT_EQ(DB_OK, exec_sql(&env, "UPDATE t SET score = 95 WHERE id = 1", NULL, NULL, &affected));
    EXPECT_EQ(1, affected);

    /* Verify */
    vector_t tuples;
    schema_t* schema = NULL;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM t WHERE id = 1", &tuples, &schema, NULL);
    EXPECT_EQ(1, (int)vector_size(&tuples));
    if (vector_size(&tuples) > 0) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, 0);
        const value_t* v = tuple_get_value(t, 1);
        ASSERT_NOT_NULL(v);
        if (v->type == TYPE_INTEGER) {
            EXPECT_EQ(95, v->val.int_val);
        }
    }
    free_tuples(&tuples);
    if (schema) { schema_destroy(schema); db_free(schema); }

    e2e_teardown(&env);
}

TEST(e2e, delete_and_verify) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    exec_sql(&env, "CREATE TABLE t (id INTEGER, name VARCHAR(32))", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO t VALUES (1, 'Alice')", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO t VALUES (2, 'Bob')", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO t VALUES (3, 'Charlie')", NULL, NULL, NULL);

    /* Delete one */
    int affected = 0;
    EXPECT_EQ(DB_OK, exec_sql(&env, "DELETE FROM t WHERE id = 2", NULL, NULL, &affected));
    EXPECT_EQ(1, affected);

    /* Verify 2 remain */
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM t", &tuples, NULL, NULL);
    EXPECT_EQ(2, (int)vector_size(&tuples));
    free_tuples(&tuples);

    e2e_teardown(&env);
}

TEST(e2e, limit) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    exec_sql(&env, "CREATE TABLE t (id INTEGER)", NULL, NULL, NULL);
    for (int i = 1; i <= 20; i++) {
        char sql[64];
        snprintf(sql, sizeof(sql), "INSERT INTO t VALUES (%d)", i);
        exec_sql(&env, sql, NULL, NULL, NULL);
    }

    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM t LIMIT 5", &tuples, NULL, NULL);
    EXPECT_EQ(5, (int)vector_size(&tuples));
    free_tuples(&tuples);

    e2e_teardown(&env);
}

TEST(e2e, drop_table) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    exec_sql(&env, "CREATE TABLE t (id INTEGER)", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO t VALUES (1)", NULL, NULL, NULL);

    /* Drop */
    EXPECT_EQ(DB_OK, exec_sql(&env, "DROP TABLE t", NULL, NULL, NULL));

    /* Select from dropped table should fail */
    int rc = exec_sql(&env, "SELECT * FROM t", NULL, NULL, NULL);
    EXPECT_NE(DB_OK, rc);

    e2e_teardown(&env);
}

TEST(e2e, multiple_tables) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    exec_sql(&env, "CREATE TABLE users (id INTEGER, name VARCHAR(32))", NULL, NULL, NULL);
    exec_sql(&env, "CREATE TABLE orders (id INTEGER, user_id INTEGER, amount INTEGER)", NULL, NULL, NULL);

    exec_sql(&env, "INSERT INTO users VALUES (1, 'Alice')", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO users VALUES (2, 'Bob')", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO orders VALUES (100, 1, 50)", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO orders VALUES (101, 1, 75)", NULL, NULL, NULL);
    exec_sql(&env, "INSERT INTO orders VALUES (102, 2, 30)", NULL, NULL, NULL);

    /* Verify each table independently */
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM users", &tuples, NULL, NULL);
    EXPECT_EQ(2, (int)vector_size(&tuples));
    free_tuples(&tuples);

    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM orders", &tuples, NULL, NULL);
    EXPECT_EQ(3, (int)vector_size(&tuples));
    free_tuples(&tuples);

    e2e_teardown(&env);
}

TEST(e2e, transaction_lifecycle) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 1));

    exec_sql(&env, "CREATE TABLE t (id INTEGER, val INTEGER)", NULL, NULL, NULL);

    /* Begin, commit */
    transaction_t* txn = txn_begin(&env.tm, ISOLATION_READ_COMMITTED);
    EXPECT_NOT_NULL(txn);
    EXPECT_TRUE(txn_is_active(txn));
    EXPECT_EQ(1, txn->txn_id);

    int rc = txn_commit(&env.tm, txn);
    EXPECT_EQ(DB_OK, rc);

    /* Begin, abort */
    txn = txn_begin(&env.tm, ISOLATION_READ_COMMITTED);
    EXPECT_NOT_NULL(txn);
    rc = txn_abort(&env.tm, txn);
    EXPECT_EQ(DB_OK, rc);

    e2e_teardown(&env);
}

TEST(e2e, lock_acquire_release) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 1));

    /* Two transactions acquire shared locks on same resource */
    int rc = lock_acquire(&env.lm, 1, 100, LOCK_SHARED);
    EXPECT_EQ(DB_OK, rc);

    rc = lock_acquire(&env.lm, 2, 100, LOCK_SHARED);
    EXPECT_EQ(DB_OK, rc);

    /* Exclusive lock should conflict */
    rc = lock_acquire(&env.lm, 3, 100, LOCK_EXCLUSIVE);
    EXPECT_EQ(DB_LOCK_CONFLICT, rc);

    /* Release shared locks */
    lock_release_all(&env.lm, 1);
    lock_release_all(&env.lm, 2);

    /* Now exclusive should succeed */
    rc = lock_acquire(&env.lm, 3, 100, LOCK_EXCLUSIVE);
    EXPECT_EQ(DB_OK, rc);

    lock_release_all(&env.lm, 3);
    e2e_teardown(&env);
}

TEST(e2e, deadlock_detection) {
    deadlock_detector_t dd;
    deadlock_detector_init(&dd);

    /* No cycle: T1->T2->T3 */
    deadlock_add_edge(&dd, 1, 2);
    deadlock_add_edge(&dd, 2, 3);
    EXPECT_EQ(INVALID_TXN_ID, deadlock_detect(&dd));

    /* Add cycle: T3->T1 */
    deadlock_add_edge(&dd, 3, 1);
    txn_id_t victim = deadlock_detect(&dd);
    EXPECT_NE(INVALID_TXN_ID, victim);

    deadlock_remove_edges(&dd, victim);
    deadlock_detector_destroy(&dd);
}

TEST(e2e, wal_roundtrip) {
    const char* path = "/tmp/test_e2e_wal.wal";
    unlink(path);

    wal_manager_t wm;
    wal_manager_init(&wm, path);

    /* Write BEGIN, UPDATE, COMMIT */
    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.type = WAL_BEGIN;
    lsn_t lsn1 = wal_append(&wm, &rec);
    EXPECT_EQ(1, lsn1);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.prev_lsn = lsn1;
    rec.type = WAL_UPDATE;
    rec.page_id = 10;
    rec.offset = 0;
    const char* before = "aaaa";
    const char* after = "bbbb";
    rec.before_image = (char*)before;
    rec.before_size = 4;
    rec.after_image = (char*)after;
    rec.after_size = 4;
    lsn_t lsn2 = wal_append(&wm, &rec);
    EXPECT_EQ(2, lsn2);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1;
    rec.prev_lsn = lsn2;
    rec.type = WAL_COMMIT;
    wal_append(&wm, &rec);

    wal_flush(&wm, 3);

    /* Read back and verify */
    wal_reset_read(&wm);
    wal_record_t read_rec;
    EXPECT_EQ(DB_OK, wal_read_next(&wm, &read_rec));
    EXPECT_EQ(WAL_BEGIN, read_rec.type);
    EXPECT_EQ(1, read_rec.txn_id);
    wal_record_destroy(&read_rec);

    EXPECT_EQ(DB_OK, wal_read_next(&wm, &read_rec));
    EXPECT_EQ(WAL_UPDATE, read_rec.type);
    EXPECT_EQ(10, read_rec.page_id);
    wal_record_destroy(&read_rec);

    EXPECT_EQ(DB_OK, wal_read_next(&wm, &read_rec));
    EXPECT_EQ(WAL_COMMIT, read_rec.type);
    wal_record_destroy(&read_rec);

    wal_manager_destroy(&wm);
    unlink(path);
}

TEST(e2e, recovery_analysis) {
    const char* db_path = "/tmp/test_e2e_recovery.db";
    const char* wal_path = "/tmp/test_e2e_recovery.wal";
    unlink(db_path);
    unlink(wal_path);

    disk_manager_t dm;
    buffer_pool_manager_t bpm;
    disk_manager_open(&dm, db_path);
    bpm_init(&bpm, 64, &dm);

    /* Write WAL: BEGIN(T1), UPDATE(T1, page 5), BEGIN(T2), COMMIT(T1) */
    wal_manager_t wm;
    wal_manager_init(&wm, wal_path);

    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1; rec.type = WAL_BEGIN;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1; rec.type = WAL_UPDATE; rec.page_id = 5;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 2; rec.type = WAL_BEGIN;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1; rec.type = WAL_COMMIT;
    wal_append(&wm, &rec);

    wal_flush(&wm, 4);

    /* Run analysis */
    recovery_manager_t rm;
    recovery_manager_init(&rm, &wm, &bpm);
    int rc = recovery_analysis(&rm);
    EXPECT_EQ(DB_OK, rc);

    /* T1 committed, T2 still active */
    att_entry_t* e1 = (att_entry_t*)hashmap_get(&rm.att, (void*)(uintptr_t)1);
    att_entry_t* e2 = (att_entry_t*)hashmap_get(&rm.att, (void*)(uintptr_t)2);
    EXPECT_NOT_NULL(e1);
    EXPECT_NOT_NULL(e2);
    if (e1) EXPECT_EQ(1, e1->status);   /* committed */
    if (e2) EXPECT_EQ(0, e2->status);   /* active */

    /* Page 5 is dirty */
    dpt_entry_t* dpt = (dpt_entry_t*)hashmap_get(&rm.dpt, (void*)(uintptr_t)5);
    EXPECT_NOT_NULL(dpt);

    recovery_manager_destroy(&rm);
    wal_manager_destroy(&wm);
    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(db_path);
    unlink(wal_path);
}

TEST(e2e, sql_runner_execute) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    sql_runner_t runner;
    sql_runner_init(&runner, &env.engine);

    /* Execute SQL string */
    int rc = sql_runner_execute_string(&runner, "CREATE TABLE t (id INTEGER, val INTEGER)");
    EXPECT_EQ(0, rc);

    rc = sql_runner_execute_string(&runner, "INSERT INTO t VALUES (1, 10)");
    EXPECT_EQ(0, rc);

    rc = sql_runner_execute_string(&runner, "INSERT INTO t VALUES (2, 20)");
    EXPECT_EQ(0, rc);

    sql_runner_destroy(&runner);

    /* Verify data is there */
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM t", &tuples, NULL, NULL);
    EXPECT_EQ(2, (int)vector_size(&tuples));
    free_tuples(&tuples);

    e2e_teardown(&env);
}

TEST(e2e, sql_runner_file) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    /* Write a temp SQL file */
    const char* sql_path = "/tmp/test_e2e_sql.sql";
    FILE* f = fopen(sql_path, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "CREATE TABLE nums (n INTEGER);\n");
    fprintf(f, "INSERT INTO nums VALUES (1);\n");
    fprintf(f, "INSERT INTO nums VALUES (2);\n");
    fprintf(f, "INSERT INTO nums VALUES (3);\n");
    fclose(f);

    sql_runner_t runner;
    sql_runner_init(&runner, &env.engine);
    int rc = sql_runner_execute_file(&runner, sql_path);
    EXPECT_EQ(0, rc);
    sql_runner_destroy(&runner);

    /* Verify */
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM nums", &tuples, NULL, NULL);
    EXPECT_EQ(3, (int)vector_size(&tuples));
    free_tuples(&tuples);

    unlink(sql_path);
    e2e_teardown(&env);
}

TEST(e2e, full_crud_workflow) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 0));

    /* CREATE */
    exec_sql(&env, "CREATE TABLE products (id INTEGER, name VARCHAR(32), price INTEGER)", NULL, NULL, NULL);

    /* INSERT 5 rows */
    for (int i = 1; i <= 5; i++) {
        char sql[128];
        snprintf(sql, sizeof(sql), "INSERT INTO products VALUES (%d, 'item%d', %d)", i, i, i * 100);
        EXPECT_EQ(DB_OK, exec_sql(&env, sql, NULL, NULL, NULL));
    }

    /* SELECT all */
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM products", &tuples, NULL, NULL);
    EXPECT_EQ(5, (int)vector_size(&tuples));
    free_tuples(&tuples);

    /* SELECT with filter */
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM products WHERE price > 200", &tuples, NULL, NULL);
    EXPECT_EQ(3, (int)vector_size(&tuples));  /* 300, 400, 500 */
    free_tuples(&tuples);

    /* UPDATE */
    int affected = 0;
    exec_sql(&env, "UPDATE products SET price = 999 WHERE id = 1", NULL, NULL, &affected);
    EXPECT_EQ(1, affected);

    /* Verify update */
    schema_t* schema = NULL;
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM products WHERE id = 1", &tuples, &schema, NULL);
    EXPECT_EQ(1, (int)vector_size(&tuples));
    if (vector_size(&tuples) > 0) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, 0);
        const value_t* v = tuple_get_value(t, 2);
        ASSERT_NOT_NULL(v);
        if (v->type == TYPE_INTEGER) {
            EXPECT_EQ(999, v->val.int_val);
        }
    }
    free_tuples(&tuples);
    if (schema) { schema_destroy(schema); db_free(schema); }

    /* DELETE */
    affected = 0;
    exec_sql(&env, "DELETE FROM products WHERE id = 3", NULL, NULL, &affected);
    EXPECT_EQ(1, affected);

    /* Verify 4 remain */
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM products", &tuples, NULL, NULL);
    EXPECT_EQ(4, (int)vector_size(&tuples));
    free_tuples(&tuples);

    /* LIMIT */
    vector_init(&tuples, sizeof(tuple_t));
    exec_sql(&env, "SELECT * FROM products LIMIT 2", &tuples, NULL, NULL);
    EXPECT_EQ(2, (int)vector_size(&tuples));
    free_tuples(&tuples);

    e2e_teardown(&env);
}

TEST(e2e, transaction_with_locking) {
    e2e_env_t env;
    ASSERT_EQ(0, e2e_setup(&env, 1));

    exec_sql(&env, "CREATE TABLE t (id INTEGER, val INTEGER)", NULL, NULL, NULL);

    /* Txn 1 begins and acquires a shared lock */
    transaction_t* txn1 = txn_begin(&env.tm, ISOLATION_READ_COMMITTED);
    EXPECT_NOT_NULL(txn1);
    int rc = lock_acquire(&env.lm, txn1->txn_id, 1, LOCK_SHARED);
    EXPECT_EQ(DB_OK, rc);

    /* Txn 2 begins and also acquires shared lock (compatible) */
    transaction_t* txn2 = txn_begin(&env.tm, ISOLATION_READ_COMMITTED);
    EXPECT_NOT_NULL(txn2);
    rc = lock_acquire(&env.lm, txn2->txn_id, 1, LOCK_SHARED);
    EXPECT_EQ(DB_OK, rc);

    /* Txn 3 tries exclusive — should conflict */
    rc = lock_acquire(&env.lm, 99, 1, LOCK_EXCLUSIVE);
    EXPECT_EQ(DB_LOCK_CONFLICT, rc);

    /* Commit txn1 and txn2 — releases their locks */
    txn_commit(&env.tm, txn1);
    txn_commit(&env.tm, txn2);

    /* Now exclusive should succeed */
    rc = lock_acquire(&env.lm, 99, 1, LOCK_EXCLUSIVE);
    EXPECT_EQ(DB_OK, rc);
    lock_release_all(&env.lm, 99);

    e2e_teardown(&env);
}

TEST(e2e, multi_txn_wal) {
    const char* db_path = "/tmp/test_e2e_multi_txn.db";
    const char* wal_path = "/tmp/test_e2e_multi_txn.wal";
    unlink(db_path);
    unlink(wal_path);

    disk_manager_t dm;
    buffer_pool_manager_t bpm;
    disk_manager_open(&dm, db_path);
    bpm_init(&bpm, 64, &dm);

    wal_manager_t wm;
    wal_manager_init(&wm, wal_path);

    txn_manager_t tm;
    lock_manager_t lm;
    lock_manager_init(&lm);
    txn_manager_init(&tm, &bpm, &lm, &wm);

    /* Begin two transactions */
    transaction_t* txn1 = txn_begin(&tm, ISOLATION_READ_COMMITTED);
    transaction_t* txn2 = txn_begin(&tm, ISOLATION_READ_COMMITTED);
    EXPECT_NOT_NULL(txn1);
    EXPECT_NOT_NULL(txn2);
    EXPECT_EQ(1, txn1->txn_id);
    EXPECT_EQ(2, txn2->txn_id);

    /* Commit both */
    EXPECT_EQ(DB_OK, txn_commit(&tm, txn1));
    EXPECT_EQ(DB_OK, txn_commit(&tm, txn2));

    /* WAL should have 4 records: BEGIN1, BEGIN2, COMMIT1, COMMIT2 */
    wal_flush(&wm, 4);

    /* Verify WAL contents */
    wal_reset_read(&wm);
    wal_record_t rec;
    int count = 0;
    while (wal_read_next(&wm, &rec) == DB_OK) {
        count++;
        wal_record_destroy(&rec);
    }
    EXPECT_EQ(4, count);

    txn_manager_destroy(&tm);
    wal_manager_destroy(&wm);
    lock_manager_destroy(&lm);
    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(db_path);
    unlink(wal_path);
}

TEST(e2e, recovery_full_pipeline) {
    const char* db_path = "/tmp/test_e2e_recovery2.db";
    const char* wal_path = "/tmp/test_e2e_recovery2.wal";
    unlink(db_path);
    unlink(wal_path);

    disk_manager_t dm;
    buffer_pool_manager_t bpm;
    disk_manager_open(&dm, db_path);
    bpm_init(&bpm, 64, &dm);

    /* Write WAL with committed and active transactions */
    wal_manager_t wm;
    wal_manager_init(&wm, wal_path);

    wal_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1; rec.type = WAL_BEGIN;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1; rec.type = WAL_UPDATE; rec.page_id = 10;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 1; rec.type = WAL_COMMIT;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 2; rec.type = WAL_BEGIN;
    wal_append(&wm, &rec);

    memset(&rec, 0, sizeof(rec));
    rec.txn_id = 2; rec.type = WAL_UPDATE; rec.page_id = 20;
    wal_append(&wm, &rec);

    /* Txn 2 is still active (no commit) — simulates crash */

    wal_flush(&wm, 5);

    /* Run full recovery */
    recovery_manager_t rm;
    recovery_manager_init(&rm, &wm, &bpm);
    int rc = recovery_run(&rm);
    EXPECT_EQ(DB_OK, rc);

    /* Verify: T1 committed, T2 should be marked as aborted (active at crash) */
    att_entry_t* e1 = (att_entry_t*)hashmap_get(&rm.att, (void*)(uintptr_t)1);
    att_entry_t* e2 = (att_entry_t*)hashmap_get(&rm.att, (void*)(uintptr_t)2);
    if (e1) EXPECT_EQ(1, e1->status);   /* committed */
    if (e2) EXPECT_EQ(2, e2->status);   /* aborted by undo */

    /* DPT should have pages 10 and 20 */
    dpt_entry_t* dpt10 = (dpt_entry_t*)hashmap_get(&rm.dpt, (void*)(uintptr_t)10);
    dpt_entry_t* dpt20 = (dpt_entry_t*)hashmap_get(&rm.dpt, (void*)(uintptr_t)20);
    EXPECT_NOT_NULL(dpt10);
    EXPECT_NOT_NULL(dpt20);

    recovery_manager_destroy(&rm);
    wal_manager_destroy(&wm);
    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(db_path);
    unlink(wal_path);
}
