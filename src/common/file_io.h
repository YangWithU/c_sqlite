#pragma once

#include "types.h"
#include "config.h"
#include <stdio.h>

typedef struct {
    FILE*        fp;
    char         file_name[MAX_FILE_NAME];
    uint32_t     page_count;
    int32_t      free_list_head;
} file_io_t;

/* Returns DB_OK on success, error code on failure */
int   file_io_open(file_io_t* io, const char* path, const char* mode);
void  file_io_close(file_io_t* io);
int   file_io_read_page(file_io_t* io, page_id_t page_id, void* buf);
int   file_io_write_page(file_io_t* io, page_id_t page_id, const void* buf);
void  file_io_flush(file_io_t* io);
