#pragma once

#include "src/common/types.h"
#include "src/buffer/buffer_pool_manager.h"
#include <stdint.h>

typedef struct {
    buffer_pool_manager_t*  bpm;
    page_id_t               current_page_id;
    int                     current_index;
    int64_t                 high_key;
    int                     exhausted;
} index_iterator_t;

/* Initialize an iterator for a range scan from start_key to high_key (inclusive).
 * The iterator walks the leaf chain starting at the leaf containing start_key. */
int  index_iterator_init(index_iterator_t* it, buffer_pool_manager_t* bpm,
                         page_id_t root_page_id, int64_t start_key, int64_t high_key);

/* Advance the iterator. Returns 1 if a valid entry is available (written to
 * *out_key and *out_rid), 0 if exhausted. */
int  index_iterator_next(index_iterator_t* it, int64_t* out_key, rid_t* out_rid);

/* Destroy the iterator (releases any held references). */
void index_iterator_destroy(index_iterator_t* it);
