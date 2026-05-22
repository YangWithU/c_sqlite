#pragma once

#include "src/common/config.h"
#include "src/common/types.h"
#include "src/common/error.h"
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * Page header layout (24 bytes) — all access via memcpy, no struct mapping.
 *
 * Offset  Size  Field
 *   0       4   page_id         (int32)
 *   4       8   page_lsn        (int64)
 *  12       4   num_tuples      (uint32)
 *  16       4   free_space      (uint32)
 *  20       4   next_page_id    (int32)
 * ======================================================================== */

/* ---------- Read helpers ---------- */

static inline int32_t page_header_get_page_id(const char* page) {
    int32_t v;
    memcpy(&v, page + PAGE_HEADER_OFFSET_PAGE_ID, sizeof(v));
    return v;
}

static inline int64_t page_header_get_lsn(const char* page) {
    int64_t v;
    memcpy(&v, page + PAGE_HEADER_OFFSET_PAGE_LSN, sizeof(v));
    return v;
}

static inline uint32_t page_header_get_num_tuples(const char* page) {
    uint32_t v;
    memcpy(&v, page + PAGE_HEADER_OFFSET_NUM_TUPLES, sizeof(v));
    return v;
}

static inline uint32_t page_header_get_free_space(const char* page) {
    uint32_t v;
    memcpy(&v, page + PAGE_HEADER_OFFSET_FREE_SPACE, sizeof(v));
    return v;
}

static inline int32_t page_header_get_next_page(const char* page) {
    int32_t v;
    memcpy(&v, page + PAGE_HEADER_OFFSET_NEXT_PAGE, sizeof(v));
    return v;
}

/* ---------- Write helpers ---------- */

static inline void page_header_set_page_id(char* page, int32_t page_id) {
    memcpy(page + PAGE_HEADER_OFFSET_PAGE_ID, &page_id, sizeof(page_id));
}

static inline void page_header_set_lsn(char* page, int64_t lsn) {
    memcpy(page + PAGE_HEADER_OFFSET_PAGE_LSN, &lsn, sizeof(lsn));
}

static inline void page_header_set_num_tuples(char* page, uint32_t num) {
    memcpy(page + PAGE_HEADER_OFFSET_NUM_TUPLES, &num, sizeof(num));
}

static inline void page_header_set_free_space(char* page, uint32_t free_sp) {
    memcpy(page + PAGE_HEADER_OFFSET_FREE_SPACE, &free_sp, sizeof(free_sp));
}

static inline void page_header_set_next_page(char* page, int32_t next) {
    memcpy(page + PAGE_HEADER_OFFSET_NEXT_PAGE, &next, sizeof(next));
}

/* ---------- Initialize a blank page header ---------- */

static inline void page_header_init(char* page, int32_t page_id) {
    memset(page, 0, PAGE_HEADER_SIZE);
    page_header_set_page_id(page, page_id);
    page_header_set_lsn(page, INVALID_LSN);
    page_header_set_num_tuples(page, 0);
    page_header_set_free_space(page, PAGE_SIZE - PAGE_HEADER_SIZE);
    page_header_set_next_page(page, INVALID_PAGE_ID);
}

/* ========================================================================
 * Slot access — each slot is 4 bytes:
 *   offset 0: uint16 tuple_offset
 *   offset 2: uint16 tuple_size   (0 = deleted)
 * ======================================================================== */

static inline char* slot_ptr(char* page, uint32_t slot_index) {
    return page + PAGE_HEADER_SIZE + slot_index * SLOT_SIZE;
}

static inline uint16_t slot_get_offset(const char* page, uint32_t slot_index) {
    uint16_t v;
    memcpy(&v, page + PAGE_HEADER_SIZE + slot_index * SLOT_SIZE
               + SLOT_OFFSET_TUPLE_OFFSET, sizeof(v));
    return v;
}

static inline uint16_t slot_get_size(const char* page, uint32_t slot_index) {
    uint16_t v;
    memcpy(&v, page + PAGE_HEADER_SIZE + slot_index * SLOT_SIZE
               + SLOT_OFFSET_TUPLE_SIZE, sizeof(v));
    return v;
}

static inline void slot_set_offset(char* page, uint32_t slot_index, uint16_t offset) {
    memcpy(page + PAGE_HEADER_SIZE + slot_index * SLOT_SIZE
               + SLOT_OFFSET_TUPLE_OFFSET, &offset, sizeof(offset));
}

static inline void slot_set_size(char* page, uint32_t slot_index, uint16_t size) {
    memcpy(page + PAGE_HEADER_SIZE + slot_index * SLOT_SIZE
               + SLOT_OFFSET_TUPLE_SIZE, &size, sizeof(size));
}
