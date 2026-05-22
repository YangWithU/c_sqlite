#include "src/planner/planner.h"
#include "src/planner/value_bridge.h"
#include "src/common/mem.h"
#include "src/common/logger.h"
#include "src/storage/schema.h"
#include <string.h>
#include <stdio.h>

/* ---- Helpers ---- */

static schema_t* get_table_schema(planner_t* p, table_id_t table_id) {
    schema_t* s = db_malloc(sizeof(schema_t));
    if (!s) return NULL;
    if (catalog_get_schema(p->catalog, table_id, s) != DB_OK) {
        db_free(s);
        return NULL;
    }
    return s;
}

static schema_t* clone_schema(const schema_t* src) {
    if (!src) return NULL;
    schema_t* s = db_malloc(sizeof(schema_t));
    if (!s) return NULL;
    schema_create(s, src->columns, src->num_columns);
    return s;
}

/* ---- Logical plan generation ---- */

static logical_node_t* plan_select(planner_t* p, const stmt_t* stmt) {
    const expr_t* where = stmt->select.where;
    int from_count = stmt->select.from_count;
    table_ref_t* from_tables = stmt->select.from_tables;
    join_clause_t* joins = stmt->select.joins;
    int join_count = stmt->select.join_count;
    expr_t** group_by = stmt->select.group_by;
    int group_count = stmt->select.group_count;
    expr_t* having = stmt->select.having;
    order_by_item_t* order_by = stmt->select.order_by;
    int order_count = stmt->select.order_count;
    expr_t** select_list = stmt->select.select_list;
    int select_count = stmt->select.select_count;
    int limit = stmt->select.limit;
    int offset = stmt->select.offset;

    logical_node_t* current = NULL;

    if (from_count == 0) {
        current = logical_node_create(LOP_SCAN);
        current->scan.table_id = INVALID_TABLE_ID;
    } else {
        table_meta_t meta;
        if (catalog_get_table(p->catalog, from_tables[0].name, &meta) != DB_OK) {
            LOG_ERROR("Table '%s' not found", from_tables[0].name);
            return NULL;
        }
        current = logical_node_create(LOP_SCAN);
        strncpy(current->scan.table_name, from_tables[0].name, MAX_TABLE_NAME - 1);
        current->scan.table_id = meta.table_id;
        current->output_schema = get_table_schema(p, meta.table_id);

        for (int i = 0; i < join_count; i++) {
            logical_node_t* join_node = logical_node_create(LOP_JOIN);
            join_node->join.join_type = joins[i].join_type;
            join_node->join.condition = joins[i].condition;

            table_meta_t right_meta;
            if (catalog_get_table(p->catalog, joins[i].table.name, &right_meta) != DB_OK) {
                LOG_ERROR("Join table '%s' not found", joins[i].table.name);
                logical_node_destroy(join_node);
                logical_node_destroy(current);
                return NULL;
            }
            logical_node_t* right_scan = logical_node_create(LOP_SCAN);
            strncpy(right_scan->scan.table_name, joins[i].table.name, MAX_TABLE_NAME - 1);
            right_scan->scan.table_id = right_meta.table_id;
            right_scan->output_schema = get_table_schema(p, right_meta.table_id);

            logical_node_add_child(join_node, current);
            logical_node_add_child(join_node, right_scan);

            if (current->output_schema && right_scan->output_schema) {
                int lc = current->output_schema->num_columns;
                int rc = right_scan->output_schema->num_columns;
                column_def_t* cols = db_calloc(lc + rc, sizeof(column_def_t));
                for (int c = 0; c < lc; c++) cols[c] = current->output_schema->columns[c];
                for (int c = 0; c < rc; c++) cols[lc + c] = right_scan->output_schema->columns[c];
                join_node->output_schema = db_malloc(sizeof(schema_t));
                schema_create(join_node->output_schema, cols, lc + rc);
                db_free(cols);
            }
            current = join_node;
        }

        for (int i = 1; i < from_count; i++) {
            table_meta_t meta2;
            if (catalog_get_table(p->catalog, from_tables[i].name, &meta2) != DB_OK) {
                LOG_ERROR("Table '%s' not found", from_tables[i].name);
                logical_node_destroy(current);
                return NULL;
            }
            logical_node_t* right_scan = logical_node_create(LOP_SCAN);
            strncpy(right_scan->scan.table_name, from_tables[i].name, MAX_TABLE_NAME - 1);
            right_scan->scan.table_id = meta2.table_id;
            right_scan->output_schema = get_table_schema(p, meta2.table_id);

            logical_node_t* cross = logical_node_create(LOP_JOIN);
            cross->join.join_type = TK_JOIN;
            cross->join.condition = NULL;
            logical_node_add_child(cross, current);
            logical_node_add_child(cross, right_scan);

            if (current->output_schema && right_scan->output_schema) {
                int lc = current->output_schema->num_columns;
                int rc = right_scan->output_schema->num_columns;
                column_def_t* cols = db_calloc(lc + rc, sizeof(column_def_t));
                for (int c = 0; c < lc; c++) cols[c] = current->output_schema->columns[c];
                for (int c = 0; c < rc; c++) cols[lc + c] = right_scan->output_schema->columns[c];
                cross->output_schema = db_malloc(sizeof(schema_t));
                schema_create(cross->output_schema, cols, lc + rc);
                db_free(cols);
            }
            current = cross;
        }
    }

    if (where) {
        logical_node_t* filter = logical_node_create(LOP_FILTER);
        filter->filter.predicate = (expr_t*)where;
        filter->output_schema = current ? current->output_schema : NULL;
        logical_node_add_child(filter, current);
        current = filter;
    }

    if (group_count > 0 || select_count > 0) {
        int has_agg = 0;
        for (int i = 0; i < select_count; i++) {
            if (select_list[i] && select_list[i]->type == EXPR_FUNCTION_CALL) {
                has_agg = 1;
                break;
            }
        }
        if (group_count > 0 || has_agg) {
            logical_node_t* agg = logical_node_create(LOP_AGGREGATE);
            agg->aggregate.group_by = group_by;
            agg->aggregate.group_count = group_count;
            int agg_count = 0;
            for (int i = 0; i < select_count; i++) {
                if (select_list[i] && select_list[i]->type == EXPR_FUNCTION_CALL)
                    agg_count++;
            }
            if (agg_count > 0) {
                agg->aggregate.aggregates = db_malloc(sizeof(expr_t*) * agg_count);
                int ai = 0;
                for (int i = 0; i < select_count; i++) {
                    if (select_list[i] && select_list[i]->type == EXPR_FUNCTION_CALL)
                        agg->aggregate.aggregates[ai++] = select_list[i];
                }
            }
            agg->aggregate.agg_count = agg_count;
            agg->output_schema = current ? current->output_schema : NULL;
            logical_node_add_child(agg, current);
            current = agg;
        }
    }

    if (having) {
        logical_node_t* having_filter = logical_node_create(LOP_FILTER);
        having_filter->filter.predicate = having;
        having_filter->output_schema = current ? current->output_schema : NULL;
        logical_node_add_child(having_filter, current);
        current = having_filter;
    }

    if (select_count > 0) {
        int is_star = (select_count == 1 && select_list[0] &&
                      select_list[0]->type == EXPR_COLUMN_REF &&
                      strcmp(select_list[0]->column_ref.column, "*") == 0);
        if (!is_star) {
            logical_node_t* project = logical_node_create(LOP_PROJECT);
            project->project.expressions = select_list;
            project->project.expr_count = select_count;
            project->output_schema = current ? current->output_schema : NULL;
            logical_node_add_child(project, current);
            current = project;
        }
    }

    if (order_count > 0) {
        logical_node_t* sort = logical_node_create(LOP_SORT);
        sort->sort.items = order_by;
        sort->sort.count = order_count;
        sort->output_schema = current ? current->output_schema : NULL;
        logical_node_add_child(sort, current);
        current = sort;
    }

    if (limit >= 0) {
        logical_node_t* lim = logical_node_create(LOP_LIMIT);
        lim->limit_node.limit = limit;
        lim->limit_node.offset = offset;
        lim->output_schema = current ? current->output_schema : NULL;
        logical_node_add_child(lim, current);
        current = lim;
    }

    return current;
}

