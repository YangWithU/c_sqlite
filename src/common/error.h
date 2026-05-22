#pragma once

typedef enum {
    DB_OK = 0,

    /* I/O errors (1001+) */
    DB_IO_ERROR = 1001,
    DB_PAGE_NOT_FOUND,
    DB_BUFFER_POOL_FULL,
    DB_FILE_NOT_OPEN,

    /* Transaction errors (2001+) */
    DB_TXN_ABORTED = 2001,
    DB_DEADLOCK,
    DB_LOCK_CONFLICT,

    /* SQL errors (3001+) */
    DB_SYNTAX_ERROR = 3001,
    DB_UNKNOWN_TABLE,
    DB_UNKNOWN_COLUMN,
    DB_DUPLICATE_KEY,
    DB_NULL_VIOLATION,
    DB_TYPE_MISMATCH,
    DB_INVALID_ARGUMENT,

    /* Memory errors (4001+) */
    DB_OUT_OF_MEMORY = 4001,

    /* Internal errors (9001+) */
    DB_INTERNAL_ERROR = 9001,
} error_code_t;

const char* error_code_to_string(error_code_t code);
