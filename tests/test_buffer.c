#include "xtest.h"
#include "src/common/config.h"
#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/mem.h"
#include "src/storage/disk_manager.h"
#include "src/buffer/lru_replacer.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/buffer/page_guard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* =====================================================================
 * Helpers
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
             "/tmp/test_buffer_minisqlite_%d.db", (int)getpid());
    return temp_db_path;
}

/* =====================================================================
 * LRU Replacer Tests
 * ===================================================================== */

TEST(lru_replacer, basic_victim) {
    lru_replacer_t replacer;
    int rc = lru_replacer_init(&replacer, 10);
    EXPECT_EQ(0, rc);

    /* Unpin 10 frames (0-9) — they all go to cold_list */
    for (frame_id_t i = 0; i < 10; i++) {
        lru_replacer_unpin(&replacer, i);
    }
    EXPECT_EQ(10, (int)lru_replacer_size(&replacer));

    /* Victim should pick from cold_list tail in reverse order of insertion.
     * Since we pushed to front, the list front-to-back is [9,8,7,6,5,4,3,2,1,0].
     * Popping from tail gives 0 first, then 1, etc. */
    frame_id_t victim_id;
    for (frame_id_t i = 0; i < 10; i++) {
        int vrc = lru_replacer_victim(&replacer, &victim_id);
        EXPECT_EQ(0, vrc);
        EXPECT_EQ((int)i, (int)victim_id);
    }

    /* No more victims — all frames evicted */
    int vrc = lru_replacer_victim(&replacer, &victim_id);
    EXPECT_NE(0, vrc);

    EXPECT_EQ(0, (int)lru_replacer_size(&replacer));
    lru_replacer_destroy(&replacer);
}

TEST(lru_replacer, pin_unpin) {
    lru_replacer_t replacer;
    lru_replacer_init(&replacer, 5);

    /* Unpin frames 0-4 */
    for (frame_id_t i = 0; i < 5; i++) {
        lru_replacer_unpin(&replacer, i);
    }
    EXPECT_EQ(5, (int)lru_replacer_size(&replacer));

    /* Pin frame 2 — removes it from replacer */
    lru_replacer_pin(&replacer, 2);
    EXPECT_EQ(4, (int)lru_replacer_size(&replacer));

    /* Victims should skip frame 2 */
    frame_id_t victim_id;
    int vrc = lru_replacer_victim(&replacer, &victim_id);
    EXPECT_EQ(0, vrc);
    EXPECT_NE(2, (int)victim_id);

    /* Pin remaining frames so they can't be evicted */
    lru_replacer_pin(&replacer, 0);
    lru_replacer_pin(&replacer, 1);
    lru_replacer_pin(&replacer, 3);
    lru_replacer_pin(&replacer, 4);
    EXPECT_EQ(0, (int)lru_replacer_size(&replacer));

    /* No victim available — all pinned */
    vrc = lru_replacer_victim(&replacer, &victim_id);
    EXPECT_NE(0, vrc);

    /* Unpin frame 3 — now it's the only evictable frame */
    lru_replacer_unpin(&replacer, 3);
    EXPECT_EQ(1, (int)lru_replacer_size(&replacer));

    vrc = lru_replacer_victim(&replacer, &victim_id);
    EXPECT_EQ(0, vrc);
    EXPECT_EQ(3, (int)victim_id);

    lru_replacer_destroy(&replacer);
}

/* =====================================================================
 * Buffer Pool Tests
 * ===================================================================== */

TEST(buffer_pool, fetch_new_page) {
    const char* path = make_temp_db_path();
    unlink(path);

    disk_manager_t dm;
    int rc = disk_manager_open(&dm, path);
    ASSERT_EQ(DB_OK, rc);

    buffer_pool_manager_t bpm;
    rc = bpm_init(&bpm, 5, &dm);
    ASSERT_EQ(0, rc);

    /* Create 5 pages to fill the pool */
    page_id_t page_ids[10];
    for (int i = 0; i < 5; i++) {
        page_t* page = bpm_new_page(&bpm, &page_ids[i]);
        ASSERT_NOT_NULL(page);
        EXPECT_EQ(1, page->pin_count);
        /* Write a pattern into the page data */
        memset(page->data, 'A' + i, PAGE_SIZE);
        page->is_dirty = 1;
        /* Unpin so it can be evicted */
        bpm_unpin_page(&bpm, page_ids[i], 1);
    }

    /* Now fetch 5 more pages — this should trigger LRU eviction */
    for (int i = 5; i < 10; i++) {
        page_t* page = bpm_new_page(&bpm, &page_ids[i]);
        ASSERT_NOT_NULL(page);
        EXPECT_EQ(1, page->pin_count);
        bpm_unpin_page(&bpm, page_ids[i], 0);
    }

    /* Fetch back one of the early pages — it should have been evicted and re-read */
    page_t* page = bpm_fetch_page(&bpm, page_ids[0]);
    ASSERT_NOT_NULL(page);
    EXPECT_EQ(1, page->pin_count);
    /* Verify the data persisted after dirty flush during eviction */
    char expected = 'A';
    EXPECT_EQ(expected, page->data[0]);
    bpm_unpin_page(&bpm, page_ids[0], 0);

    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(path);
}

