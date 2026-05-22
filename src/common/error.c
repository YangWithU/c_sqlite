#include "error.h"

static const struct {
    error_code_t code;
    const char*  msg;
} error_table[] = {
    { DB_OK,               "OK" },
    { DB_IO_ERROR,         "I/O error" },
    { DB_PAGE_NOT_FOUND,   "Page not found" },
    { DB_BUFFER_POOL_FULL, "Buffer pool full" },
    { DB_FILE_NOT_OPEN,    "File not open" },
    { DB_TXN_ABORTED,      "Transaction aborted" },
    { DB_DEADLOCK,         "Deadlock detected" },
    { DB_LOCK_CONFLICT,    "Lock conflict" },
    { DB_SYNTAX_ERROR,     "Syntax error" },
    { DB_UNKNOWN_TABLE,    "Unknown table" },
    { DB_UNKNOWN_COLUMN,   "Unknown column" },
    { DB_DUPLICATE_KEY,    "Duplicate key" },
    { DB_NULL_VIOLATION,   "NULL violation" },
    { DB_TYPE_MISMATCH,    "Type mismatch" },
    { DB_INVALID_ARGUMENT, "Invalid argument" },
    { DB_OUT_OF_MEMORY,    "Out of memory" },
    { DB_INTERNAL_ERROR,   "Internal error" },
};

const char* error_code_to_string(error_code_t code) {
    for (int i = 0; i < (int)(sizeof(error_table) / sizeof(error_table[0])); i++) {
        if (error_table[i].code == code)
            return error_table[i].msg;
    }
    return "Unknown error";
}
