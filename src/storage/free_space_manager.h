#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/vector.h"
#include <stdint.h>

/* ========================================================================
 * Free Space Manager: tracks free space per page using a vector of
 * (page_id, free_space) pairs.
 *
 * Used by the heap file to quickly find a page with enough space.
 * Simplified version — no BPM integration (works with page_id directly).
 * ======================================================================== */

typedef struct {
    page_id_t page_id;
    uint32_t  free_space;
} free_space_entry_t;

typedef struct {
    vector_t entries;   /* vector of free_space_entry_t */
} free_space_manager_t;

/* Initialize the free space manager. */
int  fsm_init(free_space_manager_t* fsm);

/* Destroy the free space manager, freeing all resources. */
void fsm_destroy(free_space_manager_t* fsm);

/* Add or update the free space for a page. */
int  fsm_update(free_space_manager_t* fsm, page_id_t page_id, uint32_t free_space);

/* Remove a page from tracking. */
int  fsm_remove(free_space_manager_t* fsm, page_id_t page_id);

/* Find the best-fit page with at least `required` free space.
 * Returns the page_id or INVALID_PAGE_ID if none found. */
page_id_t fsm_find_page(free_space_manager_t* fsm, uint32_t required);

/* Get the number of tracked pages. */
size_t fsm_count(const free_space_manager_t* fsm);