static logical_node_t* plan_insert(planner_t* p, const stmt_t* stmt) {
    table_meta_t meta;
    if (catalog_get_table(p->catalog, stmt->insert.table_name, &meta) != DB_OK) {
        LOG_ERROR("Table '%s' not found", stmt->insert.table_name);
        return NULL;
    }
    logical_node_t* node = logical_node_create(LOP_INSERT);
    strncpy(node->insert.table_name, stmt->insert.table_name, MAX_TABLE_NAME - 1);
    node->insert.table_id = meta.table_id;
    node->output_schema = get_table_schema(p, meta.table_id);
    return node;
}

static logical_node_t* plan_update(planner_t* p, const stmt_t* stmt) {
    table_meta_t meta;
    if (catalog_get_table(p->catalog, stmt->update.table_name, &meta) != DB_OK) {
        LOG_ERROR("Table '%s' not found", stmt->update.table_name);
        return NULL;
    }
    logical_node_t* node = logical_node_create(LOP_UPDATE);
    strncpy(node->update.table_name, stmt->update.table_name, MAX_TABLE_NAME - 1);
    node->update.table_id = meta.table_id;
    node->update.assignments = stmt->update.assignments;
    node->update.assign_count = stmt->update.assign_count;
    node->update.where = stmt->update.where;
    node->output_schema = get_table_schema(p, meta.table_id);

    logical_node_t* scan = logical_node_create(LOP_SCAN);
    strncpy(scan->scan.table_name, stmt->update.table_name, MAX_TABLE_NAME - 1);
    scan->scan.table_id = meta.table_id;
    scan->output_schema = get_table_schema(p, meta.table_id);
    logical_node_add_child(node, scan);

    if (stmt->update.where) {
        logical_node_t* filter = logical_node_create(LOP_FILTER);
        filter->filter.predicate = stmt->update.where;
        filter->output_schema = scan->output_schema;
        logical_node_add_child(filter, scan);
        node->children[0] = filter;
    }
    return node;
}

