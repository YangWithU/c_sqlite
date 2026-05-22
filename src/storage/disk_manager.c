#include "src/storage/disk_manager.h"
#include "src/common/file_io.h"
#include "src/common/mem.h"
#include "src/common/logger.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

/* -----------------------------------------------------------------------
 * Disk file header (page 0) layout:
 *   Offset 0   : magic "MINISQLITE\0"  (10 bytes)
 *   Offset 10  : version      uint32
 *   Offset 14  : page_count   uint32
 *   Offset 18  : free_list_head int32
 *   Offset 22  : reserved     (PAGE_SIZE - 22 bytes, zeroed)
 * ----------------------------------------------------------------------- */

#define HEADER_OFFSET_MAGIC       0
#define HEADER_OFFSET_VERSION    10
#define HEADER_OFFSET_PAGE_COUNT 14
#define HEADER_OFFSET_FREE_LIST  18
#define HEADER_SIZE              22

static void write_header_to_buf(const disk_manager_t* dm, char* buf) {
    memset(buf, 0, PAGE_SIZE);
    memcpy(buf + HEADER_OFFSET_MAGIC, DB_MAGIC, DB_MAGIC_LEN);
    uint32_t version = DB_VERSION;
    memcpy(buf + HEADER_OFFSET_VERSION, &version, sizeof(uint32_t));
    uint32_t pc = (uint32_t)dm->page_count;
    memcpy(buf + HEADER_OFFSET_PAGE_COUNT, &pc, sizeof(uint32_t));
    int32_t fl = dm->free_list_head;
    memcpy(buf + HEADER_OFFSET_FREE_LIST, &fl, sizeof(int32_t));
}

static int read_header_from_buf(disk_manager_t* dm, const char* buf) {
    char magic[DB_MAGIC_LEN];
    memcpy(magic, buf + HEADER_OFFSET_MAGIC, DB_MAGIC_LEN);
    if (memcmp(magic, DB_MAGIC, DB_MAGIC_LEN) != 0) {
        LOG_ERROR("Invalid database file: bad magic");
        return DB_IO_ERROR;
    }
    uint32_t version;
    memcpy(&version, buf + HEADER_OFFSET_VERSION, sizeof(uint32_t));
    if (version != DB_VERSION) {
        LOG_ERROR("Unsupported database version: %u", version);
        return DB_IO_ERROR;
    }
    uint32_t pc;
    memcpy(&pc, buf + HEADER_OFFSET_PAGE_COUNT, sizeof(uint32_t));
    dm->page_count = (int32_t)pc;
    int32_t fl;
    memcpy(&fl, buf + HEADER_OFFSET_FREE_LIST, sizeof(int32_t));
    dm->free_list_head = fl;
    return DB_OK;
}

int disk_manager_open(disk_manager_t* dm, const char* db_file) {
    memset(dm, 0, sizeof(*dm));
    dm->free_list_head = INVALID_PAGE_ID;

    /* Try opening an existing file first */
    file_io_t io;
    int rc = file_io_open(&io, db_file, "rb+");
    if (rc == DB_OK) {
        /* Existing file: read header page */
        char* hdr = (char*)page_alloc(1);
        if (!hdr) {
            file_io_close(&io);
            return DB_OUT_OF_MEMORY;
        }
        rc = file_io_read_page(&io, 0, hdr);
        if (rc == DB_OK) {
            rc = read_header_from_buf(dm, hdr);
        }
        page_free(hdr);
        if (rc != DB_OK) {
            file_io_close(&io);
            return rc;
        }
        strncpy(dm->db_file, db_file, MAX_FILE_NAME - 1);
        dm->is_open = 1;
        file_io_close(&io);
        return DB_OK;
    }

    /* Create new file */
    rc = file_io_open(&io, db_file, "wb+");
    if (rc != DB_OK) {
        LOG_ERROR("Cannot create database file: %s", db_file);
        return DB_IO_ERROR;
    }

    dm->page_count = 1;   /* page 0 is the header */
    dm->free_list_head = INVALID_PAGE_ID;

    /* Write initial header page */
    char* hdr = (char*)page_alloc(1);
    if (!hdr) {
        file_io_close(&io);
        return DB_OUT_OF_MEMORY;
    }
    write_header_to_buf(dm, hdr);
    rc = file_io_write_page(&io, 0, hdr);
    page_free(hdr);
    file_io_flush(&io);
    file_io_close(&io);

    if (rc != DB_OK)
        return rc;

    strncpy(dm->db_file, db_file, MAX_FILE_NAME - 1);
    dm->is_open = 1;
    return DB_OK;
}

void disk_manager_close(disk_manager_t* dm) {
    if (dm->is_open) {
        disk_manager_flush_header(dm);
        dm->is_open = 0;
    }
}

