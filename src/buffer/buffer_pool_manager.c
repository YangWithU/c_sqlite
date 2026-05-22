#include "src/buffer/buffer_pool_manager.h"
#include "src/common/mem.h"
#include "src/common/macros.h"
#include "src/common/logger.h"
#include <stdint.h>
#include <string.h>

/* ---- Integer key helpers for hashmap (page_id stored as void*) ---- */

static size_t int_hash(const void* key) {
    uintptr_t v = (uintptr_t)key;
    v = ((v >> 16) ^ v) * 0x45d9f3b;
    v = ((v >> 16) ^ v) * 0x45d9f3b;
    v = (v >> 16) ^ v;
    return (size_t)v;
}

static int int_equal(const void* a, const void* b) {
    return (uintptr_t)a == (uintptr_t)b;
}

static void* page_id_to_key(page_id_t pid) {
    return (void*)(uintptr_t)(uint32_t)pid;
}

__attribute__((unused))
static page_id_t key_to_page_id(const void* key) {
    return (page_id_t)(uint32_t)(uintptr_t)key;
}

/* Frame ID helpers for page_table hashmap values.
 * Offset by +1 when storing so that frame_id=0 maps to value=1 (not NULL),
 * since hashmap_get returns NULL for "not found". */
static void* frame_id_to_val(frame_id_t fid) {
    return (void*)(uintptr_t)((uint32_t)fid + 1);
}

static frame_id_t val_to_frame_id(void* val) {
    return (frame_id_t)((uint32_t)(uintptr_t)val - 1);
}

/* ---- Internal helpers ---- */

/* Find a free frame and return its frame_id, or -1 if none available. */
static frame_id_t find_free_frame(buffer_pool_manager_t* bpm) {
    if (list_empty(&bpm->free_list)) {
        return (frame_id_t)-1;
    }
    list_node_t* node = list_pop_front(&bpm->free_list);
    /* The free_nodes array is indexed by frame_id.
     * Find which frame_id this node corresponds to.
     * Pointer arithmetic (node - bpm->free_nodes) already gives the
     * element index, which equals the frame_id. */
    frame_id_t frame_id = (frame_id_t)(node - bpm->free_nodes);
    return frame_id;
}

/* Evict a page using the LRU replacer. Returns frame_id or -1 on failure. */
static frame_id_t evict_page(buffer_pool_manager_t* bpm) {
    frame_id_t frame_id;
    if (lru_replacer_victim(bpm->replacer, &frame_id) != 0) {
        return (frame_id_t)-1;
    }

    page_t* page = &bpm->pages[frame_id];

    /* If the page is dirty, flush it to disk first */
    if (page->is_dirty) {
        disk_manager_write_page(bpm->disk_manager, page->page_id, page->data);
        page->is_dirty = 0;
    }

    /* Remove from page table */
    hashmap_remove(&bpm->page_table, page_id_to_key(page->page_id), NULL, NULL);

    /* Reset the page */
    page->page_id = INVALID_PAGE_ID;
    page->pin_count = 0;
    page->is_dirty = 0;

    /* Return the frame to the free list */
    bpm->free_nodes[frame_id].prev = NULL;
    bpm->free_nodes[frame_id].next = NULL;
    list_push_back(&bpm->free_list, &bpm->free_nodes[frame_id]);

    return frame_id;
}

/* ---- Public API ---- */

int bpm_init(buffer_pool_manager_t* bpm, size_t pool_size, disk_manager_t* dm) {
    bpm->pool_size = pool_size;
    bpm->disk_manager = dm;

    /* Allocate page array using db_calloc (page_alloc uses PAGE_SIZE which is
     * too small for page_t since it contains extra fields beyond data[PAGE_SIZE]) */
    bpm->pages = (page_t*)db_calloc(pool_size, sizeof(page_t));
    if (!bpm->pages) {
        return -1;
    }

    /* Initialize each page */
    for (size_t i = 0; i < pool_size; i++) {
        bpm->pages[i].page_id = INVALID_PAGE_ID;
        bpm->pages[i].pin_count = 0;
        bpm->pages[i].is_dirty = 0;
        page_latch_init(&bpm->pages[i].latch);
    }

    /* Initialize page table (page_id -> frame_id) */
    hashmap_init(&bpm->page_table, 64, int_hash, int_equal);

    /* Initialize free list — all frames start free.
     * Each frame gets a list_node_t in the free_nodes array. */
    bpm->free_nodes = db_malloc(pool_size * sizeof(list_node_t));
    if (!bpm->free_nodes) {
        db_free(bpm->pages);
        hashmap_destroy(&bpm->page_table, NULL, NULL);
        return -1;
    }
    list_init(&bpm->free_list);
    for (size_t i = 0; i < pool_size; i++) {
        bpm->free_nodes[i].prev = NULL;
        bpm->free_nodes[i].next = NULL;
        list_push_back(&bpm->free_list, &bpm->free_nodes[i]);
    }

    /* Allocate and initialize the LRU replacer */
    bpm->replacer = db_malloc(sizeof(lru_replacer_t));
    if (!bpm->replacer) {
        db_free(bpm->free_nodes);
        db_free(bpm->pages);
        hashmap_destroy(&bpm->page_table, NULL, NULL);
        return -1;
    }
    lru_replacer_init(bpm->replacer, pool_size);

    return 0;
}

