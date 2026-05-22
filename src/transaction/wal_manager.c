#include "src/transaction/wal_manager.h"
#include "src/common/mem.h"
#include <string.h>
#include <stdio.h>

/* strdup not available in strict C11 — implement manually */
static char* my_strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* d = db_malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

int wal_manager_init(wal_manager_t* wm, const char* log_path) {
    if (!wm || !log_path) return DB_INVALID_ARGUMENT;

    wm->log_path = my_strdup(log_path);
    wm->current_lsn = 1;
    wm->flushed_lsn = 0;

    wm->log_file = fopen(log_path, "wb+");
    if (!wm->log_file) {
        db_free(wm->log_path);
        return DB_IO_ERROR;
    }

    return DB_OK;
}

void wal_manager_destroy(wal_manager_t* wm) {
    if (wm) {
        if (wm->log_file) fclose(wm->log_file);
        if (wm->log_path) db_free(wm->log_path);
    }
}

void wal_record_destroy(wal_record_t* rec) {
    if (!rec) return;
    if (rec->before_image) { db_free(rec->before_image); rec->before_image = NULL; }
    if (rec->after_image)  { db_free(rec->after_image);  rec->after_image = NULL; }
}

lsn_t wal_append(wal_manager_t* wm, const wal_record_t* record) {
    if (!wm || !wm->log_file) return INVALID_LSN;

    lsn_t lsn = wm->current_lsn++;

    /* Write: [LSN(4)][TxnID(4)][PrevLSN(4)][Type(4)][PageID(4)][Offset(2)]
     *        [BeforeSize(2)][AfterSize(2)][BeforeImage][AfterImage] */
    int32_t buf[6];
    buf[0] = lsn;
    buf[1] = record->txn_id;
    buf[2] = record->prev_lsn;
    buf[3] = (int32_t)record->type;
    buf[4] = record->page_id;
    buf[5] = (int32_t)record->offset;

    fwrite(buf, sizeof(int32_t), 6, wm->log_file);

    int16_t sizes[2];
    sizes[0] = record->before_size;
    sizes[1] = record->after_size;
    fwrite(sizes, sizeof(int16_t), 2, wm->log_file);

    if (record->before_size > 0 && record->before_image)
        fwrite(record->before_image, 1, record->before_size, wm->log_file);
    if (record->after_size > 0 && record->after_image)
        fwrite(record->after_image, 1, record->after_size, wm->log_file);

    return lsn;
}

int wal_flush(wal_manager_t* wm, lsn_t up_to_lsn) {
    if (!wm || !wm->log_file) return DB_INVALID_ARGUMENT;
    fflush(wm->log_file);
    wm->flushed_lsn = up_to_lsn > wm->flushed_lsn ? up_to_lsn : wm->flushed_lsn;
    return DB_OK;
}

void wal_reset_read(wal_manager_t* wm) {
    if (wm && wm->log_file) {
        rewind(wm->log_file);
    }
}

int wal_read_next(wal_manager_t* wm, wal_record_t* out) {
    if (!wm || !wm->log_file || !out) return DB_INVALID_ARGUMENT;

    memset(out, 0, sizeof(wal_record_t));

    int32_t buf[6];
    size_t nread = fread(buf, sizeof(int32_t), 6, wm->log_file);
    if (nread < 6) return DB_PAGE_NOT_FOUND;

    out->lsn = buf[0];
    out->txn_id = buf[1];
    out->prev_lsn = buf[2];
    out->type = (wal_record_type_t)buf[3];
    out->page_id = buf[4];
    out->offset = (int16_t)buf[5];

    int16_t sizes[2];
    nread = fread(sizes, sizeof(int16_t), 2, wm->log_file);
    if (nread < 2) return DB_IO_ERROR;

    out->before_size = sizes[0];
    out->after_size = sizes[1];

    if (out->before_size > 0) {
        out->before_image = db_malloc(out->before_size);
        fread(out->before_image, 1, out->before_size, wm->log_file);
    }
    if (out->after_size > 0) {
        out->after_image = db_malloc(out->after_size);
        fread(out->after_image, 1, out->after_size, wm->log_file);
    }

    return DB_OK;
}

lsn_t wal_current_lsn(const wal_manager_t* wm) {
    return wm ? wm->current_lsn : INVALID_LSN;
}
