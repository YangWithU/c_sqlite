#include "src/index/b_plus_tree_node.h"
#include <string.h>

/* ---- Internal node ---- */

int internal_find_position(const char* page, int64_t key) {
    int32_t size = bp_node_get_size(page);
    int lo = 0, hi = size;
    /* Binary search for the first key > key.
     * In a B+ tree internal node, key_i separates child_i and child_{i+1}:
     *   keys < key_i go to child_i, keys >= key_i go to child_{i+1}.
     * We want to return the child index to follow for the given key.
     * This is: find first key_i such that key < key_i, then follow child_i.
     * Or equivalently: count how many keys are <= key; that's the child index. */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (internal_get_key(page, mid) <= key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    /* lo is the child index to follow */
    return lo;
}

int internal_insert(char* page, int pos, int64_t key, page_id_t child) {
    int32_t size = bp_node_get_size(page);
    if (size >= internal_max_keys()) {
        return -1;  /* node is full */
    }

    /* Shift entries from pos..size-1 one position to the right.
     * Internal layout: [child_0][key_0 child_1][key_1 child_2]...[key_{n-1} child_n]
     * Inserting at position pos means inserting key and child_{pos+1},
     * and the old child at pos becomes the "left" child of the new key.
     * Actually, we're inserting (key, child) at position pos:
     *   The new child goes at index pos+1, the new key goes at index pos.
     *   We need to shift keys [pos..size-1] and children [pos+1..size] right by one.
     */

    /* Shift key/child entries right starting from position pos.
     * We need to move bytes from the last child's end down to where we're inserting. */
    /* Source: start of key at position pos
     * Dest:   start of key at position pos + 1
     * Bytes to move: from key_pos to end of child_{size} */

    int src_off = internal_key_offset(pos);
    int dst_off = internal_key_offset(pos + 1);
    int end_off = internal_child_offset(size) + (int)sizeof(int32_t); /* past last child */
    int bytes_to_move = end_off - src_off;

    if (bytes_to_move > 0) {
        memmove(page + dst_off, page + src_off, bytes_to_move);
    }

    /* Write the new key and child */
    internal_set_key(page, pos, key);
    internal_set_child(page, pos + 1, child);

    bp_node_set_size(page, size + 1);
    return 0;
}

/* ---- Leaf node ---- */

int leaf_find_position(const char* page, int64_t key) {
    int32_t size = bp_node_get_size(page);
    int lo = 0, hi = size;
    /* Find first index where entry key >= key */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (leaf_get_key(page, mid) < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

int leaf_insert(char* page, int pos, int64_t key, rid_t rid) {
    int32_t size = bp_node_get_size(page);
    if (size >= leaf_max_keys()) {
        return -1;  /* leaf is full */
    }

    /* Shift entries [pos..size-1] right by one entry */
    if (pos < size) {
        int src_off = leaf_key_offset(pos);
        int dst_off = leaf_key_offset(pos + 1);
        int bytes_to_move = (size - pos) * BP_LEAF_ENTRY_SIZE;
        memmove(page + dst_off, page + src_off, bytes_to_move);
    }

    /* Write new entry */
    leaf_set_key(page, pos, key);
    leaf_set_rid(page, pos, rid);

    bp_node_set_size(page, size + 1);
    return 0;
}

int leaf_remove(char* page, int pos) {
    int32_t size = bp_node_get_size(page);
    if (pos < 0 || pos >= size) {
        return -1;
    }

    /* Shift entries [pos+1..size-1] left by one entry */
    if (pos < size - 1) {
        int src_off = leaf_key_offset(pos + 1);
        int dst_off = leaf_key_offset(pos);
        int bytes_to_move = (size - pos - 1) * BP_LEAF_ENTRY_SIZE;
        memmove(page + dst_off, page + src_off, bytes_to_move);
    }

    bp_node_set_size(page, size - 1);
    return 0;
}
