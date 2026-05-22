#include "src/planner/logical_plan.h"
#include "src/common/mem.h"

#include <stdio.h>

logical_node_t* logical_node_create(logical_op_t op)
{
    logical_node_t* node = db_calloc(1, sizeof(logical_node_t));
    if (!node) {
        return NULL;
    }

    node->op = op;
    node->child_capacity = 2;
    node->children = db_calloc(node->child_capacity, sizeof(logical_node_t*));
    if (!node->children) {
        db_free(node);
        return NULL;
    }

    node->child_count = 0;
    node->output_schema = NULL;

    return node;
}

void logical_node_destroy(logical_node_t* node)
{
    if (!node) {
        return;
    }

    /* Recursively destroy all children */
    for (int i = 0; i < node->child_count; i++) {
        logical_node_destroy(node->children[i]);
    }
    db_free(node->children);

    /* Free the node itself.
     * Do NOT free output_schema (may be shared or borrowed).
     * Do NOT free any expr_t* or assignment_t* pointers (borrowed from AST). */
    db_free(node);
}

int logical_node_add_child(logical_node_t* parent, logical_node_t* child)
{
    if (!parent || !child) {
        return DB_INVALID_ARGUMENT;
    }

    /* Grow the children array if needed */
    if (parent->child_count == parent->child_capacity) {
        int new_capacity = parent->child_capacity * 2;
        logical_node_t** new_children = db_realloc(
            parent->children, new_capacity * sizeof(logical_node_t*));
        if (!new_children) {
            return DB_OUT_OF_MEMORY;
        }
        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }

    parent->children[parent->child_count++] = child;
    return DB_OK;
}

void logical_node_print(const logical_node_t* node, int indent)
{
    if (!node) {
        return;
    }

    /* Print indentation */
    for (int i = 0; i < indent; i++) {
        printf(" ");
    }

    /* Print operator-specific info */
    switch (node->op) {
    case LOP_SCAN:
        printf("SCAN(table=%s, id=%d)\n",
               node->scan.table_name, node->scan.table_id);
        break;
    case LOP_INDEX_SCAN:
        printf("INDEX_SCAN(index=%d, table=%s)\n",
               node->index_scan.index_id, node->index_scan.table_name);
        break;
    case LOP_FILTER:
        printf("FILTER\n");
        break;
    case LOP_PROJECT:
        printf("PROJECT(cols=%d)\n", node->project.expr_count);
        break;
    case LOP_JOIN:
        printf("JOIN(type=%d)\n", node->join.join_type);
        break;
    case LOP_AGGREGATE:
        printf("AGGREGATE(groups=%d, aggs=%d)\n",
               node->aggregate.group_count, node->aggregate.agg_count);
        break;
    case LOP_SORT:
        printf("SORT(keys=%d)\n", node->sort.count);
        break;
    case LOP_LIMIT:
        printf("LIMIT(n=%d, offset=%d)\n",
               node->limit_node.limit, node->limit_node.offset);
        break;
    case LOP_INSERT:
        printf("INSERT(table=%s)\n", node->insert.table_name);
        break;
    case LOP_UPDATE:
        printf("UPDATE(table=%s)\n", node->update.table_name);
        break;
    case LOP_DELETE:
        printf("DELETE(table=%s)\n", node->delete_node.table_name);
        break;
    }

    /* Recursively print children with increased indent */
    for (int i = 0; i < node->child_count; i++) {
        logical_node_print(node->children[i], indent + 2);
    }
}
