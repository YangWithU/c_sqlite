#pragma once

/* Page latch interface — no-op for single-threaded mode.
 * When multi-threading is added, replace with pthread_rwlock. */

typedef struct {
    /* empty for single-thread */
} page_latch_t;

static inline void page_latch_init(page_latch_t* latch) {
    (void)latch;
}

static inline void page_latch_destroy(page_latch_t* latch) {
    (void)latch;
}

static inline void page_latch_read_lock(page_latch_t* latch) {
    (void)latch;
}

static inline void page_latch_read_unlock(page_latch_t* latch) {
    (void)latch;
}

static inline void page_latch_write_lock(page_latch_t* latch) {
    (void)latch;
}

static inline void page_latch_write_unlock(page_latch_t* latch) {
    (void)latch;
}
