#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/hashmap.h"
#include "src/common/list.h"
#include <stdbool.h>

/* Lock modes */
typedef enum {
    LOCK_SHARED,
    LOCK_EXCLUSIVE,
    LOCK_INTENT_SHARED,
    LOCK_INTENT_EXCLUSIVE,
} lock_mode_t;

/* Lock request — queued entry for a resource */
typedef struct lock_request {
    txn_id_t    txn_id;
    lock_mode_t mode;
    int         granted;
} lock_request_t;

/* Lock queue — one per resource (identified by resource_id) */
typedef struct {
    int             request_count;
    int             request_capacity;
    lock_request_t* requests;
} lock_queue_t;

/* Lock manager */
typedef struct lock_manager {
    hashmap_t lock_table;  /* resource_id (void* key) -> lock_queue_t* */
} lock_manager_t;

/* Initialize/destroy */
int  lock_manager_init(lock_manager_t* lm);
void lock_manager_destroy(lock_manager_t* lm);

/* Try to acquire a lock. Returns DB_OK if granted, DB_LOCK_CONFLICT if must wait. */
int  lock_acquire(lock_manager_t* lm, txn_id_t txn_id, int resource_id,
                  lock_mode_t mode);

/* Release all locks held by a transaction. */
int  lock_release_all(lock_manager_t* lm, txn_id_t txn_id);

/* Check whether a transaction holds a lock on a resource. */
int  lock_has_lock(lock_manager_t* lm, txn_id_t txn_id, int resource_id);

/* Check lock compatibility between two modes. */
int  lock_compatible(lock_mode_t a, lock_mode_t b);
