#include "src/buffer/page_guard.h"
#include <stddef.h>

page_guard_t page_guard_create(page_t* page, buffer_pool_manager_t* bpm) {
    page_guard_t guard;
    guard.page = page;
    guard.bpm = bpm;
    guard.is_dirty = 0;
    return guard;
}

void page_guard_release(page_guard_t* guard) {
    if (guard->page && guard->bpm) {
        bpm_unpin_page(guard->bpm, guard->page->page_id, guard->is_dirty);
    }
    guard->page = NULL;
    guard->bpm = NULL;
    guard->is_dirty = 0;
}

void page_guard_mark_dirty(page_guard_t* guard) {
    guard->is_dirty = 1;
    if (guard->page) {
        guard->page->is_dirty = 1;
    }
}
