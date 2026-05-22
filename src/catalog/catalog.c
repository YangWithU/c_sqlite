#define _POSIX_C_SOURCE 200809L

#include "src/catalog/catalog.h"
#include "src/common/mem.h"
#include "src/common/error.h"
#include "src/common/macros.h"

#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * Hash / equality helpers for integer-keyed hashmaps
 * ======================================================================== */

static size_t int_hash_fn(const void* key) {
    uintptr_t k = (uintptr_t)key;
    k = ((k >> 16) ^ k) * 0x45d9f3b;
    k = ((k >> 16) ^ k) * 0x45d9f3b;
    k = (k >> 16) ^ k;
    return (size_t)k;
}

static int int_equal_fn(const void* a, const void* b) {
    return (uintptr_t)a == (uintptr_t)b;
}

/* ========================================================================
 * Hash / equality helpers for string-keyed hashmaps
 * ======================================================================== */

static size_t str_hash_fn(const void* key) {
    const char* s = (const char*)key;
    size_t h = 5381;
    while (*s)
        h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

static int str_equal_fn(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b) == 0;
}

/* ========================================================================
 * Destructor helpers for hashmap_destroy
 * ======================================================================== */

static void free_key_str(void* key)   { db_free(key); }
static void free_val_table_meta(void* val) { db_free(val); }

static void free_val_schema(void* val) {
    schema_t* s = (schema_t*)val;
    schema_destroy(s);
    db_free(s);
}

static void free_val_index_meta(void* val) { db_free(val); }

static void free_val_table_id(void* val) { db_free(val); }

/* ========================================================================
 * Internal: register the three system tables in the catalog caches
 * ======================================================================== */

static void register_system_table(catalog_t* cat, const char* name, table_id_t tid) {
    table_meta_t* meta = db_calloc(1, sizeof(table_meta_t));
    meta->table_id     = tid;
    strncpy(meta->table_name, name, MAX_TABLE_NAME - 1);
    meta->table_name[MAX_TABLE_NAME - 1] = '\0';
    meta->root_page_id = INVALID_PAGE_ID;
    strncpy(meta->table_type, "system", sizeof(meta->table_type) - 1);
    meta->table_type[sizeof(meta->table_type) - 1] = '\0';

    hashmap_put(&cat->table_cache, (void*)(uintptr_t)tid, meta);

    table_id_t* id_ptr = db_malloc(sizeof(table_id_t));
    *id_ptr = tid;
    hashmap_put(&cat->name_to_id, strdup(name), id_ptr);
}

/* ========================================================================
 * catalog_init
 * ======================================================================== */

int catalog_init(catalog_t* cat, buffer_pool_manager_t* bpm) {
    memset(cat, 0, sizeof(catalog_t));
    cat->bpm = bpm;

    id_generator_init(&cat->id_gen);

    hashmap_init(&cat->table_cache,  64, int_hash_fn, int_equal_fn);
    hashmap_init(&cat->name_to_id,   64, str_hash_fn, str_equal_fn);
    hashmap_init(&cat->schema_cache, 64, int_hash_fn, int_equal_fn);
    hashmap_init(&cat->index_cache,  64, int_hash_fn, int_equal_fn);

    cat->tables_page_id  = INVALID_PAGE_ID;
    cat->columns_page_id = INVALID_PAGE_ID;
    cat->indexes_page_id = INVALID_PAGE_ID;

    /* Register system tables with fixed IDs 0, 1, 2.
     * These consume the first three IDs so user tables start at 1. */
    register_system_table(cat, "__tables",  0);
    register_system_table(cat, "__columns", 1);
    register_system_table(cat, "__indexes", 2);

    return DB_OK;
}

/* ========================================================================
 * catalog_destroy
 * ======================================================================== */

void catalog_destroy(catalog_t* cat) {
    hashmap_destroy(&cat->table_cache,  NULL,             free_val_table_meta);
    hashmap_destroy(&cat->name_to_id,  free_key_str,     free_val_table_id);
    hashmap_destroy(&cat->schema_cache, NULL,             free_val_schema);
    hashmap_destroy(&cat->index_cache,  NULL,             free_val_index_meta);
}

/* ========================================================================
 * catalog_create_table
 * ======================================================================== */

