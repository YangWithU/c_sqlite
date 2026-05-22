#include "src/index/b_plus_tree.h"
#include "src/index/b_plus_tree_node.h"
#include "src/common/mem.h"
#include "src/common/error.h"
#include "src/common/macros.h"
#include <string.h>
#include <stdlib.h>

/* ---- Internal path tracking for split propagation ---- */

typedef struct {
    page_id_t page_id;
    int       child_index;  /* which child we descended into from this page */
} path_entry_t;

/* ---- Helper: fetch page and validate ---- */

static char* fetch_page_data(b_plus_tree_t* tree, page_id_t pid) {
    page_t* page = bpm_fetch_page(tree->bpm, pid);
    if (!page) return NULL;
    return page->data;
}

static void unpin(b_plus_tree_t* tree, page_id_t pid, int dirty) {
    bpm_unpin_page(tree->bpm, pid, dirty);
}

/* ---- Helper: initialize a new leaf page ---- */

static void init_leaf_page(char* data) {
    memset(data, 0, PAGE_SIZE);
    bp_node_set_type(data, BP_NODE_TYPE_LEAF);
    bp_node_set_size(data, 0);
    leaf_set_next_page(data, INVALID_PAGE_ID);
}

/* ---- Helper: initialize a new internal page ---- */

static void init_internal_page(char* data) {
    memset(data, 0, PAGE_SIZE);
    bp_node_set_type(data, BP_NODE_TYPE_INTERNAL);
    bp_node_set_size(data, 0);
}

/* ---- Helper: find the leaf page containing key ----
 * Returns the leaf page_id. Fills path[] with the traversal path.
 * path_len is set to the number of entries in the path.
 * Caller must unpin all pages in the path (except the returned leaf). */

static page_id_t find_leaf(b_plus_tree_t* tree, int64_t key,
                            path_entry_t* path, int* path_len) {
    page_id_t current = tree->root_page_id;
    *path_len = 0;

    while (1) {
        char* data = fetch_page_data(tree, current);
        if (!data) return INVALID_PAGE_ID;

        uint8_t type = bp_node_get_type(data);
        if (type == BP_NODE_TYPE_LEAF) {
            return current;
        }

        /* Internal node: find which child to descend into */
        int child_idx = internal_find_position(data, key);
        page_id_t child = internal_get_child(data, child_idx);

        /* Record path entry */
        path[*path_len].page_id = current;
        path[*path_len].child_index = child_idx;
        (*path_len)++;

        /* Unpin this internal page */
        unpin(tree, current, 0);

        current = child;
    }
}

/* ---- Public API ---- */

int bpt_init(b_plus_tree_t* tree, index_id_t id, page_id_t root_page_id,
             buffer_pool_manager_t* bpm, int unique) {
    tree->index_id = id;
    tree->bpm = bpm;
    tree->is_unique = unique;

    if (root_page_id == INVALID_PAGE_ID) {
        /* Create a new root leaf page */
        page_id_t new_pid;
        page_t* page = bpm_new_page(bpm, &new_pid);
        if (!page) return DB_OUT_OF_MEMORY;

        init_leaf_page(page->data);
        tree->root_page_id = new_pid;

        bpm_unpin_page(bpm, new_pid, 1);
    } else {
        tree->root_page_id = root_page_id;
    }

    return DB_OK;
}

void bpt_destroy(b_plus_tree_t* tree) {
    /* Nothing to free — pages are managed by the buffer pool */
    (void)tree;
}

int bpt_find(b_plus_tree_t* tree, int64_t key, rid_t* out_rid) {
    path_entry_t path[64];
    int path_len = 0;
    page_id_t leaf_pid = find_leaf(tree, key, path, &path_len);

    if (leaf_pid == INVALID_PAGE_ID) {
        return DB_PAGE_NOT_FOUND;
    }

    char* data = fetch_page_data(tree, leaf_pid);
    if (!data) return DB_PAGE_NOT_FOUND;

    int pos = leaf_find_position(data, key);
    int32_t size = bp_node_get_size(data);

    int found = 0;
    if (pos < size && leaf_get_key(data, pos) == key) {
        if (out_rid) {
            *out_rid = leaf_get_rid(data, pos);
        }
        found = 1;
    }

    unpin(tree, leaf_pid, 0);
    return found ? DB_OK : DB_PAGE_NOT_FOUND;
}

