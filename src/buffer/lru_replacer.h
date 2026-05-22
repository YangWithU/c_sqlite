#pragma once

#include "src/common/list.h"
#include "src/common/hashmap.h"
#include "src/common/types.h"
#include <stddef.h>

/* LRU-2 replacement strategy.
 *
 * Two lists:
 *   cold_list — frames accessed exactly once (most recent at front)
 *   hot_list  — frames accessed >= 2 times (most recent at front)
 *
 * Victim picks from cold_list tail (least recently used once-accessed frame).
 * If cold_list is empty, picks from hot_list tail.
 *
 * Pin removes a frame from both lists.
 * Unpin adds a frame to the cold_list (first access) or hot_list (second+ access).
 */

/* Entry stored in the replacer's frame_map (frame_id -> frame_entry_t*) */
typedef struct {
    frame_id_t  frame_id;
    int         access_count;   /* how many times unpin'd */
    list_node_t node;           /* embedded list node for cold/hot list */
    int         in_hot;         /* 0 = in cold_list, 1 = in hot_list */
} lru_replacer_entry_t;

typedef struct {
    list_t    cold_list;
    list_t    hot_list;
    size_t    capacity;
    size_t    size;             /* total frames in replacer (cold + hot) */
    hashmap_t frame_map;        /* frame_id (as void*) -> lru_replacer_entry_t* */
} lru_replacer_t;

int    lru_replacer_init(lru_replacer_t* replacer, size_t capacity);
void   lru_replacer_destroy(lru_replacer_t* replacer);
int    lru_replacer_victim(lru_replacer_t* replacer, frame_id_t* frame_id);
void   lru_replacer_pin(lru_replacer_t* replacer, frame_id_t frame_id);
void   lru_replacer_unpin(lru_replacer_t* replacer, frame_id_t frame_id);
size_t lru_replacer_size(lru_replacer_t* replacer);
