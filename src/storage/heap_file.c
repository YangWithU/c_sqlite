#include "src/storage/heap_file.h"
#include "src/storage/slotted_page.h"
#include "src/storage/page.h"
#include "src/common/mem.h"
#include "src/common/logger.h"

#include <string.h>

int heap_file_init(heap_file_t* hf, disk_manager_t* dm, schema_t* schema,
                   page_id_t first_page_id) {
    if (!hf || !dm || !schema)
        return DB_INVALID_ARGUMENT;

    hf->dm = dm;
    hf->schema = schema;
    hf->first_page_id = first_page_id;
    return DB_OK;
}

/* Ensure there is at least one page in the heap file.
 * If first_page_id is INVALID_PAGE_ID, allocate a new page. */
static int ensure_first_page(heap_file_t* hf) {
    if (hf->first_page_id != INVALID_PAGE_ID)
        return DB_OK;

    int pid = disk_manager_allocate_page(hf->dm);
    if (pid < 0)
        return pid;

    hf->first_page_id = (page_id_t)pid;

    /* Initialize the new page as a slotted page */
    char* page_buf = (char*)page_alloc(1);
    if (!page_buf)
        return DB_OUT_OF_MEMORY;

    int rc = disk_manager_read_page(hf->dm, hf->first_page_id, page_buf);
    if (rc != DB_OK) {
        page_free(page_buf);
        return rc;
    }

    slotted_page_init(page_buf, hf->first_page_id);
    rc = disk_manager_write_page(hf->dm, hf->first_page_id, page_buf);
    page_free(page_buf);
    return rc;
}

/* Find or create a page with enough free space for a tuple of the given size.
 * Returns the page_id or a negative error code.
 * Sets *page_buf to the page data (caller must page_free it). */
static int find_page_with_space(heap_file_t* hf, uint16_t tuple_size, char** page_buf) {
    int rc = ensure_first_page(hf);
    if (rc != DB_OK)
        return rc;

    page_id_t current = hf->first_page_id;
    while (current != INVALID_PAGE_ID) {
        *page_buf = (char*)page_alloc(1);
        if (!*page_buf)
            return DB_OUT_OF_MEMORY;

        rc = disk_manager_read_page(hf->dm, current, *page_buf);
        if (rc != DB_OK) {
            page_free(*page_buf);
            *page_buf = NULL;
            return rc;
        }

        if (slotted_page_free_space(*page_buf) >= (uint32_t)tuple_size + SLOT_SIZE) {
            return current;
        }

        /* Move to next page */
        page_id_t next = page_header_get_next_page(*page_buf);
        page_free(*page_buf);
        *page_buf = NULL;
        current = next;
    }

    /* No existing page has enough space: allocate a new one */
    int pid = disk_manager_allocate_page(hf->dm);
    if (pid < 0)
        return pid;

    *page_buf = (char*)page_alloc(1);
    if (!*page_buf)
        return DB_OUT_OF_MEMORY;

    /* Initialize the new page */
    memset(*page_buf, 0, PAGE_SIZE);
    slotted_page_init(*page_buf, (page_id_t)pid);
    rc = disk_manager_write_page(hf->dm, (page_id_t)pid, *page_buf);
    if (rc != DB_OK) {
        page_free(*page_buf);
        *page_buf = NULL;
        return rc;
    }

    /* Read it back */
    rc = disk_manager_read_page(hf->dm, (page_id_t)pid, *page_buf);
    if (rc != DB_OK) {
        page_free(*page_buf);
        *page_buf = NULL;
        return rc;
    }

    /* Link the new page into the list.
     * We need to find the last page and set its next_page to this new page. */
    page_id_t last = hf->first_page_id;
    char* last_buf = NULL;
    for (;;) {
        if (!last_buf) {
            last_buf = (char*)page_alloc(1);
            if (!last_buf) {
                page_free(*page_buf);
                *page_buf = NULL;
                return DB_OUT_OF_MEMORY;
            }
        }
        rc = disk_manager_read_page(hf->dm, last, last_buf);
        if (rc != DB_OK) {
            page_free(last_buf);
            page_free(*page_buf);
            *page_buf = NULL;
            return rc;
        }
        page_id_t next = page_header_get_next_page(last_buf);
        if (next == INVALID_PAGE_ID) {
            /* This is the last page — link new page here */
            page_header_set_next_page(last_buf, (page_id_t)pid);
            rc = disk_manager_write_page(hf->dm, last, last_buf);
            page_free(last_buf);
            if (rc != DB_OK) {
                page_free(*page_buf);
                *page_buf = NULL;
                return rc;
            }
            break;
        }
        last = next;
    }

    return pid;
}

int heap_file_insert(heap_file_t* hf, const char* tuple_data, uint16_t tuple_size,
                     rid_t* rid) {
    if (!hf || !tuple_data || tuple_size == 0 || !rid)
        return DB_INVALID_ARGUMENT;

    char* page_buf = NULL;
    int pid = find_page_with_space(hf, tuple_size, &page_buf);
    if (pid < 0 || !page_buf)
        return (pid < 0) ? pid : DB_INTERNAL_ERROR;

    int slot_id = slotted_page_insert(page_buf, tuple_data, tuple_size);
    if (slot_id < 0) {
        page_free(page_buf);
        return slot_id;
    }

    int rc = disk_manager_write_page(hf->dm, (page_id_t)pid, page_buf);
    page_free(page_buf);
    if (rc != DB_OK)
        return rc;

    rid->page_id = (page_id_t)pid;
    rid->slot_id = (slot_id_t)slot_id;
    return DB_OK;
}

