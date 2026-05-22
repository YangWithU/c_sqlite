#pragma once

#include "src/common/types.h"
#include "src/common/config.h"
#include <stdint.h>
#include <string.h>

/* B+ tree node type constants */
#define BP_NODE_TYPE_INTERNAL  1
#define BP_NODE_TYPE_LEAF      2

/* --- Generic node header accessors ---
 * Layout on page:
 *   [PageHeader(24B)] [node_type(1B)] [size(4B)]
 * For leaf, after size: [next_page_id(4B)]
 * For internal, after size: direct entries
 *
 * Offsets (relative to page data start):
 *   BP_TREE_NODE_TYPE_OFFSET  = 24   (from config.h)
 *   BP_TREE_NODE_SIZE_OFFSET  = 25   (from config.h)
 *   Leaf next_page_id offset  = 29
 */

#define BP_LEAF_NEXT_PAGE_OFFSET  (BP_TREE_NODE_SIZE_OFFSET + 4)
#define BP_LEAF_ENTRIES_OFFSET    (BP_LEAF_NEXT_PAGE_OFFSET + 4)
/* Leaf entry: key(8B) + rid(8B: page_id 4B + slot_id 4B) = 16B */
#define BP_LEAF_ENTRY_SIZE        (BP_TREE_KEY_SIZE + 8)  /* 16 */

/* Internal entries start right after the size field */
#define BP_INTERNAL_ENTRIES_OFFSET  (BP_TREE_NODE_SIZE_OFFSET + 4)
/* Internal: child_0(4B) [key_0(8B) child_1(4B)] ... */
/* First child is at entries offset, then alternating key+child pairs */
#define BP_INTERNAL_CHILD_SIZE      4
#define BP_INTERNAL_ENTRY_SIZE      (BP_TREE_KEY_SIZE + BP_INTERNAL_CHILD_SIZE) /* 12 */

/* ---- Generic node accessors ---- */

static inline uint8_t bp_node_get_type(const char* page) {
    return (uint8_t)page[BP_TREE_NODE_TYPE_OFFSET];
}

static inline void bp_node_set_type(char* page, uint8_t type) {
    page[BP_TREE_NODE_TYPE_OFFSET] = (char)type;
}

static inline int32_t bp_node_get_size(const char* page) {
    int32_t val;
    memcpy(&val, page + BP_TREE_NODE_SIZE_OFFSET, sizeof(int32_t));
    return val;
}

static inline void bp_node_set_size(char* page, int32_t size) {
    memcpy(page + BP_TREE_NODE_SIZE_OFFSET, &size, sizeof(int32_t));
}

/* ---- Internal node operations ---- */

/* Internal layout (after node header):
 *   [child_0(4B)] [key_0(8B) child_1(4B)] [key_1(8B) child_2(4B)] ...
 * So for n keys, there are n+1 children.
 * child_i is at:  INTERNAL_ENTRIES_OFFSET + i * (KEY_SIZE + CHILD_SIZE)  (but first is special)
 * Actually, simpler layout:
 *   [child_0] [key_0 child_1] [key_1 child_2] ...
 *   child_0  at offset 0 (relative to entries)
 *   key_i    at offset 4 + i * 12
 *   child_i+1 at offset 4 + i * 12 + 8
 */

/* Offset of the i-th child (0-indexed) in internal node */
static inline int internal_child_offset(int index) {
    if (index == 0) return BP_INTERNAL_ENTRIES_OFFSET;
    return BP_INTERNAL_ENTRIES_OFFSET + 4 + (index - 1) * BP_INTERNAL_ENTRY_SIZE + 8;
}

/* Offset of the i-th key (0-indexed) in internal node */
static inline int internal_key_offset(int index) {
    return BP_INTERNAL_ENTRIES_OFFSET + 4 + index * BP_INTERNAL_ENTRY_SIZE;
}

static inline int64_t internal_get_key(const char* page, int index) {
    int64_t val;
    memcpy(&val, page + internal_key_offset(index), sizeof(int64_t));
    return val;
}

