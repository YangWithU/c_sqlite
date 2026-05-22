#include "src/executor/bpm_heap.h"
#include "src/storage/slotted_page.h"
#include "src/storage/page.h"
#include "src/common/mem.h"
#include "src/common/logger.h"
#include <string.h>

int bpm_heap_init(bpm_heap_t* hf, buffer_pool_manager_t* bpm, schema_t* schema,
                   table_id_t table_id, page_id_t first_page_id) {
    if (!hf || !bpm || !schema)
        return DB_INVALID_ARGUMENT;
    hf->bpm = bpm;
    hf->schema = schema;
    hf->table_id = table_id;
    hf->first_page_id = first_page_id;
    return DB_OK;
}

/* Ensure there is at least one page in the heap file. */
static int ensure_first_page(bpm_heap_t* hf) {
    if (hf->first_page_id != INVALID_PAGE_ID)
        return DB_OK;

    page_id_t new_pid;
    page_t* page = bpm_new_page(hf->bpm, &new_pid);
    if (!page)
        return DB_OUT_OF_MEMORY;

    slotted_page_init(page->data, new_pid);
    hf->first_page_id = new_pid;
    bpm_unpin_page(hf->bpm, new_pid, 1);  /* dirty because we initialized it */
    return DB_OK;
}

/* Find or create a page with enough free space.
 * Returns the page_id and pins the page (caller must unpin). */
static int find_page_with_space(bpm_heap_t* hf, uint16_t tuple_size,
                                page_id_t* out_pid, page_t** out_page) {
    int rc = ensure_first_page(hf);
    if (rc != DB_OK) return rc;

    page_id_t current = hf->first_page_id;
    while (current != INVALID_PAGE_ID) {
        page_t* page = bpm_fetch_page(hf->bpm, current);
        if (!page) return DB_PAGE_NOT_FOUND;

        if (slotted_page_free_space(page->data) >= (uint32_t)tuple_size + SLOT_SIZE) {
            *out_pid = current;
            *out_page = page;
            return DB_OK;
        }

        page_id_t next = page_header_get_next_page(page->data);
        bpm_unpin_page(hf->bpm, current, 0);
        current = next;
    }

    /* No existing page has space — allocate a new one */
    page_id_t new_pid;
    page_t* new_page = bpm_new_page(hf->bpm, &new_pid);
    if (!new_page) return DB_OUT_OF_MEMORY;

    slotted_page_init(new_page->data, new_pid);

    /* Link: find last page and set its next_page */
    page_id_t last = hf->first_page_id;
    for (;;) {
        page_t* last_pg = bpm_fetch_page(hf->bpm, last);
        if (!last_pg) {
            bpm_unpin_page(hf->bpm, new_pid, 1);
            return DB_PAGE_NOT_FOUND;
        }
        page_id_t next = page_header_get_next_page(last_pg->data);
        if (next == INVALID_PAGE_ID) {
            page_header_set_next_page(last_pg->data, new_pid);
            bpm_unpin_page(hf->bpm, last, 1);  /* dirty */
            break;
        }
        bpm_unpin_page(hf->bpm, last, 0);
        last = next;
    }

    *out_pid = new_pid;
    *out_page = new_page;
    return DB_OK;
}

int bpm_heap_insert(bpm_heap_t* hf, const char* tuple_data, uint16_t tuple_size,
                     rid_t* rid) {
    if (!hf || !tuple_data || tuple_size == 0 || !rid)
        return DB_INVALID_ARGUMENT;

    page_id_t pid;
    page_t* page;
    int rc = find_page_with_space(hf, tuple_size, &pid, &page);
    if (rc != DB_OK) return rc;

    int slot_id = slotted_page_insert(page->data, tuple_data, tuple_size);
    if (slot_id < 0) {
        bpm_unpin_page(hf->bpm, pid, 0);
        return slot_id;
    }

    bpm_unpin_page(hf->bpm, pid, 1);  /* dirty */

    rid->page_id = pid;
    rid->slot_id = (slot_id_t)slot_id;
    return DB_OK;
}

