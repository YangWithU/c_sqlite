#include "src/transaction/lock_manager.h"
#include "src/common/mem.h"
#include <string.h>

/* Integer hash/equal for hashmap (key = (void*)(uintptr_t)int_id) */
static size_t int_hash_fn(const void* key) {
    uintptr_t v = (uintptr_t)key;
    return (size_t)(v * 2654435761UL);
}
static int int_equal_fn(const void* a, const void* b) {
    return (uintptr_t)a == (uintptr_t)b;
}

/* Compatibility matrix: 1 = compatible, 0 = conflict
 * Order matches lock_mode_t: S=0, X=1, IS=2, IX=3 */
static const int compat_matrix[4][4] = {
    /*              S     X     IS    IX   */
    /* S  */ { 1,    0,    1,    0  },
    /* X  */ { 0,    0,    0,    0  },
    /* IS */ { 1,    0,    1,    1  },
    /* IX */ { 0,    0,    1,    1  },
};

int lock_compatible(lock_mode_t a, lock_mode_t b) {
    return compat_matrix[a][b];
}

static lock_queue_t* find_or_create_queue(lock_manager_t* lm, int resource_id) {
    lock_queue_t* queue = (lock_queue_t*)hashmap_get(&lm->lock_table,
                                                       (void*)(uintptr_t)resource_id);
    if (!queue) {
        queue = db_calloc(1, sizeof(lock_queue_t));
        if (!queue) return NULL;
        hashmap_put(&lm->lock_table, (void*)(uintptr_t)resource_id, queue);
    }
    return queue;
}

int lock_manager_init(lock_manager_t* lm) {
    if (!lm) return DB_INVALID_ARGUMENT;
    hashmap_init(&lm->lock_table, 64, int_hash_fn, int_equal_fn);
    return DB_OK;
}

static void free_queue(void* val) {
    lock_queue_t* queue = (lock_queue_t*)val;
    if (queue) {
        if (queue->requests) db_free(queue->requests);
        db_free(queue);
    }
}

void lock_manager_destroy(lock_manager_t* lm) {
    if (!lm) return;
    hashmap_destroy(&lm->lock_table, NULL, free_queue);
}

int lock_acquire(lock_manager_t* lm, txn_id_t txn_id, int resource_id,
                 lock_mode_t mode) {
    if (!lm) return DB_INVALID_ARGUMENT;

    lock_queue_t* queue = find_or_create_queue(lm, resource_id);
    if (!queue) return DB_OUT_OF_MEMORY;

    /* Check if this txn already has a request on this resource */
    for (int i = 0; i < queue->request_count; i++) {
        if (queue->requests[i].txn_id == txn_id) {
            if (queue->requests[i].granted) {
                /* Already granted — upgrade if needed */
                if (mode > queue->requests[i].mode) {
                    queue->requests[i].mode = mode;
                }
                return DB_OK;
            } else {
                /* Waiting request — re-evaluate compatibility */
                queue->requests[i].mode = mode;
                int compatible = 1;
                for (int j = 0; j < queue->request_count; j++) {
                    if (j != i && queue->requests[j].granted &&
                        !lock_compatible(mode, queue->requests[j].mode)) {
                        compatible = 0;
                        break;
                    }
                }
                if (compatible) {
                    queue->requests[i].granted = 1;
                    return DB_OK;
                }
                return DB_LOCK_CONFLICT;
            }
        }
    }

    /* Check compatibility with all currently granted locks */
    int compatible = 1;
    for (int i = 0; i < queue->request_count; i++) {
        if (queue->requests[i].granted &&
            !lock_compatible(mode, queue->requests[i].mode)) {
            compatible = 0;
            break;
        }
    }

    /* Add request to queue */
    if (queue->request_count >= queue->request_capacity) {
        queue->request_capacity = queue->request_capacity ? queue->request_capacity * 2 : 4;
        queue->requests = db_realloc(queue->requests,
                                      queue->request_capacity * sizeof(lock_request_t));
    }

    lock_request_t* req = &queue->requests[queue->request_count++];
    req->txn_id = txn_id;
    req->mode = mode;
    req->granted = compatible;

    return compatible ? DB_OK : DB_LOCK_CONFLICT;
}

int lock_release_all(lock_manager_t* lm, txn_id_t txn_id) {
    if (!lm) return DB_INVALID_ARGUMENT;

    /* Collect all resource IDs first to avoid modifying during iteration */
    int res_count = 0;
    int res_cap = 16;
    int* res_ids = db_malloc(sizeof(int) * res_cap);

    hashmap_iter_t it;
    hashmap_iter_init(&it, &lm->lock_table);
    void* key;
    void* value;
    while (hashmap_iter_next(&it, &key, &value) != 0) {
        if (res_count >= res_cap) {
            res_cap *= 2;
            res_ids = db_realloc(res_ids, sizeof(int) * res_cap);
        }
        res_ids[res_count++] = (int)(uintptr_t)key;
    }

    /* Now process each queue */
    for (int r = 0; r < res_count; r++) {
        lock_queue_t* queue = (lock_queue_t*)hashmap_get(&lm->lock_table,
                                                           (void*)(uintptr_t)res_ids[r]);
        if (!queue) continue;

        int changed = 0;
        for (int j = 0; j < queue->request_count; j++) {
            if (queue->requests[j].txn_id == txn_id) {
                /* Remove by shifting */
                memmove(&queue->requests[j], &queue->requests[j + 1],
                        (queue->request_count - j - 1) * sizeof(lock_request_t));
                queue->request_count--;
                j--;
                changed = 1;
            }
        }

        /* After releasing, try to grant waiting requests */
        if (changed) {
            for (int k = 0; k < queue->request_count; k++) {
                if (queue->requests[k].granted) continue;
                int compat = 1;
                for (int m = 0; m < queue->request_count; m++) {
                    if (m != k && queue->requests[m].granted &&
                        !lock_compatible(queue->requests[k].mode,
                                          queue->requests[m].mode)) {
                        compat = 0;
                        break;
                    }
                }
                if (compat) queue->requests[k].granted = 1;
            }
        }
    }

    db_free(res_ids);
    return DB_OK;
}

int lock_has_lock(lock_manager_t* lm, txn_id_t txn_id, int resource_id) {
    if (!lm) return 0;
    lock_queue_t* queue = (lock_queue_t*)hashmap_get(&lm->lock_table,
                                                       (void*)(uintptr_t)resource_id);
    if (!queue) return 0;
    for (int i = 0; i < queue->request_count; i++) {
        if (queue->requests[i].txn_id == txn_id && queue->requests[i].granted)
            return 1;
    }
    return 0;
}
