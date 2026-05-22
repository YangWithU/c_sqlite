#include "xtest.h"
#include "src/common/config.h"
#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/mem.h"
#include "src/common/vector.h"
#include "src/storage/disk_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/index/b_plus_tree.h"
#include "src/index/b_plus_tree_node.h"
#include "src/index/index_iterator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* =====================================================================
 * Helpers: temporary database + buffer pool + B+ tree setup
 * ===================================================================== */

static char temp_db_path[256];

static void cleanup_temp_files(void) {
    if (temp_db_path[0])
        unlink(temp_db_path);
}

static const char* make_temp_db_path(void) {
    static int initialized = 0;
    if (!initialized) {
        atexit(cleanup_temp_files);
        initialized = 1;
    }
    snprintf(temp_db_path, sizeof(temp_db_path),
             "/tmp/test_bptree_minisqlite_%d.db", (int)getpid());
    return temp_db_path;
}

/* Shared test fixture: creates a temp db, buffer pool, and B+ tree.
 * Caller must call teardown_test_env to clean up. */
typedef struct {
    disk_manager_t        dm;
    buffer_pool_manager_t bpm;
    b_plus_tree_t         tree;
} test_env_t;

static int setup_test_env(test_env_t* env, int pool_size) {
    const char* path = make_temp_db_path();
    unlink(path);

    int rc = disk_manager_open(&env->dm, path);
    if (rc != DB_OK) return rc;

    rc = bpm_init(&env->bpm, (size_t)pool_size, &env->dm);
    if (rc != 0) {
        disk_manager_close(&env->dm);
        return DB_INTERNAL_ERROR;
    }

    rc = bpt_init(&env->tree, 1, INVALID_PAGE_ID, &env->bpm, 1 /* unique */);
    if (rc != DB_OK) {
        bpm_destroy(&env->bpm);
        disk_manager_close(&env->dm);
        return rc;
    }

    return DB_OK;
}

static void teardown_test_env(test_env_t* env) {
    bpt_destroy(&env->tree);
    bpm_destroy(&env->bpm);
    disk_manager_close(&env->dm);
    unlink(make_temp_db_path());
}

/* Helper: make an rid from page_id and slot_id */
static rid_t make_rid(int page, int slot) {
    rid_t r;
    r.page_id = page;
    r.slot_id = slot;
    return r;
}

/* =====================================================================
 * TEST: bplus_tree, insert_find
 * Insert 1000 sequential keys and verify each can be found.
 * ===================================================================== */

