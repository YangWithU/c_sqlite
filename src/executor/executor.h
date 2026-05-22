#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/storage/schema.h"
#include "src/storage/tuple.h"
#include "src/catalog/catalog.h"
#include "src/buffer/buffer_pool_manager.h"

/* Forward declaration for transaction (Phase 8) */
typedef struct transaction transaction_t;
typedef struct lock_manager lock_manager_t;

/* Execution context — shared by all executors in a query */
typedef struct {
    catalog_t*             catalog;
    buffer_pool_manager_t* bpm;
    transaction_t*         txn;       /* NULL = auto-commit mode */
    lock_manager_t*        lock_mgr;  /* NULL = no locking */
} executor_context_t;

/* Volcano-model executor: init/next/close with function pointer vtable */
typedef struct executor executor_t;

struct executor {
    int   (*init)(executor_t* self);
    int   (*next)(executor_t* self, tuple_t* out);
    void  (*close)(executor_t* self);
    void  (*destroy)(executor_t* self);
    executor_context_t* exec_ctx;
    executor_t** children;
    int    child_count;
    int    child_capacity;
    schema_t* output_schema;
    void*     state;        /* executor-specific state */
};

/* Base helpers */
int  executor_add_child(executor_t* parent, executor_t* child);
void executor_destroy_tree(executor_t* exec);