/* ---- Split helpers ---- */

/* Split a leaf page. Returns the promoted key and the new sibling page_id.
 * The caller is responsible for inserting the promoted key into the parent. */
static void split_leaf(b_plus_tree_t* tree, page_id_t leaf_pid,
                       int64_t* promoted_key, page_id_t* sibling_pid) {
    char* leaf_data = fetch_page_data(tree, leaf_pid);

    int32_t size = bp_node_get_size(leaf_data);
    int mid = size / 2;  /* split point: first key of right sibling */

    /* Create new sibling leaf page */
    page_id_t new_pid;
    page_t* new_page = bpm_new_page(tree->bpm, &new_pid);
    init_leaf_page(new_page->data);

    char* new_data = new_page->data;

    /* Copy entries [mid..size-1] to new leaf */
    int32_t right_count = size - mid;
    for (int32_t i = 0; i < right_count; i++) {
        leaf_set_key(new_data, i, leaf_get_key(leaf_data, mid + i));
        leaf_set_rid(new_data, i, leaf_get_rid(leaf_data, mid + i));
    }
    bp_node_set_size(new_data, right_count);

    /* Set promoted key = first key of right sibling */
    *promoted_key = leaf_get_key(new_data, 0);

    /* Update left leaf: shrink size to mid */
    bp_node_set_size(leaf_data, mid);

    /* Link leaves: new leaf's next = old leaf's next, old leaf's next = new */
    leaf_set_next_page(new_data, leaf_get_next_page(leaf_data));
    leaf_set_next_page(leaf_data, new_pid);

    unpin(tree, leaf_pid, 1);
    unpin(tree, new_pid, 1);

    *sibling_pid = new_pid;
}

/* Split an internal page. Returns the promoted key and new sibling page_id. */
static UNUSED_FN void split_internal(b_plus_tree_t* tree, page_id_t internal_pid,
                           int64_t* promoted_key, page_id_t* sibling_pid) {
    char* int_data = fetch_page_data(tree, internal_pid);

    int32_t size = bp_node_get_size(int_data);
    int mid = size / 2;

    /* Create new internal page */
    page_id_t new_pid;
    page_t* new_page = bpm_new_page(tree->bpm, &new_pid);
    init_internal_page(new_page->data);

    char* new_data = new_page->data;

    /* The promoted key is the middle key — it moves up, doesn't stay in either node */
    *promoted_key = internal_get_key(int_data, mid);

    /* Copy entries [mid+1..size-1] to new internal node.
     * The first child of the new node is child_{mid+1} from the old node. */
    int32_t right_count = size - mid - 1;  /* number of keys in right sibling */
    internal_set_child(new_data, 0, internal_get_child(int_data, mid + 1));

    for (int32_t i = 0; i < right_count; i++) {
        internal_set_key(new_data, i, internal_get_key(int_data, mid + 1 + i));
        internal_set_child(new_data, i + 1, internal_get_child(int_data, mid + 1 + i + 1));
    }
    bp_node_set_size(new_data, right_count);

    /* Shrink left internal: size = mid */
    bp_node_set_size(int_data, mid);

    unpin(tree, internal_pid, 1);
    unpin(tree, new_pid, 1);

    *sibling_pid = new_pid;
}

/* Insert a key/child into a parent internal node at the given position.
 * If the parent splits, recursively propagate up. */