int bpm_heap_delete(bpm_heap_t* hf, rid_t rid) {
    if (!hf) return DB_INVALID_ARGUMENT;
    if (rid.page_id == INVALID_PAGE_ID || rid.slot_id == INVALID_SLOT_ID)
        return DB_INVALID_ARGUMENT;

    page_t* page = bpm_fetch_page(hf->bpm, rid.page_id);
    if (!page) return DB_PAGE_NOT_FOUND;

    int rc = slotted_page_delete(page->data, rid.slot_id);
    bpm_unpin_page(hf->bpm, rid.page_id, rc == DB_OK ? 1 : 0);
    return rc;
}

int bpm_heap_update(bpm_heap_t* hf, rid_t rid,
                     const char* new_data, uint16_t new_size) {
    if (!hf || !new_data || new_size == 0)
        return DB_INVALID_ARGUMENT;
    if (rid.page_id == INVALID_PAGE_ID || rid.slot_id == INVALID_SLOT_ID)
        return DB_INVALID_ARGUMENT;

    page_t* page = bpm_fetch_page(hf->bpm, rid.page_id);
    if (!page) return DB_PAGE_NOT_FOUND;

    int rc = slotted_page_update(page->data, rid.slot_id, new_data, new_size);
    bpm_unpin_page(hf->bpm, rid.page_id, rc == DB_OK ? 1 : 0);
    return rc;
}

int bpm_heap_get(bpm_heap_t* hf, rid_t rid,
                  const char** tuple_data, uint16_t* tuple_size, page_t** pinned_page) {
    if (!hf || !tuple_data || !tuple_size || !pinned_page)
        return DB_INVALID_ARGUMENT;
    if (rid.page_id == INVALID_PAGE_ID || rid.slot_id == INVALID_SLOT_ID)
        return DB_INVALID_ARGUMENT;

    page_t* page = bpm_fetch_page(hf->bpm, rid.page_id);
    if (!page) return DB_PAGE_NOT_FOUND;

    int rc = slotted_page_get(page->data, rid.slot_id, tuple_data, tuple_size);
    if (rc != DB_OK) {
        bpm_unpin_page(hf->bpm, rid.page_id, 0);
        return rc;
    }

    *pinned_page = page;
    return DB_OK;
}

void bpm_heap_release_get(bpm_heap_t* hf, page_t* pinned_page) {
    if (hf && pinned_page) {
        bpm_unpin_page(hf->bpm, pinned_page->page_id, 0);
    }
}

/* ---- Iterator ---- */

int bpm_heap_iter_init(bpm_heap_iter_t* it, bpm_heap_t* hf) {
    if (!it || !hf) return DB_INVALID_ARGUMENT;
    it->heap = hf;
    it->current_page_id = hf->first_page_id;
    it->current_slot = 0;
    it->current_page = NULL;
    return DB_OK;
}

int bpm_heap_iter_next(bpm_heap_iter_t* it, rid_t* rid,
                        const char** tuple_data, uint16_t* tuple_size) {
    if (!it || !it->heap) return DB_INVALID_ARGUMENT;

    while (it->current_page_id != INVALID_PAGE_ID) {
        /* Pin the current page if needed */
        if (!it->current_page) {
            it->current_page = bpm_fetch_page(it->heap->bpm, it->current_page_id);
            if (!it->current_page) return DB_PAGE_NOT_FOUND;
            it->current_slot = 0;
        }

        uint32_t num_slots = page_header_get_num_tuples(it->current_page->data);
        while (it->current_slot < num_slots) {
            uint16_t size = slot_get_size(it->current_page->data, it->current_slot);
            if (size > 0) {
                uint16_t offset = slot_get_offset(it->current_page->data, it->current_slot);
                if (rid) {
                    rid->page_id = it->current_page_id;
                    rid->slot_id = (slot_id_t)it->current_slot;
                }
                if (tuple_data)
                    *tuple_data = it->current_page->data + offset;
                if (tuple_size)
                    *tuple_size = size;
                it->current_slot++;
                return DB_OK;
            }
            it->current_slot++;
        }

        /* Move to next page */
        page_id_t next = page_header_get_next_page(it->current_page->data);
        bpm_unpin_page(it->heap->bpm, it->current_page_id, 0);
        it->current_page = NULL;
        it->current_page_id = next;
    }

    return DB_PAGE_NOT_FOUND;  /* end of iteration */
}

void bpm_heap_iter_destroy(bpm_heap_iter_t* it) {
    if (it && it->current_page) {
        bpm_unpin_page(it->heap->bpm, it->current_page_id, 0);
        it->current_page = NULL;
    }
}
