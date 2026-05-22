#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/storage/disk_manager.h"
#include "src/storage/schema.h"
#include <stdint.h>

/* ========================================================================
 * Heap file: a linked list of slotted pages storing tuples.
 *
 * Uses disk_manager_t directly for page I/O (BPM integration deferred).
 * Each heap file has a first_page_id; pages are linked via next_page_id
 * in the page header. Tuples are identified by RID (page_id, slot_id).
 * ======================================================================== */

typedef struct {
    disk_manager_t* dm;          /* disk manager for page I/O */
    schema_t*       schema;      /* schema for tuple serialization */
    page_id_t       first_page_id;  /* first data page (INVALID_PAGE_ID if empty) */
} heap_file_t;

/* Open/create a heap file. first_page_id can be INVALID_PAGE_ID to start empty. */
int  heap_file_init(heap_file_t* hf, disk_manager_t* dm, schema_t* schema,
                    page_id_t first_page_id);

/* Insert a serialized tuple into the heap file.
 * Returns DB_OK and sets rid to the new tuple's RID. */
int  heap_file_insert(heap_file_t* hf, const char* tuple_data, uint16_t tuple_size,
                      rid_t* rid);

/* Delete a tuple by RID.
 * Returns DB_OK or error code. */
int  heap_file_delete(heap_file_t* hf, rid_t rid);

/* Update a tuple by RID with new serialized data.
 * Returns DB_OK or error code. */
int  heap_file_update(heap_file_t* hf, rid_t rid,
                      const char* new_data, uint16_t new_size);

/* Get a tuple by RID.
 * tuple_data and tuple_size are set to point into a page buffer that is
 * valid until the next heap_file call (single internal buffer).
 * Returns DB_OK or error code. */
int  heap_file_get(heap_file_t* hf, rid_t rid,
                   const char** tuple_data, uint16_t* tuple_size);

/* ========================================================================
 * Heap iterator: scans all tuples across all pages.
 *
 * Usage:
 *   heap_iter_t it;
 *   heap_iter_init(&it, &hf);
 *   while (heap_iter_next(&it, &rid, &data, &size) == DB_OK) { ... }
 * ======================================================================== */

typedef struct {
    heap_file_t*  hf;
    page_id_t     current_page_id;
    uint32_t      current_slot;
    char*         page_buf;       /* buffered page data */
    int           page_loaded;   /* whether page_buf has valid data */
} heap_iter_t;

/* Initialize an iterator for scanning the heap file. */
int  heap_iter_init(heap_iter_t* it, heap_file_t* hf);

/* Advance to the next tuple. Sets rid, tuple_data, and tuple_size.
 * Returns DB_OK if a tuple was found, or a non-zero value when done. */
int  heap_iter_next(heap_iter_t* it, rid_t* rid,
                    const char** tuple_data, uint16_t* tuple_size);

/* Destroy the iterator, freeing its buffer. */
void heap_iter_destroy(heap_iter_t* it);