static int insert_into_parent(b_plus_tree_t* tree,
                              path_entry_t* path, int path_len,
                              page_id_t left_pid, int64_t key, page_id_t right_pid) {
    if (path_len == 0) {
        /* We need to create a new root */
        page_id_t new_root_pid;
        page_t* new_root = bpm_new_page(tree->bpm, &new_root_pid);
        if (!new_root) return DB_OUT_OF_MEMORY;

        init_internal_page(new_root->data);
        char* data = new_root->data;

        internal_set_child(data, 0, left_pid);
        internal_set_key(data, 0, key);
        internal_set_child(data, 1, right_pid);
        bp_node_set_size(data, 1);

        tree->root_page_id = new_root_pid;
        unpin(tree, new_root_pid, 1);
        return DB_OK;
    }

    /* Get the parent from the path */
    int parent_idx = path_len - 1;
    page_id_t parent_pid = path[parent_idx].page_id;
    int insert_pos = path[parent_idx].child_index;

    char* parent_data = fetch_page_data(tree, parent_pid);
    int32_t size = bp_node_get_size(parent_data);

    if (size < internal_max_keys()) {
        /* Parent has room — insert directly */
        /* insert_pos is the child index we descended from. The new key should
         * go at position insert_pos (pointing to right_pid as child insert_pos+1). */
        internal_insert(parent_data, insert_pos, key, right_pid);
        unpin(tree, parent_pid, 1);
        return DB_OK;
    }

    /* Parent is full — need to split.
     * First, insert temporarily (the node is over-capacity by 1), then split. */

    /* We need to handle this carefully. The internal_insert function won't allow
     * over-capacity, so we do a manual insert into a temporary buffer or
     * we directly insert and split.
     *
     * Strategy: insert into the parent (which is at max), then split.
     * We'll use a slightly different approach: do the split first conceptually.
     * We know the insert position. We can figure out where the new key/child
     * goes after the split.
     *
     * Simpler approach: temporarily allow the insert (override the size check),
     * then split. We'll do it manually.
     */

    /* Shift entries right to make room at insert_pos */
    int src_off = internal_key_offset(insert_pos);
    int dst_off = internal_key_offset(insert_pos + 1);
    int end_off = internal_child_offset(size) + (int)sizeof(int32_t);
    int bytes_to_move = end_off - src_off;
    if (bytes_to_move > 0) {
        memmove(parent_data + dst_off, parent_data + src_off, bytes_to_move);
    }
    internal_set_key(parent_data, insert_pos, key);
    internal_set_child(parent_data, insert_pos + 1, right_pid);
    /* Temporarily set size to size+1 (over-capacity) */
    bp_node_set_size(parent_data, size + 1);

    /* Now split the over-capacity internal node */
    int32_t new_size = size + 1;  /* total keys now */
    int mid = new_size / 2;
    int64_t promoted_key = internal_get_key(parent_data, mid);

    /* Create new internal sibling */
    page_id_t new_pid;
    page_t* new_page = bpm_new_page(tree->bpm, &new_pid);
    init_internal_page(new_page->data);
    char* new_data = new_page->data;

    int32_t right_count = new_size - mid - 1;
    internal_set_child(new_data, 0, internal_get_child(parent_data, mid + 1));
    for (int32_t i = 0; i < right_count; i++) {
        internal_set_key(new_data, i, internal_get_key(parent_data, mid + 1 + i));
        internal_set_child(new_data, i + 1, internal_get_child(parent_data, mid + 1 + i + 1));
    }
    bp_node_set_size(new_data, right_count);

    /* Shrink left node to mid keys */
    bp_node_set_size(parent_data, mid);

    unpin(tree, parent_pid, 1);
    unpin(tree, new_pid, 1);

    /* Recursively insert promoted key into grandparent */
    return insert_into_parent(tree, path, parent_idx,
                              parent_pid, promoted_key, new_pid);
}

