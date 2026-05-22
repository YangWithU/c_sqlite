#pragma once

#include "src/common/types.h"
#include "src/common/config.h"
#include "src/common/hashmap.h"
#include "src/common/vector.h"
#include "src/storage/schema.h"
#include "src/catalog/id_generator.h"
#include "src/buffer/buffer_pool_manager.h"

/* Table metadata stored in the catalog */
typedef struct {
    table_id_t  table_id;
    char        table_name[MAX_TABLE_NAME];
    page_id_t   root_page_id;
    char        table_type[16]; /* "user" or "system" */
} table_meta_t;

/* Index metadata stored in the catalog */
typedef struct {
    index_id_t  index_id;
    char        index_name[MAX_INDEX_NAME];
    table_id_t  table_id;
    column_id_t column_id;
    page_id_t   root_page_id;
    int         is_unique;
} index_meta_t;

/* System catalog — holds metadata for all tables, columns, and indexes.
 * In this phase, metadata is stored in-memory only; disk persistence will
 * be added when integrating all phases. */
typedef struct {
    buffer_pool_manager_t* bpm;
    id_generator_t        id_gen;
    hashmap_t             table_cache;   /* table_id (void* key) -> table_meta_t* */
    hashmap_t             name_to_id;    /* table_name (strdup'd key) -> table_id_t* */
    hashmap_t             schema_cache; /* table_id (void* key) -> schema_t* */
    hashmap_t             index_cache;   /* index_id (void* key) -> index_meta_t* */
    page_id_t             tables_page_id;   /* __tables heap first page (reserved) */
    page_id_t             columns_page_id;  /* __columns heap first page (reserved) */
    page_id_t             indexes_page_id;  /* __indexes heap first page (reserved) */
} catalog_t;

/* Initialize the catalog. Creates in-memory caches and registers system tables.
 * Returns DB_OK on success. */
int  catalog_init(catalog_t* cat, buffer_pool_manager_t* bpm);

/* Destroy the catalog, freeing all cached metadata. */
void catalog_destroy(catalog_t* cat);

/* Create a new user table with the given name and schema.
 * Returns the new table_id on success, or INVALID_TABLE_ID on failure
 * (e.g. duplicate name). */
table_id_t catalog_create_table(catalog_t* cat, const char* name, const schema_t* schema);

/* Look up a table by name. On success, writes metadata to *out and returns DB_OK.
 * Returns DB_UNKNOWN_TABLE if not found. */
int  catalog_get_table(catalog_t* cat, const char* name, table_meta_t* out);

/* Drop a table by name. Removes all associated metadata and indexes.
 * Returns DB_OK on success, DB_UNKNOWN_TABLE if not found. */
int  catalog_drop_table(catalog_t* cat, const char* name);

/* Update a table's root_page_id in the catalog cache.
 * Returns DB_OK on success, DB_UNKNOWN_TABLE if not found. */
int  catalog_update_root_page(catalog_t* cat, table_id_t table_id, page_id_t root_page_id);

/* Look up a table's schema by its table_id. On success, copies the schema
 * into *out and returns DB_OK. Caller must call schema_destroy on *out when done.
 * Returns DB_UNKNOWN_TABLE if not found. */
int  catalog_get_schema(catalog_t* cat, table_id_t table_id, schema_t* out);

/* List all user table names. Pushes strdup'd strings into *out (a vector_t of char*).
 * Caller must free each string and call vector_destroy.
 * Returns DB_OK on success. */
int  catalog_list_tables(catalog_t* cat, vector_t* out_names);

/* Create a new index. Returns the new index_id on success,
 * or INVALID_INDEX_ID on failure (e.g. duplicate name). */
index_id_t catalog_create_index(catalog_t* cat, const char* name, table_id_t table_id,
                                column_id_t col_id, page_id_t root_page_id, int unique);

/* Look up an index by name. On success, writes metadata to *out and returns DB_OK.
 * Returns DB_UNKNOWN_TABLE if not found. */
int  catalog_get_index(catalog_t* cat, const char* name, index_meta_t* out);

/* Get all indexes for a given table. Pushes index_meta_t copies into *out
 * (a vector_t of index_meta_t). Returns DB_OK. */
int  catalog_get_table_indexes(catalog_t* cat, table_id_t table_id, vector_t* out);

/* Drop an index by name. Returns DB_OK on success, DB_UNKNOWN_TABLE if not found. */
int  catalog_drop_index(catalog_t* cat, const char* name);