static logical_node_t* plan_delete(planner_t* p, const stmt_t* stmt) {
    table_meta_t meta;
    if (catalog_get_table(p->catalog, stmt->delete_stmt.table_name, &meta) != DB_OK) {
        LOG_ERROR("Table '%s' not found", stmt->delete_stmt.table_name);
        return NULL;
    }
    logical_node_t* node = logical_node_create(LOP_DELETE);
    strncpy(node->delete_node.table_name, stmt->delete_stmt.table_name, MAX_TABLE_NAME - 1);
    node->delete_node.table_id = meta.table_id;
    node->delete_node.where = stmt->delete_stmt.where;

    logical_node_t* scan = logical_node_create(LOP_SCAN);
    strncpy(scan->scan.table_name, stmt->delete_stmt.table_name, MAX_TABLE_NAME - 1);
    scan->scan.table_id = meta.table_id;
    scan->output_schema = get_table_schema(p, meta.table_id);
    logical_node_add_child(node, scan);

    if (stmt->delete_stmt.where) {
        logical_node_t* filter = logical_node_create(LOP_FILTER);
        filter->filter.predicate = stmt->delete_stmt.where;
        filter->output_schema = scan->output_schema;
        logical_node_add_child(filter, scan);
        node->children[0] = filter;
    }
    return node;
}

logical_node_t* planner_plan_logical(planner_t* p, const stmt_t* stmt) {
    if (!stmt) return NULL;
    switch (stmt->type) {
    case STMT_SELECT:  return plan_select(p, stmt);
    case STMT_INSERT:  return plan_insert(p, stmt);
    case STMT_UPDATE:  return plan_update(p, stmt);
    case STMT_DELETE:  return plan_delete(p, stmt);
    default:           return NULL;
    }
}

/* ---- Physical plan generation ---- */

static physical_node_t* convert_logical(planner_t* p, const logical_node_t* logical);

static physical_node_t* convert_scan(planner_t* p, const logical_node_t* logical) {
    (void)p;
    physical_node_t* node = physical_node_create(POP_SEQ_SCAN);
    strncpy(node->seq_scan.table_name, logical->scan.table_name, MAX_TABLE_NAME - 1);
    node->seq_scan.table_id = logical->scan.table_id;
    node->seq_scan.predicate = NULL;
    node->output_schema = clone_schema(logical->output_schema);
    return node;
}