int bpt_insert(b_plus_tree_t* tree, int64_t key, rid_t rid) {
    path_entry_t path[64];
    int path_len = 0;
    page_id_t leaf_pid = find_leaf(tree, key, path, &path_len);

    if (leaf_pid == INVALID_PAGE_ID) {
        return DB_INTERNAL_ERROR;
    }

    char* leaf_data = fetch_page_data(tree, leaf_pid);
    if (!leaf_data) {
        return DB_INTERNAL_ERROR;
    }

    int32_t size = bp_node_get_size(leaf_data);
    int pos = leaf_find_position(leaf_data, key);

    /* Check for duplicate key */
    if (pos < size && leaf_get_key(leaf_data, pos) == key) {
        unpin(tree, leaf_pid, 0);
        if (tree->is_unique) {
            return DB_DUPLICATE_KEY;
        }
        /* For non-unique, update the rid (simplification: allow duplicates at leaf level) */
        leaf_set_rid(leaf_data, pos, rid);
        unpin(tree, leaf_pid, 1);
        return DB_OK;
    }

    /* Check if leaf has room */
    if (size < leaf_max_keys()) {
        /* Simple case: leaf is not full */
        leaf_insert(leaf_data, pos, key, rid);
        unpin(tree, leaf_pid, 1);
        return DB_OK;
    }

    /* Leaf is full — need to split.
     * First, insert into the leaf (over-capacity temporarily), then split. */
    /* We need to allow over-capacity temporarily. Do a manual insert. */
    if (pos < size) {
        int src_off = leaf_key_offset(pos);
        int dst_off = leaf_key_offset(pos + 1);
        int bytes = (size - pos) * BP_LEAF_ENTRY_SIZE;
        memmove(leaf_data + dst_off, leaf_data + src_off, bytes);
    }
    leaf_set_key(leaf_data, pos, key);
    leaf_set_rid(leaf_data, pos, rid);
    bp_node_set_size(leaf_data, size + 1);

    /* Unpin before split_leaf (which will re-fetch) */
    unpin(tree, leaf_pid, 1);

    /* Split the leaf */
    int64_t promoted_key;
    page_id_t sibling_pid;
    split_leaf(tree, leaf_pid, &promoted_key, &sibling_pid);

    /* Insert promoted key into parent */
    return insert_into_parent(tree, path, path_len, leaf_pid, promoted_key, sibling_pid);
}

int bpt_remove(b_plus_tree_t* tree, int64_t key) {
    path_entry_t path[64];
    int path_len = 0;
    page_id_t leaf_pid = find_leaf(tree, key, path, &path_len);

    if (leaf_pid == INVALID_PAGE_ID) {
        return DB_PAGE_NOT_FOUND;
    }

    char* leaf_data = fetch_page_data(tree, leaf_pid);
    if (!leaf_data) return DB_PAGE_NOT_FOUND;

    int pos = leaf_find_position(leaf_data, key);
    int32_t size = bp_node_get_size(leaf_data);

    if (pos >= size || leaf_get_key(leaf_data, pos) != key) {
        unpin(tree, leaf_pid, 0);
        return DB_PAGE_NOT_FOUND;
    }

    leaf_remove(leaf_data, pos);
    unpin(tree, leaf_pid, 1);

    /* Simplified: don't merge or redistribute. Just leave underflow. */
    return DB_OK;
}

int bpt_find_range(b_plus_tree_t* tree, int64_t low, int64_t high, vector_t* results) {
    path_entry_t path[64];
    int path_len = 0;
    page_id_t leaf_pid = find_leaf(tree, low, path, &path_len);

    if (leaf_pid == INVALID_PAGE_ID) {
        return DB_OK;
    }

    page_id_t current_pid = leaf_pid;

    while (current_pid != INVALID_PAGE_ID) {
        char* data = fetch_page_data(tree, current_pid);
        if (!data) break;

        int32_t size = bp_node_get_size(data);
        page_id_t next_pid = leaf_get_next_page(data);

        int start_pos = 0;
        /* On the first leaf, find the start position */
        if (current_pid == leaf_pid) {
            start_pos = leaf_find_position(data, low);
        }

        for (int i = start_pos; i < size; i++) {
            int64_t k = leaf_get_key(data, i);
            if (k > high) {
                /* Past the range — we're done */
                unpin(tree, current_pid, 0);
                return DB_OK;
            }
            rid_t rid = leaf_get_rid(data, i);
            vector_push(results, &rid);
        }

        unpin(tree, current_pid, 0);
        current_pid = next_pid;
    }

    return DB_OK;
}
