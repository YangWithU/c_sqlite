#include "file_io.h"
#include "error.h"
#include "platform.h"
#include <string.h>

int file_io_open(file_io_t* io, const char* path, const char* mode) {
    io->fp = fopen(path, mode);
    if (!io->fp)
        return DB_IO_ERROR;
    strncpy(io->file_name, path, MAX_FILE_NAME - 1);
    io->file_name[MAX_FILE_NAME - 1] = '\0';
    io->page_count = 0;
    io->free_list_head = INVALID_PAGE_ID;
    return DB_OK;
}

void file_io_close(file_io_t* io) {
    if (io->fp) {
        fclose(io->fp);
        io->fp = NULL;
    }
}

int file_io_read_page(file_io_t* io, page_id_t page_id, void* buf) {
    if (!io->fp)
        return DB_FILE_NOT_OPEN;
    if (page_id < 0)
        return DB_PAGE_NOT_FOUND;
    long offset = (long)page_id * PAGE_SIZE;
    if (fseek(io->fp, offset, SEEK_SET) != 0)
        return DB_IO_ERROR;
    size_t read = fread(buf, PAGE_SIZE, 1, io->fp);
    if (read != 1)
        return DB_IO_ERROR;
    return DB_OK;
}

int file_io_write_page(file_io_t* io, page_id_t page_id, const void* buf) {
    if (!io->fp)
        return DB_FILE_NOT_OPEN;
    if (page_id < 0)
        return DB_PAGE_NOT_FOUND;
    long offset = (long)page_id * PAGE_SIZE;
    if (fseek(io->fp, offset, SEEK_SET) != 0)
        return DB_IO_ERROR;
    size_t written = fwrite(buf, PAGE_SIZE, 1, io->fp);
    if (written != 1)
        return DB_IO_ERROR;
    return DB_OK;
}

void file_io_flush(file_io_t* io) {
    if (io->fp)
        fflush(io->fp);
}
