#pragma once

#include "error.h"
#include <stdint.h>

/* Generic Result wrapper (optional, for cases where out-param is awkward) */
typedef struct {
    error_code_t error;
    union {
        int64_t int_val;
        void*   ptr_val;
    };
} result_t;

static inline result_t result_ok_int(int64_t val) {
    result_t r = { DB_OK, .int_val = val };
    return r;
}

static inline result_t result_ok_ptr(void* ptr) {
    result_t r = { DB_OK, .ptr_val = ptr };
    return r;
}

static inline result_t result_err(error_code_t code) {
    result_t r = { code, .int_val = 0 };
    return r;
}

static inline int result_is_ok(const result_t* r) {
    return r->error == DB_OK;
}