static physical_node_t* convert_index_scan(planner_t* p, const logical_node_t* logical) {
    (void)p;
    physical_node_t* node = physical_node_create(POP_INDEX_SCAN);
    strncpy(node->index_scan.table_name, logical->index_scan.table_name, MAX_TABLE_NAME - 1);
    node->index_scan.table_id = logical->index_scan.table_id;
    node->index_scan.index_id = logical->index_scan.index_id;
    node->index_scan.column_id = logical->index_scan.column_id;
    node->index_scan.low_key = logical->index_scan.low_key;
    node->index_scan.high_key = logical->index_scan.high_key;
    node->index_scan.has_low = logical->index_scan.has_low;
    node->index_scan.has_high = logical->index_scan.has_high;
    node->output_schema = clone_schema(logical->output_schema);
    return node;
}

static physical_node_t* convert_filter(planner_t* p, const logical_node_t* logical) {
    /* Merge filter into child scan if possible */
    if (logical->child_count == 1 && logical->children[0]->op == LOP_SCAN) {
        physical_node_t* scan = convert_scan(p, logical->children[0]);
        scan->seq_scan.predicate = logical->filter.predicate;
        return scan;
    }

    physical_node_t* filter = physical_node_create(POP_FILTER);
    filter->filter.predicate = logical->filter.predicate;
    filter->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(filter, child);
    }
    return filter;
}

static physical_node_t* convert_project(planner_t* p, const logical_node_t* logical) {
    physical_node_t* node = physical_node_create(POP_PROJECT);
    node->project.expressions = logical->project.expressions;
    node->project.expr_count = logical->project.expr_count;
    node->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(node, child);
    }
    return node;
}

static physical_node_t* convert_join(planner_t* p, const logical_node_t* logical) {
    physical_node_t* node = physical_node_create(POP_NESTED_LOOP_JOIN);
    node->nl_join.join_type = logical->join.join_type;
    node->nl_join.condition = logical->join.condition;
    node->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(node, child);
    }
    return node;
}

static physical_node_t* convert_aggregate(planner_t* p, const logical_node_t* logical) {
    physical_node_t* node = physical_node_create(POP_HASH_AGGREGATE);
    node->hash_agg.group_by = logical->aggregate.group_by;
    node->hash_agg.group_count = logical->aggregate.group_count;
    node->hash_agg.aggregates = logical->aggregate.aggregates;
    node->hash_agg.agg_count = logical->aggregate.agg_count;
    node->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(node, child);
    }
    return node;
}

static physical_node_t* convert_sort(planner_t* p, const logical_node_t* logical) {
    physical_node_t* node = physical_node_create(POP_SORT);
    node->sort.items = logical->sort.items;
    node->sort.count = logical->sort.count;
    node->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(node, child);
    }
    return node;
}

static physical_node_t* convert_limit(planner_t* p, const logical_node_t* logical) {
    physical_node_t* node = physical_node_create(POP_LIMIT);
    node->limit_node.limit = logical->limit_node.limit;
    node->limit_node.offset = logical->limit_node.offset;
    node->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(node, child);
    }
    return node;
}

static physical_node_t* convert_insert(planner_t* p, const logical_node_t* logical) {
    (void)p;
    physical_node_t* node = physical_node_create(POP_INSERT);
    strncpy(node->insert.table_name, logical->insert.table_name, MAX_TABLE_NAME - 1);
    node->insert.table_id = logical->insert.table_id;
    node->output_schema = clone_schema(logical->output_schema);
    return node;
}

static physical_node_t* convert_update(planner_t* p, const logical_node_t* logical) {
    physical_node_t* node = physical_node_create(POP_UPDATE);
    strncpy(node->update.table_name, logical->update.table_name, MAX_TABLE_NAME - 1);
    node->update.table_id = logical->update.table_id;
    node->update.assignments = logical->update.assignments;
    node->update.assign_count = logical->update.assign_count;
    node->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(node, child);
    }
    return node;
}

static physical_node_t* convert_delete(planner_t* p, const logical_node_t* logical) {
    physical_node_t* node = physical_node_create(POP_DELETE);
    strncpy(node->delete_node.table_name, logical->delete_node.table_name, MAX_TABLE_NAME - 1);
    node->delete_node.table_id = logical->delete_node.table_id;
    node->output_schema = clone_schema(logical->output_schema);
    for (int i = 0; i < logical->child_count; i++) {
        physical_node_t* child = convert_logical(p, logical->children[i]);
        if (child) physical_node_add_child(node, child);
    }
    return node;
}