table_id_t catalog_create_table(catalog_t* cat, const char* name, const schema_t* schema) {
    /* Check for duplicate name */
    if (hashmap_get(&cat->name_to_id, name) != NULL)
        return INVALID_TABLE_ID;

    table_id_t tid = id_generator_next_table(&cat->id_gen);

    /* Create table_meta_t */
    table_meta_t* meta = db_calloc(1, sizeof(table_meta_t));
    meta->table_id     = tid;
    strncpy(meta->table_name, name, MAX_TABLE_NAME - 1);
    meta->table_name[MAX_TABLE_NAME - 1] = '\0';
    meta->root_page_id = INVALID_PAGE_ID;
    strncpy(meta->table_type, "user", sizeof(meta->table_type) - 1);
    meta->table_type[sizeof(meta->table_type) - 1] = '\0';

    /* Insert into caches */
    hashmap_put(&cat->table_cache, (void*)(uintptr_t)tid, meta);

    table_id_t* id_ptr = db_malloc(sizeof(table_id_t));
    *id_ptr = tid;
    hashmap_put(&cat->name_to_id, strdup(name), id_ptr);

    /* Deep-copy the schema and cache it */
    schema_t* schema_copy = db_malloc(sizeof(schema_t));
    schema_create(schema_copy, schema->columns, schema->num_columns);
    hashmap_put(&cat->schema_cache, (void*)(uintptr_t)tid, schema_copy);

    return tid;
}

/* ========================================================================
 * catalog_get_table
 * ======================================================================== */

int catalog_get_table(catalog_t* cat, const char* name, table_meta_t* out) {
    table_id_t* id_ptr = (table_id_t*)hashmap_get(&cat->name_to_id, name);
    if (!id_ptr)
        return DB_UNKNOWN_TABLE;

    table_meta_t* meta = (table_meta_t*)hashmap_get(&cat->table_cache,
                                                     (void*)(uintptr_t)*id_ptr);
    if (!meta)
        return DB_UNKNOWN_TABLE;

    *out = *meta;
    return DB_OK;
}

/* ========================================================================
 * catalog_drop_table
 * ======================================================================== */

int catalog_drop_table(catalog_t* cat, const char* name) {
    table_id_t* id_ptr = (table_id_t*)hashmap_get(&cat->name_to_id, name);
    if (!id_ptr)
        return DB_UNKNOWN_TABLE;

    table_id_t tid = *id_ptr;

    /* Remove all indexes that belong to this table */
    {
        vector_t indexes_to_drop;
        vector_init(&indexes_to_drop, sizeof(index_id_t));

        hashmap_iter_t it;
        hashmap_iter_init(&it, &cat->index_cache);
        void* ikey;
        void* ival;
        while (hashmap_iter_next(&it, &ikey, &ival)) {
            index_meta_t* imeta = (index_meta_t*)ival;
            if (imeta->table_id == tid) {
                vector_push(&indexes_to_drop, &imeta->index_id);
            }
        }

        for (size_t i = 0; i < vector_size(&indexes_to_drop); i++) {
            index_id_t* iid = (index_id_t*)vector_at(&indexes_to_drop, i);
            /* Build the index name so we can remove from name-based lookups if needed.
             * For now we only have the index_cache keyed by id, so remove directly. */
            index_meta_t* imeta = (index_meta_t*)hashmap_get(&cat->index_cache,
                                                              (void*)(uintptr_t)*iid);
            if (imeta) {
                /* We need the name to remove from any name-keyed map, but we only
                 * have index_cache keyed by index_id. Store name temporarily. */
                char idx_name[MAX_INDEX_NAME];
                strncpy(idx_name, imeta->index_name, MAX_INDEX_NAME - 1);
                idx_name[MAX_INDEX_NAME - 1] = '\0';
                hashmap_remove(&cat->index_cache, (void*)(uintptr_t)*iid, NULL, free_val_index_meta);
            }
        }

        vector_destroy(&indexes_to_drop);
    }

    /* Remove from caches */
    hashmap_remove(&cat->table_cache,  (void*)(uintptr_t)tid, NULL,             free_val_table_meta);
    hashmap_remove(&cat->name_to_id,   name,                 free_key_str,     free_val_table_id);
    hashmap_remove(&cat->schema_cache,  (void*)(uintptr_t)tid, NULL,             free_val_schema);

    return DB_OK;
}

/* ========================================================================
 * catalog_get_schema
 * ======================================================================== */