void bpm_destroy(buffer_pool_manager_t* bpm) {
    /* Flush all dirty pages */
    bpm_flush_all(bpm);

    /* Destroy page latches */
    for (size_t i = 0; i < bpm->pool_size; i++) {
        page_latch_destroy(&bpm->pages[i].latch);
    }

    /* Free resources */
    db_free(bpm->pages);
    bpm->pages = NULL;
    db_free(bpm->free_nodes);
    bpm->free_nodes = NULL;
    hashmap_destroy(&bpm->page_table, NULL, NULL);
    lru_replacer_destroy(bpm->replacer);
    db_free(bpm->replacer);
    bpm->replacer = NULL;
}

page_t* bpm_fetch_page(buffer_pool_manager_t* bpm, page_id_t page_id) {
    /* 1. Look up in page table */
    void* val = hashmap_get(&bpm->page_table, page_id_to_key(page_id));
    if (val) {
        frame_id_t frame_id = val_to_frame_id(val);
        page_t* page = &bpm->pages[frame_id];
        page->pin_count++;
        /* Pin in replacer (it's being used) */
        lru_replacer_pin(bpm->replacer, frame_id);
        return page;
    }

    /* 2. Page not in buffer — find a free frame or evict */
    frame_id_t frame_id = find_free_frame(bpm);
    if (frame_id == (frame_id_t)-1) {
        frame_id = evict_page(bpm);
        if (frame_id == (frame_id_t)-1) {
            return NULL;  /* no space available */
        }
    }

    /* 3. Read the page from disk */
    page_t* page = &bpm->pages[frame_id];
    int rc = disk_manager_read_page(bpm->disk_manager, page_id, page->data);
    if (rc != DB_OK) {
        /* Put frame back on free list */
        bpm->free_nodes[frame_id].prev = NULL;
        bpm->free_nodes[frame_id].next = NULL;
        list_push_back(&bpm->free_list, &bpm->free_nodes[frame_id]);
        return NULL;
    }

    /* 4. Set up the page */
    page->page_id = page_id;
    page->pin_count = 1;
    page->is_dirty = 0;
    page_latch_init(&page->latch);

    /* 5. Add to page table */
    hashmap_put(&bpm->page_table, page_id_to_key(page_id),
                frame_id_to_val(frame_id));

    return page;
}

page_t* bpm_new_page(buffer_pool_manager_t* bpm, page_id_t* page_id) {
    /* 1. Allocate a new page on disk */
    int new_pid = disk_manager_allocate_page(bpm->disk_manager);
    if (new_pid < 0) {
        return NULL;
    }

    /* 2. Find a free frame or evict */
    frame_id_t frame_id = find_free_frame(bpm);
    if (frame_id == (frame_id_t)-1) {
        frame_id = evict_page(bpm);
        if (frame_id == (frame_id_t)-1) {
            /* Could not get a frame — undo disk allocation */
            disk_manager_deallocate_page(bpm->disk_manager, new_pid);
            return NULL;
        }
    }

    /* 3. Set up the page */
    page_t* page = &bpm->pages[frame_id];
    memset(page->data, 0, PAGE_SIZE);
    page->page_id = new_pid;
    page->pin_count = 1;
    page->is_dirty = 0;
    page_latch_init(&page->latch);

    /* 4. Write the blank page to disk */
    disk_manager_write_page(bpm->disk_manager, new_pid, page->data);

    /* 5. Add to page table */
    hashmap_put(&bpm->page_table, page_id_to_key(new_pid),
                frame_id_to_val(frame_id));

    *page_id = new_pid;
    return page;
}

int bpm_unpin_page(buffer_pool_manager_t* bpm, page_id_t page_id, int is_dirty) {
    void* val = hashmap_get(&bpm->page_table, page_id_to_key(page_id));
    if (!val) {
        return -1;  /* page not in buffer */
    }

    frame_id_t frame_id = val_to_frame_id(val);
    page_t* page = &bpm->pages[frame_id];

    if (page->pin_count <= 0) {
        return -1;  /* already unpinned */
    }

    page->pin_count--;
    if (is_dirty) {
        page->is_dirty = 1;
    }

    /* If pin_count reaches 0, add to replacer so it can be evicted */
    if (page->pin_count == 0) {
        lru_replacer_unpin(bpm->replacer, frame_id);
    }

    return 0;
}

int bpm_flush_page(buffer_pool_manager_t* bpm, page_id_t page_id) {
    void* val = hashmap_get(&bpm->page_table, page_id_to_key(page_id));
    if (!val) {
        return -1;  /* page not in buffer */
    }

    frame_id_t frame_id = val_to_frame_id(val);
    page_t* page = &bpm->pages[frame_id];

    int rc = disk_manager_write_page(bpm->disk_manager, page_id, page->data);
    if (rc == DB_OK) {
        page->is_dirty = 0;
    }
    return rc;
}

void bpm_flush_all(buffer_pool_manager_t* bpm) {
    for (size_t i = 0; i < bpm->pool_size; i++) {
        page_t* page = &bpm->pages[i];
        if (page->page_id != INVALID_PAGE_ID && page->is_dirty) {
            disk_manager_write_page(bpm->disk_manager, page->page_id, page->data);
            page->is_dirty = 0;
        }
    }
}