static physical_node_t* convert_logical(planner_t* p, const logical_node_t* logical) {
    if (!logical) return NULL;
    switch (logical->op) {
    case LOP_SCAN:        return convert_scan(p, logical);
    case LOP_INDEX_SCAN:  return convert_index_scan(p, logical);
    case LOP_FILTER:      return convert_filter(p, logical);
    case LOP_PROJECT:     return convert_project(p, logical);
    case LOP_JOIN:        return convert_join(p, logical);
    case LOP_AGGREGATE:   return convert_aggregate(p, logical);
    case LOP_SORT:        return convert_sort(p, logical);
    case LOP_LIMIT:       return convert_limit(p, logical);
    case LOP_INSERT:      return convert_insert(p, logical);
    case LOP_UPDATE:      return convert_update(p, logical);
    case LOP_DELETE:      return convert_delete(p, logical);
    }
    return NULL;
}

/* ---- RBO: MatchIndex optimization ---- */

static void optimize_match_index(planner_t* p, physical_node_t* node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++)
        optimize_match_index(p, node->children[i]);

    if (node->op != POP_SEQ_SCAN || !node->seq_scan.predicate) return;
    if (node->seq_scan.table_id == INVALID_TABLE_ID) return;

    vector_t indexes;
    vector_init(&indexes, sizeof(index_meta_t));
    catalog_get_table_indexes(p->catalog, node->seq_scan.table_id, &indexes);

    if (vector_size(&indexes) == 0) {
        vector_destroy(&indexes);
        return;
    }

    expr_t* pred = node->seq_scan.predicate;
    if (pred && pred->type == EXPR_BINARY && pred->binary.op == TK_EQUAL) {
        expr_t* left = pred->binary.left;
        expr_t* right = pred->binary.right;
        expr_t* col_expr = NULL;
        expr_t* lit_expr = NULL;
        if (left->type == EXPR_COLUMN_REF && right->type == EXPR_LITERAL) {
            col_expr = left; lit_expr = right;
        } else if (right->type == EXPR_COLUMN_REF && left->type == EXPR_LITERAL) {
            col_expr = right; lit_expr = left;
        }

        if (col_expr && lit_expr && node->output_schema) {
            for (size_t i = 0; i < vector_size(&indexes); i++) {
                index_meta_t* idx = (index_meta_t*)vector_at(&indexes, i);
                int col_idx = schema_find_column(node->output_schema, col_expr->column_ref.column);
                if (col_idx >= 0 && (column_id_t)col_idx == idx->column_id) {
                    /* Convert to IndexScan if the literal is an integer */
                    if (lit_expr->literal.value.type == VALUE_INT) {
                        int64_t key_val = lit_expr->literal.value.int_val;
                        node->op = POP_INDEX_SCAN;
                        strncpy(node->index_scan.table_name, node->seq_scan.table_name, MAX_TABLE_NAME);
                        node->index_scan.table_id = node->seq_scan.table_id;
                        node->index_scan.index_id = idx->index_id;
                        node->index_scan.column_id = idx->column_id;
                        node->index_scan.low_key = key_val;
                        node->index_scan.high_key = key_val;
                        node->index_scan.has_low = 1;
                        node->index_scan.has_high = 1;
                    }
                    break;
                }
            }
        }
    }
    vector_destroy(&indexes);
}

physical_node_t* planner_plan_physical(planner_t* p, const logical_node_t* logical) {
    physical_node_t* physical = convert_logical(p, logical);
    if (!physical) return NULL;
    optimize_match_index(p, physical);
    return physical;
}

/* ---- Public API ---- */

int planner_init(planner_t* p, catalog_t* cat) {
    p->catalog = cat;
    return DB_OK;
}

void planner_destroy(planner_t* p) {
    (void)p;
}

physical_node_t* planner_plan(planner_t* p, const stmt_t* stmt) {
    logical_node_t* logical = planner_plan_logical(p, stmt);
    if (!logical) return NULL;
    physical_node_t* physical = planner_plan_physical(p, logical);
    logical_node_destroy(logical);
    return physical;
}
