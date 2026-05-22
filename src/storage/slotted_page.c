#include "src/storage/slotted_page.h"
#include "src/storage/page.h"

#include <string.h>

void slotted_page_init(char* page, page_id_t page_id) {
    page_header_init(page, page_id);
}

/* Compute the boundary between the slot array and the free space.
 * The slot array grows forward from PAGE_HEADER_SIZE.
 * lower_bound = PAGE_HEADER_SIZE + num_slots * SLOT_SIZE */
static uint32_t slot_array_end(const char* page) {
    return PAGE_HEADER_SIZE + page_header_get_num_tuples(page) * SLOT_SIZE;
}

/* Get the lower free space boundary (end of slot array). */
static uint32_t free_space_lower(const char* page) {
    return slot_array_end(page);
}

/* Get the upper free space boundary (start of tuple data area).
 * This is the minimum tuple_offset among all active (non-deleted) slots,
 * or PAGE_SIZE if no tuples exist. */
static uint32_t free_space_upper(const char* page) {
    uint32_t num_slots = page_header_get_num_tuples(page);
    uint32_t upper = PAGE_SIZE;
    for (uint32_t i = 0; i < num_slots; i++) {
        uint16_t size = slot_get_size(page, i);
        if (size == 0) continue;  /* deleted slot */
        uint16_t offset = slot_get_offset(page, i);
        if (offset < upper)
            upper = offset;
    }
    return upper;
}

uint32_t slotted_page_free_space(const char* page) {
    uint32_t lower = free_space_lower(page);
    uint32_t upper = free_space_upper(page);
    return (upper > lower) ? (upper - lower) : 0;
}

uint32_t slotted_page_num_tuples(const char* page) {
    uint32_t num_slots = page_header_get_num_tuples(page);
    uint32_t count = 0;
    for (uint32_t i = 0; i < num_slots; i++) {
        if (slot_get_size(page, i) > 0)
            count++;
    }
    return count;
}

int slotted_page_insert(char* page, const char* tuple_data, uint16_t tuple_size) {
    if (!page || !tuple_data || tuple_size == 0)
        return DB_INVALID_ARGUMENT;

    /* We need space for both the new slot and the tuple data.
     * New slot goes at end of existing slot array.
     * Tuple data goes at the upper boundary of free space, growing downward. */
    uint32_t num_slots = page_header_get_num_tuples(page);
    uint32_t new_slot_array_end = PAGE_HEADER_SIZE + (num_slots + 1) * SLOT_SIZE;
    uint32_t upper = free_space_upper(page);

    /* Check if there is enough contiguous free space.
     * Free space region is [slot_array_end, upper).
     * We need: tuple_size bytes for data + SLOT_SIZE bytes for the new slot
     * (slot is at the front, data at the back). */
    uint32_t available = (upper > new_slot_array_end) ? (upper - new_slot_array_end) : 0;
    if (available < tuple_size)
        return DB_OUT_OF_MEMORY;

    /* Place tuple data at upper - tuple_size */
    uint16_t tuple_offset = (uint16_t)(upper - tuple_size);
    memcpy(page + tuple_offset, tuple_data, tuple_size);

    /* Set up the new slot */
    slot_set_offset(page, num_slots, tuple_offset);
    slot_set_size(page, num_slots, tuple_size);

    /* Update header */
    page_header_set_num_tuples(page, num_slots + 1);
    page_header_set_free_space(page, slotted_page_free_space(page));

    return (int)num_slots;  /* return slot_id (0-indexed) */
}

int slotted_page_get(const char* page, slot_id_t slot_id,
                     const char** tuple_data, uint16_t* tuple_size) {
    if (!page || !tuple_data || !tuple_size)
        return DB_INVALID_ARGUMENT;

    uint32_t num_slots = page_header_get_num_tuples(page);
    if (slot_id < 0 || (uint32_t)slot_id >= num_slots)
        return DB_PAGE_NOT_FOUND;

    uint16_t size = slot_get_size(page, (uint32_t)slot_id);
    if (size == 0)
        return DB_PAGE_NOT_FOUND;  /* deleted slot */

    uint16_t offset = slot_get_offset(page, (uint32_t)slot_id);
    *tuple_data = page + offset;
    *tuple_size = size;
    return DB_OK;
}

int slotted_page_delete(char* page, slot_id_t slot_id) {
    if (!page)
        return DB_INVALID_ARGUMENT;

    uint32_t num_slots = page_header_get_num_tuples(page);
    if (slot_id < 0 || (uint32_t)slot_id >= num_slots)
        return DB_PAGE_NOT_FOUND;

    uint16_t size = slot_get_size(page, (uint32_t)slot_id);
    if (size == 0)
        return DB_PAGE_NOT_FOUND;  /* already deleted */

    /* Mark slot as deleted by setting size to 0 */
    slot_set_size(page, (uint32_t)slot_id, 0);

    /* Update free space in header */
    page_header_set_free_space(page, slotted_page_free_space(page));

    return DB_OK;
}

int slotted_page_update(char* page, slot_id_t slot_id,
                        const char* new_data, uint16_t new_size) {
    if (!page || !new_data || new_size == 0)
        return DB_INVALID_ARGUMENT;

    uint32_t num_slots = page_header_get_num_tuples(page);
    if (slot_id < 0 || (uint32_t)slot_id >= num_slots)
        return DB_PAGE_NOT_FOUND;

    uint16_t old_size = slot_get_size(page, (uint32_t)slot_id);
    if (old_size == 0)
        return DB_PAGE_NOT_FOUND;  /* deleted slot */

    uint16_t old_offset = slot_get_offset(page, (uint32_t)slot_id);

    if (new_size <= old_size) {
        /* In-place update: new data fits in old slot space */
        memcpy(page + old_offset, new_data, new_size);
        slot_set_size(page, (uint32_t)slot_id, new_size);
        page_header_set_free_space(page, slotted_page_free_space(page));
        return DB_OK;
    }

    /* New data is larger: need to allocate new space.
     * Old slot becomes dead space (not reclaimed without compaction). */
    uint32_t upper = free_space_upper(page);
    uint32_t new_slot_array_end = slot_array_end(page);
    uint32_t available = (upper > new_slot_array_end) ? (upper - new_slot_array_end) : 0;
    if (available < new_size)
        return DB_OUT_OF_MEMORY;

    uint16_t new_offset = (uint16_t)(upper - new_size);
    memcpy(page + new_offset, new_data, new_size);

    /* Update slot to point to new location */
    slot_set_offset(page, (uint32_t)slot_id, new_offset);
    slot_set_size(page, (uint32_t)slot_id, new_size);

    page_header_set_free_space(page, slotted_page_free_space(page));
    return DB_OK;
}
