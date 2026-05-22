#pragma once

#include "src/common/types.h"
#include "src/common/config.h"
#include "src/common/error.h"
#include <stdint.h>

typedef struct disk_manager disk_manager_t;

struct disk_manager {
    char    db_file[MAX_FILE_NAME];
    int32_t page_count;       /* total pages in file (including header page 0) */
    int32_t free_list_head;   /* page_id of first free page, or INVALID_PAGE_ID */
    int     is_open;
};

/* Create / open a database file.
 * If the file does not exist it is created and initialized (header page 0).
 * If it exists the header page is read to recover page_count and free_list. */
int  disk_manager_open(disk_manager_t* dm, const char* db_file);

/* Close the database file, flushing header page. */
void disk_manager_close(disk_manager_t* dm);

/* Allocate a new page.  Returns the page_id (>= 1) or negative error code.
 * Reuses freed pages from the free list when available. */
int  disk_manager_allocate_page(disk_manager_t* dm);

/* Deallocate a page, putting it on the free list. */
int  disk_manager_deallocate_page(disk_manager_t* dm, page_id_t page_id);

/* Read a full page into buf (must be PAGE_SIZE bytes). */
int  disk_manager_read_page(disk_manager_t* dm, page_id_t page_id, char* buf);

/* Write a full page from buf. */
int  disk_manager_write_page(disk_manager_t* dm, page_id_t page_id, const char* buf);

/* Flush the header page (page 0) to disk. */
int  disk_manager_flush_header(disk_manager_t* dm);