int heap_file_delete(heap_file_t* hf, rid_t rid) {
    if (!hf)
        return DB_INVALID_ARGUMENT;
    if (rid.page_id == INVALID_PAGE_ID || rid.slot_id == INVALID_SLOT_ID)
        return DB_INVALID_ARGUMENT;

    char* page_buf = (char*)page_alloc(1);
    if (!page_buf)
        return DB_OUT_OF_MEMORY;

    int rc = disk_manager_read_page(hf->dm, rid.page_id, page_buf);
    if (rc != DB_OK) {
        page_free(page_buf);
        return rc;
    }

    rc = slotted_page_delete(page_buf, rid.slot_id);
    if (rc != DB_OK) {
        page_free(page_buf);
        return rc;
    }

    rc = disk_manager_write_page(hf->dm, rid.page_id, page_buf);
    page_free(page_buf);
    return rc;
}

int heap_file_update(heap_file_t* hf, rid_t rid,
                     const char* new_data, uint16_t new_size) {
    if (!hf || !new_data || new_size == 0)
        return DB_INVALID_ARGUMENT;
    if (rid.page_id == INVALID_PAGE_ID || rid.slot_id == INVALID_SLOT_ID)
        return DB_INVALID_ARGUMENT;

    char* page_buf = (char*)page_alloc(1);
    if (!page_buf)
        return DB_OUT_OF_MEMORY;

    int rc = disk_manager_read_page(hf->dm, rid.page_id, page_buf);
    if (rc != DB_OK) {
        page_free(page_buf);
        return rc;
    }

    rc = slotted_page_update(page_buf, rid.slot_id, new_data, new_size);
    if (rc != DB_OK) {
        page_free(page_buf);
        return rc;
    }

    rc = disk_manager_write_page(hf->dm, rid.page_id, page_buf);
    page_free(page_buf);
    return rc;
}

/* Static buffer for heap_file_get (valid until next call).
 * This is a simple approach since we don't have a BPM yet. */
static char* hf_get_buf = NULL;

int heap_file_get(heap_file_t* hf, rid_t rid,
                  const char** tuple_data, uint16_t* tuple_size) {
    if (!hf || !tuple_data || !tuple_size)
        return DB_INVALID_ARGUMENT;
    if (rid.page_id == INVALID_PAGE_ID || rid.slot_id == INVALID_SLOT_ID)
        return DB_INVALID_ARGUMENT;

    if (hf_get_buf) {
        page_free(hf_get_buf);
        hf_get_buf = NULL;
    }

    hf_get_buf = (char*)page_alloc(1);
    if (!hf_get_buf)
        return DB_OUT_OF_MEMORY;

    int rc = disk_manager_read_page(hf->dm, rid.page_id, hf_get_buf);
    if (rc != DB_OK)
        return rc;

    return slotted_page_get(hf_get_buf, rid.slot_id, tuple_data, tuple_size);
}

/* ---- Heap Iterator ---- */

int heap_iter_init(heap_iter_t* it, heap_file_t* hf) {
    if (!it || !hf)
        return DB_INVALID_ARGUMENT;

    it->hf = hf;
    it->current_page_id = hf->first_page_id;
    it->current_slot = 0;
    it->page_buf = NULL;
    it->page_loaded = 0;
    return DB_OK;
}

int heap_iter_next(heap_iter_t* it, rid_t* rid,
                   const char** tuple_data, uint16_t* tuple_size) {
    if (!it || !it->hf)
        return DB_INVALID_ARGUMENT;

    while (it->current_page_id != INVALID_PAGE_ID) {
        /* Load the current page if needed */
        if (!it->page_loaded) {
            if (it->page_buf) {
                page_free(it->page_buf);
                it->page_buf = NULL;
            }
            it->page_buf = (char*)page_alloc(1);
            if (!it->page_buf)
                return DB_OUT_OF_MEMORY;

            int rc = disk_manager_read_page(it->hf->dm, it->current_page_id, it->page_buf);
            if (rc != DB_OK) {
                page_free(it->page_buf);
                it->page_buf = NULL;
                return rc;
            }
            it->page_loaded = 1;
            it->current_slot = 0;
        }

        /* Scan slots in the current page */
        uint32_t num_slots = page_header_get_num_tuples(it->page_buf);
        while (it->current_slot < num_slots) {
            uint16_t size = slot_get_size(it->page_buf, it->current_slot);
            if (size > 0) {
                /* Found a live tuple */
                uint16_t offset = slot_get_offset(it->page_buf, it->current_slot);
                if (rid) {
                    rid->page_id = it->current_page_id;
                    rid->slot_id = (slot_id_t)it->current_slot;
                }
                if (tuple_data)
                    *tuple_data = it->page_buf + offset;
                if (tuple_size)
                    *tuple_size = size;
                it->current_slot++;
                return DB_OK;
            }
            it->current_slot++;
        }

        /* Move to the next page */
        it->current_page_id = page_header_get_next_page(it->page_buf);
        it->page_loaded = 0;
    }

    return DB_PAGE_NOT_FOUND;  /* end of iteration */
}

void heap_iter_destroy(heap_iter_t* it) {
    if (it) {
        if (it->page_buf) {
            page_free(it->page_buf);
            it->page_buf = NULL;
        }
        it->page_loaded = 0;
    }
}
