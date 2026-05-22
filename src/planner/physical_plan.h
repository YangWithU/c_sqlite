#pragma once

#include "src/common/types.h"
#include "src/common/error.h"
#include "src/common/config.h"
#include "src/parser/ast.h"
#include "src/storage/schema.h"

/* Physical operator types */
typedef enum {
    POP_SEQ_SCAN,
    POP_INDEX_SCAN,
    POP_FILTER,
    POP_PROJECT,
    POP_NESTED_LOOP_JOIN,
    POP_HASH_JOIN,
    POP_HASH_AGGREGATE,
    POP_SORT,
    POP_LIMIT,
    POP_INSERT,
    POP_UPDATE,
    POP_DELETE,
} physical_op_t;

/* Physical plan node */
typedef struct physical_node {
    physical_op_t op;
    struct physical_node** children;
    int child_count;
    int child_capacity;
    schema_t* output_schema;

    union {
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id; expr_t* predicate; } seq_scan;
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id;
                 index_id_t index_id; column_id_t column_id;
                 int64_t low_key; int64_t high_key; int has_low; int has_high; } index_scan;
        struct { expr_t* predicate; } filter;
        struct { expr_t** expressions; int expr_count; } project;
        struct { token_type_t join_type; expr_t* condition; } nl_join;
        struct { token_type_t join_type; expr_t* left_key; expr_t* right_key; } hash_join;
        struct { expr_t** group_by; int group_count; expr_t** aggregates; int agg_count; } hash_agg;
        struct { order_by_item_t* items; int count; } sort;
        struct { int limit; int offset; } limit_node;
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id; } insert;
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id;
                 assignment_t* assignments; int assign_count; } update;
        struct { char table_name[MAX_TABLE_NAME]; table_id_t table_id; } delete_node;
    };
} physical_node_t;

physical_node_t* physical_node_create(physical_op_t op);
void             physical_node_destroy(physical_node_t* node);
int              physical_node_add_child(physical_node_t* parent, physical_node_t* child);
void             physical_node_print(const physical_node_t* node, int indent);
