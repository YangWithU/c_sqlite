#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* WAL record types */
typedef enum {
    WAL_BEGIN,
    WAL_COMMIT,
    WAL_ABORT,
    WAL_UPDATE,
    WAL_DELETE,
    WAL_INSERT,
    WAL_CHECKPOINT,
    WAL_CLR,     /* compensation log record (undo of an update) */
} wal_record_type_t;

/* WAL log record */
typedef struct {
    lsn_t           lsn;
    txn_id_t        txn_id;
    lsn_t           prev_lsn;
    wal_record_type_t type;
    uint32_t        length;
    page_id_t       page_id;
    int16_t         offset;     /* slot_id for update/delete/insert */
    char*           before_image;  /* old data (for undo) */
    char*           after_image;   /* new data (for redo) */
    uint16_t        before_size;
    uint16_t        after_size;
} wal_record_t;

/* WAL manager */
typedef struct {
    char*        log_path;
    FILE*        log_file;
    lsn_t        current_lsn;
    lsn_t        flushed_lsn;
} wal_manager_t;

/* Initialize WAL manager with the given log file path */
int  wal_manager_init(wal_manager_t* wm, const char* log_path);

/* Destroy WAL manager, closing the log file */
void wal_manager_destroy(wal_manager_t* wm);

/* Append a log record. Returns the LSN of the new record. */
lsn_t wal_append(wal_manager_t* wm, const wal_record_t* record);

/* Flush log up to the given LSN. */
int  wal_flush(wal_manager_t* wm, lsn_t up_to_lsn);

/* Read the next log record from the log file. Returns DB_OK or DB_PAGE_NOT_FOUND at EOF. */
int  wal_read_next(wal_manager_t* wm, wal_record_t* out);

/* Reset the read position to the beginning of the log. */
void wal_reset_read(wal_manager_t* wm);

/* Get the current LSN. */
lsn_t wal_current_lsn(const wal_manager_t* wm);

/* Destroy a log record's heap-allocated fields. */
void wal_record_destroy(wal_record_t* rec);
