#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/config.h"
#include "src/parser/ast.h"
#include "src/storage/schema.h"

/* Logical operator types */
typedef enum {
    LOP_SCAN,
    LOP_INDEX_SCAN,
    LOP_FILTER,
    LOP_PROJECT,
    LOP_JOIN,
    LOP_AGGREGATE,
    LOP_SORT,
    LOP_LIMIT,
    LOP_INSERT,
    LOP_UPDATE,
    LOP_DELETE,
} logical_op_t;

/* Logical plan node */
typedef struct logical_node {
    logical_op_t op;
    struct logical_node** children;
    int child_count;
    int child_capacity;
    schema_t* output_schema;

    union {
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id; } scan;
        struct { index_id_t index_id; char table_name[MAX_TABLE_NAME]; table_id_t table_id;
                 column_id_t column_id; int64_t low_key; int64_t high_key;
                 int has_low; int has_high; } index_scan;
        struct { expr_t* predicate; } filter;
        struct { expr_t** expressions; int expr_count; } project;
        struct { token_type_t join_type; expr_t* condition; } join;
        struct { expr_t** group_by; int group_count; expr_t** aggregates; int agg_count; } aggregate;
        struct { order_by_item_t* items; int count; } sort;
        struct { int limit; int offset; } limit_node;
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id; } insert;
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id;
                 assignment_t* assignments; int assign_count; expr_t* where; } update;
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id; expr_t* where; } delete_node;
    };
} logical_node_t;

logical_node_t* logical_node_create(logical_op_t op);
void            logical_node_destroy(logical_node_t* node);
int             logical_node_add_child(logical_node_t* parent, logical_node_t* child);
void            logical_node_print(const logical_node_t* node, int indent);
