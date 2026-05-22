#include "src/planner/physical_plan.h"
#include "src/common/mem.h"
#include <stdio.h>

/* ---- Create ---- */

physical_node_t* physical_node_create(physical_op_t op)
{
    physical_node_t* node = db_calloc(1, sizeof(physical_node_t));
    if (!node) return NULL;

    node->op = op;
    node->child_count = 0;
    node->child_capacity = 2;

    node->children = db_calloc(node->child_capacity, sizeof(physical_node_t*));
    if (!node->children) {
        db_free(node);
        return NULL;
    }

    node->output_schema = NULL;
    return node;
}

/* ---- Destroy ---- */

void physical_node_destroy(physical_node_t* node)
{
    if (!node) return;

    /* Recursively destroy children */
    for (int i = 0; i < node->child_count; i++) {
        physical_node_destroy(node->children[i]);
    }
    db_free(node->children);

    /* Do NOT free output_schema or expr_t* pointers --
     * they are owned elsewhere (catalog / AST). */

    db_free(node);
}

/* ---- Add child ---- */

int physical_node_add_child(physical_node_t* parent, physical_node_t* child)
{
    if (!parent || !child) return DB_INVALID_ARGUMENT;

    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity * 2;
        physical_node_t** new_arr =
            db_realloc(parent->children, new_cap * sizeof(physical_node_t*));
        if (!new_arr) return DB_OUT_OF_MEMORY;

        parent->children = new_arr;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
    return DB_OK;
}

/* ---- Print ---- */

static void print_indent(int indent)
{
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

void physical_node_print(const physical_node_t* node, int indent)
{
    if (!node) return;

    print_indent(indent);

    switch (node->op) {
    case POP_SEQ_SCAN:
        if (node->seq_scan.predicate) {
            printf("SEQ_SCAN(table=%s, id=%d, filtered)\n",
                   node->seq_scan.table_name, node->seq_scan.table_id);
        } else {
            printf("SEQ_SCAN(table=%s, id=%d)\n",
                   node->seq_scan.table_name, node->seq_scan.table_id);
        }
        break;

    case POP_INDEX_SCAN:
        printf("INDEX_SCAN(index=%d, table=%s)\n",
               node->index_scan.index_id, node->index_scan.table_name);
        break;

    case POP_FILTER:
        printf("FILTER\n");
        break;

    case POP_PROJECT:
        printf("PROJECT(cols=%d)\n", node->project.expr_count);
        break;

    case POP_NESTED_LOOP_JOIN:
        printf("NESTED_LOOP_JOIN(type=%d)\n", node->nl_join.join_type);
        break;

    case POP_HASH_JOIN:
        printf("HASH_JOIN(type=%d)\n", node->hash_join.join_type);
        break;

    case POP_HASH_AGGREGATE:
        printf("HASH_AGGREGATE(groups=%d, aggs=%d)\n",
               node->hash_agg.group_count, node->hash_agg.agg_count);
        break;

    case POP_SORT:
        printf("SORT(keys=%d)\n", node->sort.count);
        break;

    case POP_LIMIT:
        printf("LIMIT(n=%d, offset=%d)\n",
               node->limit_node.limit, node->limit_node.offset);
        break;

    case POP_INSERT:
        printf("INSERT(table=%s)\n", node->insert.table_name);
        break;

    case POP_UPDATE:
        printf("UPDATE(table=%s)\n", node->update.table_name);
        break;

    case POP_DELETE:
        printf("DELETE(table=%s)\n", node->delete_node.table_name);
        break;
    }

    for (int i = 0; i < node->child_count; i++) {
        physical_node_print(node->children[i], indent + 1);
    }
}
