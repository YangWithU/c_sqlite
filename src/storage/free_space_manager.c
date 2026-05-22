#include "src/storage/free_space_manager.h"
#include "src/common/mem.h"

#include <string.h>

int fsm_init(free_space_manager_t* fsm) {
    if (!fsm)
        return DB_INVALID_ARGUMENT;
    vector_init(&fsm->entries, sizeof(free_space_entry_t));
    return DB_OK;
}

void fsm_destroy(free_space_manager_t* fsm) {
    if (fsm) {
        vector_destroy(&fsm->entries);
    }
}

int fsm_update(free_space_manager_t* fsm, page_id_t page_id, uint32_t free_space) {
    if (!fsm)
        return DB_INVALID_ARGUMENT;
    if (page_id == INVALID_PAGE_ID)
        return DB_INVALID_ARGUMENT;

    /* Check if page_id already exists */
    size_t n = vector_size(&fsm->entries);
    for (size_t i = 0; i < n; i++) {
        free_space_entry_t* entry = (free_space_entry_t*)vector_at(&fsm->entries, i);
        if (entry->page_id == page_id) {
            entry->free_space = free_space;
            return DB_OK;
        }
    }

    /* Not found: add new entry */
    free_space_entry_t new_entry;
    new_entry.page_id = page_id;
    new_entry.free_space = free_space;
    return vector_push(&fsm->entries, &new_entry);
}

int fsm_remove(free_space_manager_t* fsm, page_id_t page_id) {
    if (!fsm)
        return DB_INVALID_ARGUMENT;

    size_t n = vector_size(&fsm->entries);
    for (size_t i = 0; i < n; i++) {
        free_space_entry_t* entry = (free_space_entry_t*)vector_at(&fsm->entries, i);
        if (entry->page_id == page_id) {
            /* Swap with last element and pop (avoid shifting) */
            free_space_entry_t* last = (free_space_entry_t*)vector_at(&fsm->entries, n - 1);
            if (i != n - 1) {
                *entry = *last;
            }
            vector_pop(&fsm->entries);
            return DB_OK;
        }
    }

    return DB_PAGE_NOT_FOUND;
}

page_id_t fsm_find_page(free_space_manager_t* fsm, uint32_t required) {
    if (!fsm)
        return INVALID_PAGE_ID;

    /* Best-fit: find the page with the least free space that still meets the requirement */
    page_id_t best = INVALID_PAGE_ID;
    uint32_t best_space = UINT32_MAX;

    size_t n = vector_size(&fsm->entries);
    for (size_t i = 0; i < n; i++) {
        const free_space_entry_t* entry = (const free_space_entry_t*)vector_at(&fsm->entries, i);
        if (entry->free_space >= required && entry->free_space < best_space) {
            best = entry->page_id;
            best_space = entry->free_space;
        }
    }

    return best;
}

size_t fsm_count(const free_space_manager_t* fsm) {
    if (!fsm)
        return 0;
    return vector_size(&fsm->entries);
}