TEST(bplus_tree, insert_find) {
    test_env_t env;
    int rc = setup_test_env(&env, 256);
    ASSERT_EQ(rc, DB_OK);

    const int N = 1000;
    for (int i = 0; i < N; i++) {
        rid_t rid = make_rid(i / 10, i % 10);
        rc = bpt_insert(&env.tree, (int64_t)i, rid);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Verify all keys can be found */
    for (int i = 0; i < N; i++) {
        rid_t out_rid;
        rc = bpt_find(&env.tree, (int64_t)i, &out_rid);
        EXPECT_EQ(rc, DB_OK);
        EXPECT_EQ(out_rid.page_id, i / 10);
        EXPECT_EQ(out_rid.slot_id, i % 10);
    }

    /* Verify a non-existent key is not found */
    rid_t out_rid;
    rc = bpt_find(&env.tree, (int64_t)9999, &out_rid);
    EXPECT_EQ(rc, DB_PAGE_NOT_FOUND);

    teardown_test_env(&env);
}

/* =====================================================================
 * TEST: bplus_tree, insert_random
 * Insert 500 random keys and verify each can be found.
 * ===================================================================== */

TEST(bplus_tree, insert_random) {
    test_env_t env;
    int rc = setup_test_env(&env, 256);
    ASSERT_EQ(rc, DB_OK);

    /* Use a simple LCG PRNG seeded with current time for reproducibility */
    unsigned int seed = (unsigned int)time(NULL);
    const int N = 500;

    /* Generate unique random keys */
    int64_t* keys = (int64_t*)malloc(N * sizeof(int64_t));
    ASSERT_NOT_NULL(keys);

    /* Fill with sequential keys then shuffle (Fisher-Yates) */
    for (int i = 0; i < N; i++) {
        keys[i] = i + 1;
    }
    for (int i = N - 1; i > 0; i--) {
        int j = rand_r(&seed) % (i + 1);
        int64_t tmp = keys[i];
        keys[i] = keys[j];
        keys[j] = tmp;
    }

    /* Insert all keys */
    for (int i = 0; i < N; i++) {
        rid_t rid = make_rid((int)keys[i], (int)keys[i]);
        rc = bpt_insert(&env.tree, keys[i], rid);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Verify all keys can be found */
    for (int i = 0; i < N; i++) {
        rid_t out_rid;
        rc = bpt_find(&env.tree, keys[i], &out_rid);
        EXPECT_EQ(rc, DB_OK);
        EXPECT_EQ(out_rid.page_id, (int)keys[i]);
        EXPECT_EQ(out_rid.slot_id, (int)keys[i]);
    }

    free(keys);
    teardown_test_env(&env);
}

/* =====================================================================
 * TEST: bplus_tree, split
 * Insert enough keys to cause multiple splits, then verify tree is correct.
 * ===================================================================== */

TEST(bplus_tree, split) {
    test_env_t env;
    int rc = setup_test_env(&env, 512);
    ASSERT_EQ(rc, DB_OK);

    /* Insert more keys than one leaf can hold to force splits.
     * With ~253 keys per leaf and ~338 keys per internal node,
     * inserting 5000 keys will cause many splits. */
    const int N = 5000;
    for (int i = 0; i < N; i++) {
        rid_t rid = make_rid(i, i);
        rc = bpt_insert(&env.tree, (int64_t)i, rid);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Verify all keys can be found */
    for (int i = 0; i < N; i++) {
        rid_t out_rid;
        rc = bpt_find(&env.tree, (int64_t)i, &out_rid);
        EXPECT_EQ(rc, DB_OK);
        EXPECT_EQ(out_rid.page_id, i);
        EXPECT_EQ(out_rid.slot_id, i);
    }

    /* Verify non-existent keys are not found */
    rid_t out_rid;
    rc = bpt_find(&env.tree, (int64_t)(N + 100), &out_rid);
    EXPECT_EQ(rc, DB_PAGE_NOT_FOUND);

    rc = bpt_find(&env.tree, (int64_t)(-1), &out_rid);
    EXPECT_EQ(rc, DB_PAGE_NOT_FOUND);

    teardown_test_env(&env);
}

/* =====================================================================
 * TEST: bplus_tree, range_scan
 * Insert 100 keys, range scan [25..75], verify results.
 * ===================================================================== */

TEST(bplus_tree, range_scan) {
    test_env_t env;
    int rc = setup_test_env(&env, 256);
    ASSERT_EQ(rc, DB_OK);

    const int N = 100;
    for (int i = 0; i < N; i++) {
        rid_t rid = make_rid(i, i);
        rc = bpt_insert(&env.tree, (int64_t)i, rid);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Range scan [25, 75] */
    vector_t results;
    vector_init(&results, sizeof(rid_t));

    rc = bpt_find_range(&env.tree, 25, 75, &results);
    EXPECT_EQ(rc, DB_OK);

    /* Should get 51 results (keys 25 through 75 inclusive) */
    size_t count = vector_size(&results);
    EXPECT_EQ((int)count, 51);

    /* Verify all results are in range and sorted */
    for (size_t i = 0; i < count; i++) {
        rid_t* r = (rid_t*)vector_at(&results, i);
        EXPECT_NOT_NULL(r);
        int expected_key = 25 + (int)i;
        EXPECT_EQ(r->page_id, expected_key);
        EXPECT_EQ(r->slot_id, expected_key);
    }

    vector_destroy(&results);
    teardown_test_env(&env);
}

/* =====================================================================
 * TEST: bplus_tree, unique_constraint
 * Insert key=1, try again, expect DB_DUPLICATE_KEY.
 * ===================================================================== */

TEST(bplus_tree, unique_constraint) {
    test_env_t env;
    int rc = setup_test_env(&env, 256);
    ASSERT_EQ(rc, DB_OK);

    rid_t rid1 = make_rid(1, 1);
    rc = bpt_insert(&env.tree, 1, rid1);
    EXPECT_EQ(rc, DB_OK);

    /* Try inserting the same key again — should fail with duplicate key */
    rid_t rid2 = make_rid(2, 2);
    rc = bpt_insert(&env.tree, 1, rid2);
    EXPECT_EQ(rc, DB_DUPLICATE_KEY);

    /* Original value should be unchanged */
    rid_t out_rid;
    rc = bpt_find(&env.tree, 1, &out_rid);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(out_rid.page_id, 1);
    EXPECT_EQ(out_rid.slot_id, 1);

    /* Test non-unique tree allows duplicates */
    b_plus_tree_t tree_nu;
    rc = bpt_init(&tree_nu, 2, INVALID_PAGE_ID, &env.bpm, 0 /* not unique */);
    EXPECT_EQ(rc, DB_OK);

    rc = bpt_insert(&tree_nu, 1, rid1);
    EXPECT_EQ(rc, DB_OK);

    rc = bpt_insert(&tree_nu, 1, rid2);
    EXPECT_EQ(rc, DB_OK);  /* non-unique: should succeed (updates rid) */

    /* Find should return the updated rid */
    rc = bpt_find(&tree_nu, 1, &out_rid);
    EXPECT_EQ(rc, DB_OK);
    EXPECT_EQ(out_rid.page_id, 2);
    EXPECT_EQ(out_rid.slot_id, 2);

    bpt_destroy(&tree_nu);
    teardown_test_env(&env);
}

/* =====================================================================
 * TEST: bplus_tree, remove
 * Insert keys, remove some, verify finds fail for removed keys.
 * ===================================================================== */

TEST(bplus_tree, remove) {
    test_env_t env;
    int rc = setup_test_env(&env, 256);
    ASSERT_EQ(rc, DB_OK);

    const int N = 200;
    for (int i = 0; i < N; i++) {
        rid_t rid = make_rid(i, i);
        rc = bpt_insert(&env.tree, (int64_t)i, rid);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Remove even-numbered keys */
    for (int i = 0; i < N; i += 2) {
        rc = bpt_remove(&env.tree, (int64_t)i);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Verify removed keys are not found */
    for (int i = 0; i < N; i += 2) {
        rid_t out_rid;
        rc = bpt_find(&env.tree, (int64_t)i, &out_rid);
        EXPECT_EQ(rc, DB_PAGE_NOT_FOUND);
    }

    /* Verify odd keys are still found */
    for (int i = 1; i < N; i += 2) {
        rid_t out_rid;
        rc = bpt_find(&env.tree, (int64_t)i, &out_rid);
        EXPECT_EQ(rc, DB_OK);
        EXPECT_EQ(out_rid.page_id, i);
        EXPECT_EQ(out_rid.slot_id, i);
    }

    /* Remove a key that doesn't exist */
    rc = bpt_remove(&env.tree, (int64_t)9999);
    EXPECT_EQ(rc, DB_PAGE_NOT_FOUND);

    teardown_test_env(&env);
}

/* =====================================================================
 * TEST: index_iterator, forward_scan
 * Full forward scan of all keys in the tree.
 * ===================================================================== */

TEST(index_iterator, forward_scan) {
    test_env_t env;
    int rc = setup_test_env(&env, 256);
    ASSERT_EQ(rc, DB_OK);

    const int N = 500;
    for (int i = 0; i < N; i++) {
        rid_t rid = make_rid(i, i);
        rc = bpt_insert(&env.tree, (int64_t)i, rid);
        EXPECT_EQ(rc, DB_OK);
    }

    /* Create iterator scanning from 0 to N-1 (full scan) */
    index_iterator_t it;
    rc = index_iterator_init(&it, &env.bpm, env.tree.root_page_id, 0, N - 1);
    EXPECT_EQ(rc, DB_OK);

    /* Iterate through all entries */
    int count = 0;
    int64_t prev_key = -1;
    while (1) {
        int64_t key;
        rid_t rid;
        int has_next = index_iterator_next(&it, &key, &rid);
        if (!has_next) break;

        /* Verify keys are in ascending order */
        EXPECT_TRUE(key > prev_key);
        EXPECT_EQ((int)key, count);
        EXPECT_EQ(rid.page_id, count);
        EXPECT_EQ(rid.slot_id, count);

        prev_key = key;
        count++;
    }

    EXPECT_EQ(count, N);

    index_iterator_destroy(&it);

    /* Test partial range scan with iterator: keys 100..199 */
    rc = index_iterator_init(&it, &env.bpm, env.tree.root_page_id, 100, 199);
    EXPECT_EQ(rc, DB_OK);

    count = 0;
    while (1) {
        int64_t key;
        rid_t rid;
        int has_next = index_iterator_next(&it, &key, &rid);
        if (!has_next) break;

        EXPECT_TRUE(key >= 100 && key <= 199);
        count++;
    }

    EXPECT_EQ(count, 100);  /* 100 entries: 100..199 */

    index_iterator_destroy(&it);
    teardown_test_env(&env);
}
