#pragma once

#include "src/common/types.h"
#include "src/common/config.h"
#include "src/common/error.h"
#include <stdint.h>

/* ========================================================================
 * Slotted page operations.
 *
 * Layout within a page buffer:
 *   [Page header (24B)] [Slot array (grows forward)] ... [free space] ...
 *   ... [Tuple data (grows backward from page end)]
 *
 * Each slot: 4 bytes = (uint16 tuple_offset, uint16 tuple_size)
 *   size == 0 means the slot is deleted (tombstone).
 * ======================================================================== */

/* Initialize a page buffer as a blank slotted page.
 * Sets header fields and clears all slots. */
void slotted_page_init(char* page, page_id_t page_id);

/* Insert a tuple into the page.
 * Returns the slot_id (>= 0) or negative error code.
 * tuple_size must be > 0. */
int slotted_page_insert(char* page, const char* tuple_data, uint16_t tuple_size);

/* Get a pointer to tuple data and its size from a slot.
 * Returns DB_OK on success, DB_PAGE_NOT_FOUND if slot_id is invalid,
 * or error code if slot is deleted. */
int slotted_page_get(const char* page, slot_id_t slot_id,
                     const char** tuple_data, uint16_t* tuple_size);

/* Delete a tuple from a slot (sets size to 0, tombstone).
 * The freed space is accounted for but not compacted.
 * Returns DB_OK or error code. */
int slotted_page_delete(char* page, slot_id_t slot_id);

/* Update a tuple in a slot with new data.
 * If the new data fits in the old slot's space, reuses it (in-place).
 * Otherwise, allocates new space (old slot becomes dead space).
 * Returns DB_OK or error code. */
int slotted_page_update(char* page, slot_id_t slot_id,
                        const char* new_data, uint16_t new_size);

/* Return the amount of usable free space in the page. */
uint32_t slotted_page_free_space(const char* page);

/* Return the number of tuples (slots in use, excluding deleted). */
uint32_t slotted_page_num_tuples(const char* page);
