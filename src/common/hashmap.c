#include "hashmap.h"
#include "mem.h"
#include <string.h>

#define HASHMAP_DEFAULT_BUCKETS 64

void hashmap_init(hashmap_t* map, size_t bucket_count,
                  size_t (*hash_fn)(const void*),
                  int (*equal_fn)(const void*, const void*)) {
    if (bucket_count == 0)
        bucket_count = HASHMAP_DEFAULT_BUCKETS;
    map->bucket_count = bucket_count;
    map->size = 0;
    map->hash_fn = hash_fn;
    map->equal_fn = equal_fn;
    map->buckets = db_calloc(bucket_count, sizeof(hashmap_entry_t*));
}

void hashmap_destroy(hashmap_t* map, void (*key_dtor)(void*), void (*val_dtor)(void*)) {
    for (size_t i = 0; i < map->bucket_count; i++) {
        hashmap_entry_t* cur = map->buckets[i];
        while (cur) {
            hashmap_entry_t* next = cur->next;
            if (key_dtor) key_dtor(cur->key);
            if (val_dtor) val_dtor(cur->value);
            db_free(cur);
            cur = next;
        }
    }
    db_free(map->buckets);
    map->buckets = NULL;
    map->size = 0;
}

int hashmap_put(hashmap_t* map, void* key, void* value) {
    size_t idx = map->hash_fn(key) % map->bucket_count;

    /* Check if key already exists — update value */
    hashmap_entry_t* cur = map->buckets[idx];
    while (cur) {
        if (map->equal_fn(cur->key, key)) {
            cur->value = value;
            return 0;
        }
        cur = cur->next;
    }

    /* Insert new entry at head */
    hashmap_entry_t* entry = db_malloc(sizeof(hashmap_entry_t));
    if (!entry) return -1;
    entry->key = key;
    entry->value = value;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->size++;
    return 0;
}

void* hashmap_put_old(hashmap_t* map, void* key, void* value) {
    size_t idx = map->hash_fn(key) % map->bucket_count;

    /* Check if key already exists — update value and return old */
    hashmap_entry_t* cur = map->buckets[idx];
    while (cur) {
        if (map->equal_fn(cur->key, key)) {
            void* old_value = cur->value;
            cur->value = value;
            return old_value;
        }
        cur = cur->next;
    }

    /* Insert new entry at head */
    hashmap_entry_t* entry = db_malloc(sizeof(hashmap_entry_t));
    if (!entry) return NULL;
    entry->key = key;
    entry->value = value;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->size++;
    return NULL;
}

void* hashmap_get(const hashmap_t* map, const void* key) {
    size_t idx = map->hash_fn(key) % map->bucket_count;
    hashmap_entry_t* cur = map->buckets[idx];
    while (cur) {
        if (map->equal_fn(cur->key, key))
            return cur->value;
        cur = cur->next;
    }
    return NULL;
}

int hashmap_remove(hashmap_t* map, const void* key,
                   void (*key_dtor)(void*), void (*val_dtor)(void*)) {
    size_t idx = map->hash_fn(key) % map->bucket_count;
    hashmap_entry_t* prev = NULL;
    hashmap_entry_t* cur = map->buckets[idx];
    while (cur) {
        if (map->equal_fn(cur->key, key)) {
            if (prev)
                prev->next = cur->next;
            else
                map->buckets[idx] = cur->next;
            if (key_dtor) key_dtor(cur->key);
            if (val_dtor) val_dtor(cur->value);
            db_free(cur);
            map->size--;
            return 0;
        }
        prev = cur;
        cur = cur->next;
    }
    return -1; /* not found */
}

size_t hashmap_size(const hashmap_t* map) {
    return map->size;
}

/* Iterator implementation */
void hashmap_iter_init(hashmap_iter_t* it, const hashmap_t* map) {
    it->map = map;
    it->bucket_idx = 0;
    it->entry = NULL;
}

int hashmap_iter_next(hashmap_iter_t* it, void** out_key, void** out_value) {
    const hashmap_t* map = it->map;

    /* If we have a current entry, advance to next in chain */
    if (it->entry)
        it->entry = it->entry->next;

    /* Find next non-empty bucket */
    while (!it->entry && it->bucket_idx < map->bucket_count) {
        it->entry = map->buckets[it->bucket_idx];
        it->bucket_idx++;
    }

    if (!it->entry)
        return 0; /* done */

    if (out_key)   *out_key = it->entry->key;
    if (out_value) *out_value = it->entry->value;
    return 1; /* has next */
}
