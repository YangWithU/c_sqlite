#pragma once

#include <stddef.h>

typedef struct hashmap_entry {
    void* key;
    void* value;
    struct hashmap_entry* next;
} hashmap_entry_t;

typedef struct {
    hashmap_entry_t** buckets;
    size_t            bucket_count;
    size_t            size;
    size_t          (*hash_fn)(const void* key);
    int             (*equal_fn)(const void* a, const void* b);
} hashmap_t;

void    hashmap_init(hashmap_t* map, size_t bucket_count,
                     size_t (*hash_fn)(const void*),
                     int (*equal_fn)(const void*, const void*));
void    hashmap_destroy(hashmap_t* map, void (*key_dtor)(void*), void (*val_dtor)(void*));
int     hashmap_put(hashmap_t* map, void* key, void* value);
void*   hashmap_get(const hashmap_t* map, const void* key);
int     hashmap_remove(hashmap_t* map, const void* key, void (*key_dtor)(void*), void (*val_dtor)(void*));
size_t  hashmap_size(const hashmap_t* map);

/* Iterator */
typedef struct {
    const hashmap_t* map;
    size_t           bucket_idx;
    hashmap_entry_t* entry;
} hashmap_iter_t;

void        hashmap_iter_init(hashmap_iter_t* it, const hashmap_t* map);
int         hashmap_iter_next(hashmap_iter_t* it, void** out_key, void** out_value);
