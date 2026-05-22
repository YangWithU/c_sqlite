#include "src/executor/executor.h"
#include "src/common/mem.h"

int executor_add_child(executor_t* parent, executor_t* child) {
    if (!parent || !child) return DB_INVALID_ARGUMENT;
    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity ? parent->child_capacity * 2 : 4;
        executor_t** new_arr = db_realloc(parent->children,
                                          new_cap * sizeof(executor_t*));
        if (!new_arr) return DB_OUT_OF_MEMORY;
        parent->children = new_arr;
        parent->child_capacity = new_cap;
    }
    parent->children[parent->child_count++] = child;
    return DB_OK;
}

void executor_destroy_tree(executor_t* exec) {
    if (!exec) return;
    if (exec->children) {
        for (int i = 0; i < exec->child_count; i++) {
            executor_destroy_tree(exec->children[i]);
        }
        db_free(exec->children);
    }
    if (exec->destroy) {
        exec->destroy(exec);
    }
}