TEST(buffer_pool, dirty_flush) {
    const char* path = make_temp_db_path();
    unlink(path);

    disk_manager_t dm;
    int rc = disk_manager_open(&dm, path);
    ASSERT_EQ(DB_OK, rc);

    buffer_pool_manager_t bpm;
    rc = bpm_init(&bpm, 5, &dm);
    ASSERT_EQ(0, rc);

    /* Create a new page and write data */
    page_id_t pid;
    page_t* page = bpm_new_page(&bpm, &pid);
    ASSERT_NOT_NULL(page);

    /* Write a distinctive pattern */
    const char* test_str = "BUFFER_FLUSH_TEST";
    memcpy(page->data, test_str, strlen(test_str) + 1);
    page->is_dirty = 1;

    /* Unpin dirty */
    bpm_unpin_page(&bpm, pid, 1);

    /* Explicitly flush */
    rc = bpm_flush_page(&bpm, pid);
    EXPECT_EQ(0, rc);

    /* Read directly from disk to verify persistence */
    char disk_buf[PAGE_SIZE];
    rc = disk_manager_read_page(&dm, pid, disk_buf);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_STR_EQ(test_str, disk_buf);

    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(path);
}

TEST(buffer_pool, pin_count) {
    const char* path = make_temp_db_path();
    unlink(path);

    disk_manager_t dm;
    int rc = disk_manager_open(&dm, path);
    ASSERT_EQ(DB_OK, rc);

    buffer_pool_manager_t bpm;
    rc = bpm_init(&bpm, 5, &dm);
    ASSERT_EQ(0, rc);

    /* Create a page */
    page_id_t pid;
    page_t* page = bpm_new_page(&bpm, &pid);
    ASSERT_NOT_NULL(page);
    EXPECT_EQ(1, page->pin_count);

    /* Fetch the same page again — pin_count should increase */
    page_t* page2 = bpm_fetch_page(&bpm, pid);
    ASSERT_NOT_NULL(page2);
    EXPECT_EQ(2, page->pin_count);
    EXPECT_TRUE(page == page2);  /* same pointer */

    /* Unpin once — pin_count should be 1, page still pinned */
    int urc = bpm_unpin_page(&bpm, pid, 0);
    EXPECT_EQ(0, urc);
    EXPECT_EQ(1, page->pin_count);

    /* Page should not be in replacer yet (still pinned) */
    EXPECT_EQ(0, (int)lru_replacer_size(bpm.replacer));

    /* Unpin again — pin_count goes to 0, page goes to replacer */
    urc = bpm_unpin_page(&bpm, pid, 0);
    EXPECT_EQ(0, urc);
    EXPECT_EQ(0, page->pin_count);
    EXPECT_EQ(1, (int)lru_replacer_size(bpm.replacer));

    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(path);
}

/* =====================================================================
 * Page Guard Tests
 * ===================================================================== */

TEST(page_guard, basic) {
    const char* path = make_temp_db_path();
    unlink(path);

    disk_manager_t dm;
    int rc = disk_manager_open(&dm, path);
    ASSERT_EQ(DB_OK, rc);

    buffer_pool_manager_t bpm;
    rc = bpm_init(&bpm, 5, &dm);
    ASSERT_EQ(0, rc);

    /* Create a page */
    page_id_t pid;
    page_t* page = bpm_new_page(&bpm, &pid);
    ASSERT_NOT_NULL(page);
    EXPECT_EQ(1, page->pin_count);

    /* Create a guard */
    page_guard_t guard = page_guard_create(page, &bpm);
    EXPECT_EQ(0, guard.is_dirty);
    EXPECT_EQ(1, page->pin_count);

    /* Modify the page and mark dirty */
    const char* guard_test_str = "GUARD_TEST";
    memcpy(page->data, guard_test_str, strlen(guard_test_str) + 1);
    page_guard_mark_dirty(&guard);
    EXPECT_EQ(1, guard.is_dirty);
    EXPECT_EQ(1, page->is_dirty);

    /* Release the guard — should unpin the page as dirty */
    page_guard_release(&guard);
    EXPECT_EQ(0, page->pin_count);
    EXPECT_NULL(guard.page);

    /* Flush and verify persistence */
    rc = bpm_flush_page(&bpm, pid);
    EXPECT_EQ(0, rc);

    char disk_buf[PAGE_SIZE];
    rc = disk_manager_read_page(&dm, pid, disk_buf);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_STR_EQ(guard_test_str, disk_buf);

    bpm_destroy(&bpm);
    disk_manager_close(&dm);
    unlink(path);
}
