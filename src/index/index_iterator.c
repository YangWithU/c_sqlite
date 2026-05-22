#include "src/index/index_iterator.h"
#include "src/index/b_plus_tree_node.h"
#include <string.h>

/* ---- Internal helper: find the leaf page containing key ---- */

static page_id_t find_leaf_page(buffer_pool_manager_t* bpm,
                                 page_id_t root_page_id,
                                 int64_t key) {
    page_id_t current = root_page_id;

    while (1) {
        page_t* page = bpm_fetch_page(bpm, current);
        if (!page) return INVALID_PAGE_ID;

        char* data = page->data;
        uint8_t type = bp_node_get_type(data);

        if (type == BP_NODE_TYPE_LEAF) {
            /* Don't unpin — the caller will use this leaf */
            return current;
        }

        /* Internal node: find which child to descend into */
        int child_idx = internal_find_position(data, key);
        page_id_t child = internal_get_child(data, child_idx);

        bpm_unpin_page(bpm, current, 0);
        current = child;
    }
}

int index_iterator_init(index_iterator_t* it, buffer_pool_manager_t* bpm,
                         page_id_t root_page_id, int64_t start_key, int64_t high_key) {
    it->bpm = bpm;
    it->high_key = high_key;
    it->exhausted = 0;
    it->current_index = 0;

    /* Find the leaf containing start_key */
    it->current_page_id = find_leaf_page(bpm, root_page_id, start_key);

    if (it->current_page_id == INVALID_PAGE_ID) {
        it->exhausted = 1;
        return DB_OK;
    }

    /* Find the starting position within the leaf */
    page_t* page = bpm_fetch_page(bpm, it->current_page_id);
    if (!page) {
        it->exhausted = 1;
        return DB_OK;
    }

    char* data = page->data;
    it->current_index = leaf_find_position(data, start_key);

    /* If the start position is past the end of this leaf, we need to
     * move to the next leaf. */
    int32_t size = bp_node_get_size(data);
    if (it->current_index >= size) {
        page_id_t next = leaf_get_next_page(data);
        bpm_unpin_page(bpm, it->current_page_id, 0);

        if (next == INVALID_PAGE_ID) {
            it->exhausted = 1;
            it->current_page_id = INVALID_PAGE_ID;
            return DB_OK;
        }

        it->current_page_id = next;
        it->current_index = 0;
    } else {
        bpm_unpin_page(bpm, it->current_page_id, 0);
    }

    return DB_OK;
}

int index_iterator_next(index_iterator_t* it, int64_t* out_key, rid_t* out_rid) {
    if (it->exhausted) {
        return 0;
    }

    /* Fetch the current leaf page */
    page_t* page = bpm_fetch_page(it->bpm, it->current_page_id);
    if (!page) {
        it->exhausted = 1;
        return 0;
    }

    char* data = page->data;
    int32_t size = bp_node_get_size(data);

    /* Check if we've exhausted this leaf */
    if (it->current_index >= size) {
        /* Move to next leaf */
        page_id_t next = leaf_get_next_page(data);
        bpm_unpin_page(it->bpm, it->current_page_id, 0);

        if (next == INVALID_PAGE_ID) {
            it->exhausted = 1;
            it->current_page_id = INVALID_PAGE_ID;
            return 0;
        }

        it->current_page_id = next;
        it->current_index = 0;

        /* Fetch the next leaf */
        page = bpm_fetch_page(it->bpm, it->current_page_id);
        if (!page) {
            it->exhausted = 1;
            return 0;
        }
        data = page->data;
        size = bp_node_get_size(data);
    }

    /* Check again after potentially advancing to next leaf */
    if (it->current_index >= size) {
        bpm_unpin_page(it->bpm, it->current_page_id, 0);
        it->exhausted = 1;
        return 0;
    }

    /* Read the current entry */
    int64_t key = leaf_get_key(data, it->current_index);
    rid_t rid = leaf_get_rid(data, it->current_index);

    /* Check if we've passed the high key */
    if (key > it->high_key) {
        bpm_unpin_page(it->bpm, it->current_page_id, 0);
        it->exhausted = 1;
        return 0;
    }

    /* Return the current entry */
    if (out_key) *out_key = key;
    if (out_rid) *out_rid = rid;

    it->current_index++;
    bpm_unpin_page(it->bpm, it->current_page_id, 0);

    return 1;
}

void index_iterator_destroy(index_iterator_t* it) {
    /* All pages are unpinned during iteration, so nothing to release */
    it->exhausted = 1;
    it->current_page_id = INVALID_PAGE_ID;
    it->bpm = NULL;
}
