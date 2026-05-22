#pragma once

#include "src/buffer/page_latch.h"
#include "src/buffer/lru_replacer.h"
#include "src/common/hashmap.h"
#include "src/common/list.h"
#include "src/common/types.h"
#include "src/common/config.h"
#include "src/storage/disk_manager.h"
#include <stddef.h>

/* Buffer pool page — the in-memory representation of a disk page. */
typedef struct {
    char          data[PAGE_SIZE];
    page_id_t     page_id;
    int           pin_count;
    int           is_dirty;
    page_latch_t  latch;
} page_t;

/* Forward declaration */
typedef struct buffer_pool_manager buffer_pool_manager_t;

struct buffer_pool_manager {
    size_t            pool_size;
    page_t*           pages;          /* array of pool_size pages */
    hashmap_t         page_table;     /* page_id (as void*) -> frame_id */
    list_t            free_list;      /* free frames (list_node_t entries) */
    list_node_t*      free_nodes;     /* backing array for free_list nodes */
    lru_replacer_t*   replacer;
    disk_manager_t*   disk_manager;
};

int      bpm_init(buffer_pool_manager_t* bpm, size_t pool_size, disk_manager_t* dm);
void     bpm_destroy(buffer_pool_manager_t* bpm);
page_t*  bpm_fetch_page(buffer_pool_manager_t* bpm, page_id_t page_id);
page_t*  bpm_new_page(buffer_pool_manager_t* bpm, page_id_t* page_id);
int      bpm_unpin_page(buffer_pool_manager_t* bpm, page_id_t page_id, int is_dirty);
int      bpm_flush_page(buffer_pool_manager_t* bpm, page_id_t page_id);
void     bpm_flush_all(buffer_pool_manager_t* bpm);
