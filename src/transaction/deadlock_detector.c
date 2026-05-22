#include "src/transaction/deadlock_detector.h"
#include "src/common/mem.h"
#include "src/common/error.h"
#include <string.h>

static size_t int_hash_fn(const void* key) {
    uintptr_t v = (uintptr_t)key;
    return (size_t)(v * 2654435761UL);
}
static int int_equal_fn(const void* a, const void* b) {
    return (uintptr_t)a == (uintptr_t)b;
}

int deadlock_detector_init(deadlock_detector_t* dd) {
    if (!dd) return DB_INVALID_ARGUMENT;
    hashmap_init(&dd->waits_for, 16, int_hash_fn, int_equal_fn);
    return DB_OK;
}

void deadlock_detector_destroy(deadlock_detector_t* dd) {
    if (dd) hashmap_destroy(&dd->waits_for, NULL, free);
}

void deadlock_add_edge(deadlock_detector_t* dd, txn_id_t txn_a, txn_id_t txn_b) {
    if (!dd) return;
    txn_id_t* existing = (txn_id_t*)hashmap_get(&dd->waits_for, (void*)(uintptr_t)txn_a);
    if (!existing) {
        txn_id_t* val = db_malloc(sizeof(txn_id_t));
        *val = txn_b;
        hashmap_put(&dd->waits_for, (void*)(uintptr_t)txn_a, val);
    } else {
        *existing = txn_b;
    }
}

void deadlock_remove_edges(deadlock_detector_t* dd, txn_id_t txn) {
    if (!dd) return;
    hashmap_remove(&dd->waits_for, (void*)(uintptr_t)txn, NULL, free);
}

/* DFS cycle detection using iterator */
txn_id_t deadlock_detect(deadlock_detector_t* dd) {
    if (!dd) return INVALID_TXN_ID;

    /* Iterate all entries using hashmap iterator */
    hashmap_iter_t it;
    hashmap_iter_init(&it, &dd->waits_for);

    void* key;
    void* value;
    while (hashmap_iter_next(&it, &key, &value) != 0) {
        txn_id_t start = (txn_id_t)(uintptr_t)key;

        /* DFS from start */
        int stack_cap = 32;
        txn_id_t* stack = db_malloc(sizeof(txn_id_t) * stack_cap);
        int stack_size = 0;
        txn_id_t current = start;

        while (current != INVALID_TXN_ID) {
            /* Check for cycle */
            for (int i = 0; i < stack_size; i++) {
                if (stack[i] == current) {
                    db_free(stack);
                    return current;  /* victim */
                }
            }

            if (stack_size >= stack_cap) {
                stack_cap *= 2;
                stack = db_realloc(stack, sizeof(txn_id_t) * stack_cap);
            }
            stack[stack_size++] = current;

            txn_id_t* next = (txn_id_t*)hashmap_get(&dd->waits_for, (void*)(uintptr_t)current);
            current = next ? *next : INVALID_TXN_ID;
        }

        db_free(stack);
    }

    return INVALID_TXN_ID;
}