int disk_manager_allocate_page(disk_manager_t* dm) {
    if (!dm->is_open)
        return DB_FILE_NOT_OPEN;

    file_io_t io;
    int rc = file_io_open(&io, dm->db_file, "rb+");
    if (rc != DB_OK)
        return rc;

    page_id_t allocated_id;

    if (dm->free_list_head != INVALID_PAGE_ID) {
        /* Reuse a freed page from the free list */
        allocated_id = dm->free_list_head;

        /* Read the freed page to get the next_free_page_id (first 4 bytes) */
        char* page_buf = (char*)page_alloc(1);
        if (!page_buf) {
            file_io_close(&io);
            return DB_OUT_OF_MEMORY;
        }
        rc = file_io_read_page(&io, allocated_id, page_buf);
        if (rc != DB_OK) {
            page_free(page_buf);
            file_io_close(&io);
            return rc;
        }
        int32_t next_free;
        memcpy(&next_free, page_buf, sizeof(int32_t));
        dm->free_list_head = next_free;

        /* Zero out the page before returning */
        memset(page_buf, 0, PAGE_SIZE);
        rc = file_io_write_page(&io, allocated_id, page_buf);
        page_free(page_buf);
        if (rc != DB_OK) {
            file_io_close(&io);
            return rc;
        }
    } else {
        /* Extend the file by appending a new page */
        allocated_id = dm->page_count;

        char* page_buf = (char*)page_alloc(1);
        if (!page_buf) {
            file_io_close(&io);
            return DB_OUT_OF_MEMORY;
        }
        memset(page_buf, 0, PAGE_SIZE);
        rc = file_io_write_page(&io, allocated_id, page_buf);
        page_free(page_buf);
        if (rc != DB_OK) {
            file_io_close(&io);
            return rc;
        }
        dm->page_count++;
    }

    /* Flush the updated header */
    char* hdr = (char*)page_alloc(1);
    if (!hdr) {
        file_io_close(&io);
        return DB_OUT_OF_MEMORY;
    }
    write_header_to_buf(dm, hdr);
    rc = file_io_write_page(&io, 0, hdr);
    page_free(hdr);
    file_io_flush(&io);
    file_io_close(&io);

    return (rc == DB_OK) ? allocated_id : rc;
}

int disk_manager_deallocate_page(disk_manager_t* dm, page_id_t page_id) {
    if (!dm->is_open)
        return DB_FILE_NOT_OPEN;
    if (page_id <= 0 || page_id >= dm->page_count)
        return DB_PAGE_NOT_FOUND;

    file_io_t io;
    int rc = file_io_open(&io, dm->db_file, "rb+");
    if (rc != DB_OK)
        return rc;

    /* Write the current free_list_head as next_free in the first 4 bytes */
    char* page_buf = (char*)page_alloc(1);
    if (!page_buf) {
        file_io_close(&io);
        return DB_OUT_OF_MEMORY;
    }

    /* Read existing page content first (to preserve rest of page) */
    rc = file_io_read_page(&io, page_id, page_buf);
    if (rc != DB_OK) {
        page_free(page_buf);
        file_io_close(&io);
        return rc;
    }

    /* Overwrite first 4 bytes with the current free list head */
    memcpy(page_buf, &dm->free_list_head, sizeof(int32_t));
    rc = file_io_write_page(&io, page_id, page_buf);
    page_free(page_buf);
    if (rc != DB_OK) {
        file_io_close(&io);
        return rc;
    }

    /* Update free list: this page becomes the new head */
    dm->free_list_head = page_id;

    /* Flush header */
    char* hdr = (char*)page_alloc(1);
    if (!hdr) {
        file_io_close(&io);
        return DB_OUT_OF_MEMORY;
    }
    write_header_to_buf(dm, hdr);
    rc = file_io_write_page(&io, 0, hdr);
    page_free(hdr);
    file_io_flush(&io);
    file_io_close(&io);

    return rc;
}

int disk_manager_read_page(disk_manager_t* dm, page_id_t page_id, char* buf) {
    if (!dm->is_open)
        return DB_FILE_NOT_OPEN;
    if (page_id < 0 || page_id >= dm->page_count)
        return DB_PAGE_NOT_FOUND;

    file_io_t io;
    int rc = file_io_open(&io, dm->db_file, "rb");
    if (rc != DB_OK)
        return rc;
    rc = file_io_read_page(&io, page_id, buf);
    file_io_close(&io);
    return rc;
}

int disk_manager_write_page(disk_manager_t* dm, page_id_t page_id, const char* buf) {
    if (!dm->is_open)
        return DB_FILE_NOT_OPEN;
    if (page_id < 0 || page_id >= dm->page_count)
        return DB_PAGE_NOT_FOUND;

    file_io_t io;
    int rc = file_io_open(&io, dm->db_file, "rb+");
    if (rc != DB_OK)
        return rc;
    rc = file_io_write_page(&io, page_id, buf);
    file_io_flush(&io);
    file_io_close(&io);
    return rc;
}

int disk_manager_flush_header(disk_manager_t* dm) {
    if (!dm->is_open)
        return DB_FILE_NOT_OPEN;

    file_io_t io;
    int rc = file_io_open(&io, dm->db_file, "rb+");
    if (rc != DB_OK)
        return rc;
    char* hdr = (char*)page_alloc(1);
    if (!hdr) {
        file_io_close(&io);
        return DB_OUT_OF_MEMORY;
    }
    write_header_to_buf(dm, hdr);
    rc = file_io_write_page(&io, 0, hdr);
    page_free(hdr);
    file_io_flush(&io);
    file_io_close(&io);
    return rc;
}