static inline void internal_set_key(char* page, int index, int64_t key) {
    memcpy(page + internal_key_offset(index), &key, sizeof(int64_t));
}

static inline page_id_t internal_get_child(const char* page, int index) {
    int32_t val;
    memcpy(&val, page + internal_child_offset(index), sizeof(int32_t));
    return val;
}

static inline void internal_set_child(char* page, int index, page_id_t child) {
    memcpy(page + internal_child_offset(index), &child, sizeof(int32_t));
}

/* Compute max keys an internal node can hold */
static inline int32_t internal_max_keys(void) {
    /* Available space after header (up to PAGE_SIZE) for entries:
     * child_0(4) + n * (key(8) + child(4)) <= PAGE_SIZE - INTERNAL_ENTRIES_OFFSET
     * 4 + n*12 <= 4096 - 29 = 4067
     * n*12 <= 4063
     * n <= 338.58 => 338
     */
    int32_t available = PAGE_SIZE - BP_INTERNAL_ENTRIES_OFFSET;
    return (available - BP_INTERNAL_CHILD_SIZE) / BP_INTERNAL_ENTRY_SIZE;
}

/* Find position where key should be inserted / is located.
 * Returns the child index to follow for the given key. */
int internal_find_position(const char* page, int64_t key);

/* Insert a key/child pair at position pos. Shifts existing entries right.
 * Returns 0 on success, -1 if node is full. */
int internal_insert(char* page, int pos, int64_t key, page_id_t child);

/* ---- Leaf node operations ---- */

/* Offset of the i-th leaf entry key (0-indexed) */
static inline int leaf_key_offset(int index) {
    return BP_LEAF_ENTRIES_OFFSET + index * BP_LEAF_ENTRY_SIZE;
}

/* Offset of the i-th leaf entry rid (0-indexed) */
static inline int leaf_rid_offset(int index) {
    return BP_LEAF_ENTRIES_OFFSET + index * BP_LEAF_ENTRY_SIZE + BP_TREE_KEY_SIZE;
}

static inline int64_t leaf_get_key(const char* page, int index) {
    int64_t val;
    memcpy(&val, page + leaf_key_offset(index), sizeof(int64_t));
    return val;
}

static inline void leaf_set_key(char* page, int index, int64_t key) {
    memcpy(page + leaf_key_offset(index), &key, sizeof(int64_t));
}

static inline rid_t leaf_get_rid(const char* page, int index) {
    rid_t rid;
    memcpy(&rid, page + leaf_rid_offset(index), sizeof(rid_t));
    return rid;
}

static inline void leaf_set_rid(char* page, int index, rid_t rid) {
    memcpy(page + leaf_rid_offset(index), &rid, sizeof(rid_t));
}

static inline page_id_t leaf_get_next_page(const char* page) {
    int32_t val;
    memcpy(&val, page + BP_LEAF_NEXT_PAGE_OFFSET, sizeof(int32_t));
    return val;
}

static inline void leaf_set_next_page(char* page, page_id_t next) {
    memcpy(page + BP_LEAF_NEXT_PAGE_OFFSET, &next, sizeof(int32_t));
}

/* Compute max keys a leaf node can hold */
static inline int32_t leaf_max_keys(void) {
    /* Available space: PAGE_SIZE - BP_LEAF_ENTRIES_OFFSET
     * Each entry: 16B
     * n * 16 <= 4096 - 33 = 4063
     * n <= 253.9375 => 253
     */
    int32_t available = PAGE_SIZE - BP_LEAF_ENTRIES_OFFSET;
    return available / BP_LEAF_ENTRY_SIZE;
}

/* Find position where key should go in leaf (first index where key <= entry key).
 * Returns size if key is greater than all entries. */
int leaf_find_position(const char* page, int64_t key);

/* Insert a key/rid pair at position pos in leaf. Shifts entries right.
 * Returns 0 on success, -1 if leaf is full. */
int leaf_insert(char* page, int pos, int64_t key, rid_t rid);

/* Remove the entry at position pos. Shifts entries left.
 * Returns 0 on success. */
int leaf_remove(char* page, int pos);