int catalog_get_schema(catalog_t* cat, table_id_t table_id, schema_t* out) {
    schema_t* cached = (schema_t*)hashmap_get(&cat->schema_cache,
                                                (void*)(uintptr_t)table_id);
    if (!cached)
        return DB_UNKNOWN_TABLE;

    /* Deep copy the schema for the caller */
    return schema_create(out, cached->columns, cached->num_columns);
}

/* ========================================================================
 * catalog_list_tables
 * ======================================================================== */

int catalog_list_tables(catalog_t* cat, vector_t* out_names) {
    vector_init(out_names, sizeof(char*));

    hashmap_iter_t it;
    hashmap_iter_init(&it, &cat->table_cache);
    void* ikey;
    void* ival;
    while (hashmap_iter_next(&it, &ikey, &ival)) {
        table_meta_t* meta = (table_meta_t*)ival;
        if (strcmp(meta->table_type, "user") == 0) {
            char* name_copy = strdup(meta->table_name);
            vector_push(out_names, &name_copy);
        }
    }

    return DB_OK;
}

/* ========================================================================
 * catalog_create_index
 * ======================================================================== */

index_id_t catalog_create_index(catalog_t* cat, const char* name, table_id_t table_id,
                                column_id_t col_id, page_id_t root_page_id, int unique) {
    /* Check for duplicate index name by scanning the index cache */
    hashmap_iter_t it;
    hashmap_iter_init(&it, &cat->index_cache);
    void* ikey;
    void* ival;
    while (hashmap_iter_next(&it, &ikey, &ival)) {
        index_meta_t* existing = (index_meta_t*)ival;
        if (strcmp(existing->index_name, name) == 0)
            return INVALID_INDEX_ID;
    }

    index_id_t iid = id_generator_next_index(&cat->id_gen);

    index_meta_t* meta = db_calloc(1, sizeof(index_meta_t));
    meta->index_id     = iid;
    strncpy(meta->index_name, name, MAX_INDEX_NAME - 1);
    meta->index_name[MAX_INDEX_NAME - 1] = '\0';
    meta->table_id     = table_id;
    meta->column_id    = col_id;
    meta->root_page_id = root_page_id;
    meta->is_unique    = unique;

    hashmap_put(&cat->index_cache, (void*)(uintptr_t)iid, meta);

    return iid;
}

/* ========================================================================
 * catalog_get_index
 * ======================================================================== */

int catalog_get_index(catalog_t* cat, const char* name, index_meta_t* out) {
    hashmap_iter_t it;
    hashmap_iter_init(&it, &cat->index_cache);
    void* ikey;
    void* ival;
    while (hashmap_iter_next(&it, &ikey, &ival)) {
        index_meta_t* meta = (index_meta_t*)ival;
        if (strcmp(meta->index_name, name) == 0) {
            *out = *meta;
            return DB_OK;
        }
    }
    return DB_UNKNOWN_TABLE;
}

/* ========================================================================
 * catalog_get_table_indexes
 * ======================================================================== */

int catalog_get_table_indexes(catalog_t* cat, table_id_t table_id, vector_t* out) {
    vector_init(out, sizeof(index_meta_t));

    hashmap_iter_t it;
    hashmap_iter_init(&it, &cat->index_cache);
    void* ikey;
    void* ival;
    while (hashmap_iter_next(&it, &ikey, &ival)) {
        index_meta_t* meta = (index_meta_t*)ival;
        if (meta->table_id == table_id) {
            vector_push(out, meta);
        }
    }

    return DB_OK;
}

/* ========================================================================
 * catalog_drop_index
 * ======================================================================== */

int catalog_drop_index(catalog_t* cat, const char* name) {
    /* Find the index by name to get its index_id */
    hashmap_iter_t it;
    hashmap_iter_init(&it, &cat->index_cache);
    void* ikey;
    void* ival;
    while (hashmap_iter_next(&it, &ikey, &ival)) {
        index_meta_t* meta = (index_meta_t*)ival;
        if (strcmp(meta->index_name, name) == 0) {
            index_id_t iid = meta->index_id;
            hashmap_remove(&cat->index_cache, (void*)(uintptr_t)iid, NULL, free_val_index_meta);
            return DB_OK;
        }
    }
    return DB_UNKNOWN_TABLE;
}
