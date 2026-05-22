#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/storage/schema.h"
#include <stdint.h>

/* ========================================================================
 * BPM-backed heap file: linked list of slotted pages using buffer pool.
 *
 * Mirrors heap_file_t but uses bpm_fetch_page/bpm_new_page/bpm_unpin_page
 * instead of disk_manager directly. This is the version used by executors.
 * ======================================================================== */

typedef struct {
    buffer_pool_manager_t* bpm;
    schema_t*              schema;
    page_id_t              first_page_id;
    table_id_t             table_id;
} bpm_heap_t;

typedef struct {
    bpm_heap_t* heap;
    page_id_t   current_page_id;
    uint32_t    current_slot;
    page_t*     current_page;  /* pinned page */
} bpm_heap_iter_t;

/* Open/create a BPM heap file. first_page_id can be INVALID_PAGE_ID to start empty. */
int  bpm_heap_init(bpm_heap_t* hf, buffer_pool_manager_t* bpm, schema_t* schema,
                   table_id_t table_id, page_id_t first_page_id);

/* Insert a serialized tuple. Returns DB_OK and sets rid. */
int  bpm_heap_insert(bpm_heap_t* hf, const char* tuple_data, uint16_t tuple_size,
                     rid_t* rid);

/* Delete a tuple by RID. */
int  bpm_heap_delete(bpm_heap_t* hf, rid_t rid);

/* Update a tuple by RID. */
int  bpm_heap_update(bpm_heap_t* hf, rid_t rid,
                     const char* new_data, uint16_t new_size);

/* Get a tuple by RID. Data pointers are valid while page is pinned.
 * Caller must call bpm_heap_release_get when done. */
int  bpm_heap_get(bpm_heap_t* hf, rid_t rid,
                  const char** tuple_data, uint16_t* tuple_size, page_t** pinned_page);

/* Release a page pinned by bpm_heap_get. */
void bpm_heap_release_get(bpm_heap_t* hf, page_t* pinned_page);

/* ---- Iterator ---- */
int  bpm_heap_iter_init(bpm_heap_iter_t* it, bpm_heap_t* hf);
int  bpm_heap_iter_next(bpm_heap_iter_t* it, rid_t* rid,
                        const char** tuple_data, uint16_t* tuple_size);
void bpm_heap_iter_destroy(bpm_heap_iter_t* it);
