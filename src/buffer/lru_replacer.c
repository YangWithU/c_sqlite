#include "src/buffer/lru_replacer.h"
#include "src/common/mem.h"
#include "src/common/macros.h"
#include <stdint.h>

/* ---- Integer key helpers for hashmap (frame_id_t stored as void*) ---- */

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

static void* frame_id_to_key(frame_id_t fid) {
    return (void*)(uintptr_t)(uint32_t)fid;
}

__attribute__((unused))
static frame_id_t key_to_frame_id(const void* key) {
    return (frame_id_t)(uint32_t)(uintptr_t)key;
}

/* ---- LRU Replacer implementation ---- */

int lru_replacer_init(lru_replacer_t* replacer, size_t capacity) {
    list_init(&replacer->cold_list);
    list_init(&replacer->hot_list);
    replacer->capacity = capacity;
    replacer->size = 0;
    hashmap_init(&replacer->frame_map, 64, int_hash, int_equal);
    return 0;
}

void lru_replacer_destroy(lru_replacer_t* replacer) {
    /* Free all entries in the frame_map. */
    hashmap_iter_t it;
    hashmap_iter_init(&it, &replacer->frame_map);
    void* key;
    void* value;
    while (hashmap_iter_next(&it, &key, &value)) {
        (void)key;
        db_free(value);
    }
    hashmap_destroy(&replacer->frame_map, NULL, NULL);
    list_init(&replacer->cold_list);
    list_init(&replacer->hot_list);
    replacer->size = 0;
}

int lru_replacer_victim(lru_replacer_t* replacer, frame_id_t* frame_id) {
    list_node_t* node = list_pop_back(&replacer->cold_list);
    if (!node) {
        node = list_pop_back(&replacer->hot_list);
    }
    if (!node) {
        return -1;  /* nothing to evict */
    }

    lru_replacer_entry_t* entry =
        CONTAINER_OF(node, lru_replacer_entry_t, node);

    *frame_id = entry->frame_id;

    /* Remove from frame_map */
    hashmap_remove(&replacer->frame_map, frame_id_to_key(entry->frame_id), NULL, NULL);

    db_free(entry);
    replacer->size--;
    return 0;
}

void lru_replacer_pin(lru_replacer_t* replacer, frame_id_t frame_id) {
    void* val = hashmap_get(&replacer->frame_map, frame_id_to_key(frame_id));
    if (!val) {
        /* Not in replacer — nothing to do */
        return;
    }

    lru_replacer_entry_t* entry = (lru_replacer_entry_t*)val;

    /* Remove from whichever list it's in, maintaining list size */
    if (entry->in_hot) {
        list_remove_from(&replacer->hot_list, &entry->node);
    } else {
        list_remove_from(&replacer->cold_list, &entry->node);
    }

    /* Remove from frame_map */
    hashmap_remove(&replacer->frame_map, frame_id_to_key(frame_id), NULL, NULL);

    db_free(entry);
    replacer->size--;
}

void lru_replacer_unpin(lru_replacer_t* replacer, frame_id_t frame_id) {
    void* val = hashmap_get(&replacer->frame_map, frame_id_to_key(frame_id));
    if (val) {
        /* Already in replacer — move to hot_list (second+ access) */
        lru_replacer_entry_t* entry = (lru_replacer_entry_t*)val;

        /* Remove from current list, maintaining list size */
        if (entry->in_hot) {
            list_remove_from(&replacer->hot_list, &entry->node);
        } else {
            list_remove_from(&replacer->cold_list, &entry->node);
        }

        entry->access_count++;
        entry->in_hot = 1;
        /* Push to front of hot_list (most recently accessed) */
        list_push_front(&replacer->hot_list, &entry->node);
    } else {
        /* New entry — goes to cold_list */
        lru_replacer_entry_t* entry = db_malloc(sizeof(lru_replacer_entry_t));
        if (!entry) return;  /* allocation failure */
        entry->frame_id = frame_id;
        entry->access_count = 1;
        entry->in_hot = 0;
        entry->node.prev = NULL;
        entry->node.next = NULL;

        hashmap_put(&replacer->frame_map, frame_id_to_key(frame_id), entry);
        list_push_front(&replacer->cold_list, &entry->node);
        replacer->size++;
    }
}

size_t lru_replacer_size(lru_replacer_t* replacer) {
    return replacer->size;
}
