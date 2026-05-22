#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/vector.h"
#include "src/buffer/buffer_pool_manager.h"
#include <stdint.h>

typedef struct {
    index_id_t              index_id;
    page_id_t               root_page_id;
    buffer_pool_manager_t*  bpm;
    int                     is_unique;
} b_plus_tree_t;

/* Initialize a B+ tree.
 * root_page_id may be INVALID_PAGE_ID to create a new empty tree. */
int  bpt_init(b_plus_tree_t* tree, index_id_t id, page_id_t root_page_id,
              buffer_pool_manager_t* bpm, int unique);

/* Destroy the tree structure (does not free pages). */
void bpt_destroy(b_plus_tree_t* tree);

/* Find a key in the tree. On success, writes the rid to *out_rid and returns DB_OK.
 * Returns DB_PAGE_NOT_FOUND if key is not present. */
int  bpt_find(b_plus_tree_t* tree, int64_t key, rid_t* out_rid);

/* Insert a key/rid pair. Returns DB_OK on success.
 * If is_unique and key already exists, returns DB_DUPLICATE_KEY. */
int  bpt_insert(b_plus_tree_t* tree, int64_t key, rid_t rid);

/* Remove a key from the tree. Returns DB_OK on success.
 * Returns DB_PAGE_NOT_FOUND if key is not found. */
int  bpt_remove(b_plus_tree_t* tree, int64_t key);

/* Find all keys in [low, high] (inclusive). Results are pushed into the vector
 * as rid_t elements. Returns DB_OK. */
int  bpt_find_range(b_plus_tree_t* tree, int64_t low, int64_t high, vector_t* results);
