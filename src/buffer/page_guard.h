#pragma once

#include "src/buffer/buffer_pool_manager.h"

/* Page guard — manual RAII for C.
 *
 * Wraps a page_t* and automatically calls bpm_unpin_page on release.
 * The user can mark the page dirty; the dirty flag is propagated on release.
 *
 * Usage:
 *   page_guard_t guard = page_guard_create(page, bpm);
 *   page_guard_mark_dirty(&guard);
 *   // ... use guard.page ...
 *   page_guard_release(&guard);   // unpins the page
 */

typedef struct {
    page_t*                 page;
    buffer_pool_manager_t*  bpm;
    int                     is_dirty;
} page_guard_t;

page_guard_t page_guard_create(page_t* page, buffer_pool_manager_t* bpm);
void         page_guard_release(page_guard_t* guard);
void         page_guard_mark_dirty(page_guard_t* guard);
