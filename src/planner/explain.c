#include "src/planner/explain.h"
#include <stdio.h>

static void explain_print_node(const physical_node_t* node, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; i++) printf("  ");

    switch (node->op) {
    case POP_SEQ_SCAN:
        if (node->seq_scan.predicate)
            printf("SeqScan(table=%s, filtered)\n", node->seq_scan.table_name);
        else
            printf("SeqScan(table=%s)\n", node->seq_scan.table_name);
        break;
    case POP_INDEX_SCAN:
        printf("IndexScan(index=%d, table=%s)\n",
               node->index_scan.index_id, node->index_scan.table_name);
        break;
    case POP_FILTER:
        printf("Filter\n");
        break;
    case POP_PROJECT:
        printf("Project(cols=%d)\n", node->project.expr_count);
        break;
    case POP_NESTED_LOOP_JOIN:
        printf("NestedLoopJoin\n");
        break;
    case POP_HASH_JOIN:
        printf("HashJoin\n");
        break;
    case POP_HASH_AGGREGATE:
        printf("HashAggregate(groups=%d, aggs=%d)\n",
               node->hash_agg.group_count, node->hash_agg.agg_count);
        break;
    case POP_SORT:
        printf("Sort(keys=%d)\n", node->sort.count);
        break;
    case POP_LIMIT:
        printf("Limit(n=%d, offset=%d)\n", node->limit_node.limit, node->limit_node.offset);
        break;
    case POP_INSERT:
        printf("Insert(table=%s)\n", node->insert.table_name);
        break;
    case POP_UPDATE:
        printf("Update(table=%s)\n", node->update.table_name);
        break;
    case POP_DELETE:
        printf("Delete(table=%s)\n", node->delete_node.table_name);
        break;
    }

    for (int i = 0; i < node->child_count; i++)
        explain_print_node(node->children[i], indent + 1);
}

void explain_print_plan(const physical_node_t* plan) {
    printf("== Query Plan ==\n");
    explain_print_node(plan, 0);
    printf("================\n");
}
