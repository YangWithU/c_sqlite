#include "src/executor/execution_engine.h"
#include "src/executor/executor.h"
#include "src/executor/bpm_heap.h"
#include "src/executor/seq_scan_executor.h"
#include "src/executor/filter_executor.h"
#include "src/executor/project_executor.h"
#include "src/executor/insert_executor.h"
#include "src/executor/update_executor.h"
#include "src/executor/delete_executor.h"
#include "src/executor/nl_join_executor.h"
#include "src/executor/hash_agg_executor.h"
#include "src/executor/sort_executor.h"
#include "src/executor/limit_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/planner/logical_plan.h"
#include "src/planner/physical_plan.h"
#include "src/planner/explain.h"
#include "src/planner/value_bridge.h"
#include "src/common/mem.h"
#include "src/common/logger.h"
#include <stdio.h>
#include <string.h>

int execution_engine_init(execution_engine_t* engine, catalog_t* catalog,
                           buffer_pool_manager_t* bpm) {
    if (!engine || !catalog || !bpm) return DB_INVALID_ARGUMENT;

    engine->catalog = catalog;
    engine->bpm = bpm;

    engine->planner = db_malloc(sizeof(planner_t));
    if (!engine->planner) return DB_OUT_OF_MEMORY;
    int rc = planner_init(engine->planner, catalog);
    if (rc != DB_OK) { db_free(engine->planner); return rc; }

    return DB_OK;
}

void execution_engine_destroy(execution_engine_t* engine) {
    if (engine) {
        if (engine->planner) {
            planner_destroy(engine->planner);
            db_free(engine->planner);
        }
    }
}

/* ---- Build executor tree from physical plan ---- */

static executor_context_t make_exec_ctx(execution_engine_t* engine) {
    executor_context_t ctx;
    ctx.catalog = engine->catalog;
    ctx.bpm = engine->bpm;
    ctx.txn = NULL;       /* Phase 8 */
    ctx.lock_mgr = NULL;  /* Phase 8 */
    return ctx;
}

/* Build an executor tree recursively from a physical plan node. */
static executor_t* build_executor(execution_engine_t* engine,
                                    executor_context_t* ctx,
                                    const physical_node_t* plan) {
    if (!plan) return NULL;

    switch (plan->op) {
    case POP_SEQ_SCAN: {
        table_meta_t meta;
        schema_t* schema = db_malloc(sizeof(schema_t));
        if (!schema) return NULL;

        int rc = catalog_get_table(engine->catalog, plan->seq_scan.table_name, &meta);
        if (rc != DB_OK) { db_free(schema); return NULL; }
        rc = catalog_get_schema(engine->catalog, meta.table_id, schema);
        if (rc != DB_OK) { db_free(schema); return NULL; }

        bpm_heap_t* heap = db_malloc(sizeof(bpm_heap_t));
        if (!heap) { schema_destroy(schema); db_free(schema); return NULL; }
        bpm_heap_init(heap, engine->bpm, schema, meta.table_id, meta.root_page_id);

        /* Copy schema for output */
        schema_t* out_schema = db_malloc(sizeof(schema_t));
        if (!out_schema) { db_free(heap); schema_destroy(schema); db_free(schema); return NULL; }
        catalog_get_schema(engine->catalog, meta.table_id, out_schema);

        executor_t* exec = seq_scan_executor_create(ctx, heap, schema,
                                                      plan->seq_scan.predicate);
        if (exec) exec->output_schema = out_schema;
        return exec;
    }

    case POP_FILTER: {
        executor_t* child = build_executor(engine, ctx,
                                            plan->child_count > 0 ? plan->children[0] : NULL);
        if (!child) return NULL;

        schema_t* out_schema = NULL;
        if (child->output_schema) {
            out_schema = db_malloc(sizeof(schema_t));
            catalog_get_schema(engine->catalog, 0, out_schema);  /* placeholder */
            /* Actually copy from child schema */
            if (child->output_schema) {
                if (out_schema->columns) { schema_destroy(out_schema); }
                *out_schema = *child->output_schema;
            }
        }

        return filter_executor_create(ctx, child, plan->filter.predicate, out_schema);
    }

    case POP_PROJECT: {
        executor_t* child = build_executor(engine, ctx,
                                            plan->child_count > 0 ? plan->children[0] : NULL);
        if (!child) return NULL;

        schema_t* out_schema = db_malloc(sizeof(schema_t));
        if (!out_schema) { executor_destroy_tree(child); return NULL; }

        /* Build a simple output schema from project expressions */
        int ncols = plan->project.expr_count;
        column_def_t* cols = db_calloc(ncols, sizeof(column_def_t));
        for (int i = 0; i < ncols; i++) {
            snprintf(cols[i].name, MAX_COLUMN_NAME, "col%d", i);
            /* Try to infer type from child schema if column ref */
            if (plan->project.expressions[i]->type == EXPR_COLUMN_REF && child->output_schema) {
                int ci = schema_find_column(child->output_schema,
                                             plan->project.expressions[i]->column_ref.column);
                if (ci >= 0) cols[i] = child->output_schema->columns[ci];
                strncpy(cols[i].name, plan->project.expressions[i]->column_ref.column, MAX_COLUMN_NAME - 1);
            } else {
                cols[i].type = TYPE_INTEGER;  /* default */
                cols[i].nullable = true;
            }
        }
        schema_create(out_schema, cols, ncols);
        db_free(cols);

        return project_executor_create(ctx, child, plan->project.expressions,
                                        plan->project.expr_count, out_schema);
    }

    case POP_INSERT: {
        table_meta_t meta;
        schema_t* schema = db_malloc(sizeof(schema_t));
        if (!schema) return NULL;

        int rc = catalog_get_table(engine->catalog, plan->insert.table_name, &meta);
        if (rc != DB_OK) { db_free(schema); return NULL; }
        rc = catalog_get_schema(engine->catalog, meta.table_id, schema);
        if (rc != DB_OK) { db_free(schema); return NULL; }

        bpm_heap_t* heap = db_malloc(sizeof(bpm_heap_t));
        if (!heap) { schema_destroy(schema); db_free(schema); return NULL; }
        bpm_heap_init(heap, engine->bpm, schema, meta.table_id, meta.root_page_id);

        /* We need the INSERT stmt's value expressions.
         * For now, pass NULL values — the execution engine handles this differently
         * for INSERT since the physical plan doesn't carry the expressions.
         * We'll handle INSERT as a special case in execute_dml. */
        db_free(heap);
        schema_destroy(schema);
        db_free(schema);
        return NULL;
    }

    case POP_UPDATE: {
        executor_t* child = build_executor(engine, ctx,
                                            plan->child_count > 0 ? plan->children[0] : NULL);
        table_meta_t meta;
        schema_t* schema = db_malloc(sizeof(schema_t));
        if (!schema) return NULL;

        int rc = catalog_get_table(engine->catalog, plan->update.table_name, &meta);
        if (rc != DB_OK) { db_free(schema); if (child) executor_destroy_tree(child); return NULL; }
        rc = catalog_get_schema(engine->catalog, meta.table_id, schema);
        if (rc != DB_OK) { db_free(schema); if (child) executor_destroy_tree(child); return NULL; }

        bpm_heap_t* heap = db_malloc(sizeof(bpm_heap_t));
        if (!heap) { schema_destroy(schema); db_free(schema); if (child) executor_destroy_tree(child); return NULL; }
        bpm_heap_init(heap, engine->bpm, schema, meta.table_id, meta.root_page_id);

        return update_executor_create(ctx, heap, schema,
                                       plan->update.assignments,
                                       plan->update.assign_count, child);
    }

    case POP_DELETE: {
        executor_t* child = build_executor(engine, ctx,
                                            plan->child_count > 0 ? plan->children[0] : NULL);
        table_meta_t meta;
        schema_t* schema = db_malloc(sizeof(schema_t));
        if (!schema) return NULL;

        int rc = catalog_get_table(engine->catalog, plan->delete_node.table_name, &meta);
        if (rc != DB_OK) { db_free(schema); if (child) executor_destroy_tree(child); return NULL; }
        rc = catalog_get_schema(engine->catalog, meta.table_id, schema);
        if (rc != DB_OK) { db_free(schema); if (child) executor_destroy_tree(child); return NULL; }

        bpm_heap_t* heap = db_malloc(sizeof(bpm_heap_t));
        if (!heap) { schema_destroy(schema); db_free(schema); if (child) executor_destroy_tree(child); return NULL; }
        bpm_heap_init(heap, engine->bpm, schema, meta.table_id, meta.root_page_id);

        return delete_executor_create(ctx, heap, schema, child);
    }

    case POP_SORT: {
        executor_t* child = build_executor(engine, ctx,
                                            plan->child_count > 0 ? plan->children[0] : NULL);
        if (!child) return NULL;

        schema_t* out_schema = db_malloc(sizeof(schema_t));
        if (child->output_schema) {
            *out_schema = *child->output_schema;
        }
        return sort_executor_create(ctx, child, plan->sort.items, plan->sort.count, out_schema);
    }

    case POP_LIMIT: {
        executor_t* child = build_executor(engine, ctx,
                                            plan->child_count > 0 ? plan->children[0] : NULL);
        if (!child) return NULL;

        schema_t* out_schema = db_malloc(sizeof(schema_t));
        if (child->output_schema) {
            *out_schema = *child->output_schema;
        }
        return limit_executor_create(ctx, child, plan->limit_node.limit,
                                      plan->limit_node.offset, out_schema);
    }

    case POP_NESTED_LOOP_JOIN: {
        executor_t* outer = build_executor(engine, ctx,
                                             plan->child_count > 0 ? plan->children[0] : NULL);
        executor_t* inner = build_executor(engine, ctx,
                                             plan->child_count > 1 ? plan->children[1] : NULL);
        if (!outer || !inner) {
            if (outer) executor_destroy_tree(outer);
            if (inner) executor_destroy_tree(inner);
            return NULL;
        }

        /* Build combined schema */
        int total_cols = outer->output_schema->num_columns + inner->output_schema->num_columns;
        column_def_t* cols = db_calloc(total_cols, sizeof(column_def_t));
        int off = 0;
        for (int i = 0; i < outer->output_schema->num_columns; i++)
            cols[off++] = outer->output_schema->columns[i];
        for (int i = 0; i < inner->output_schema->num_columns; i++)
            cols[off++] = inner->output_schema->columns[i];
        schema_t* out_schema = db_malloc(sizeof(schema_t));
        schema_create(out_schema, cols, total_cols);
        db_free(cols);

        return nl_join_executor_create(ctx, outer, inner, plan->nl_join.join_type,
                                        plan->nl_join.condition, out_schema);
    }

    case POP_HASH_AGGREGATE: {
        executor_t* child = build_executor(engine, ctx,
                                             plan->child_count > 0 ? plan->children[0] : NULL);
        if (!child) return NULL;
        return hash_agg_executor_create(ctx, child, plan->hash_agg.group_by,
                                         plan->hash_agg.group_count,
                                         plan->hash_agg.aggregates,
                                         plan->hash_agg.agg_count, NULL);
    }

    default:
        return NULL;
    }
}

/* ---- DDL handling ---- */

static int execute_ddl(execution_engine_t* engine, const stmt_t* stmt, int* out_affected) {
    switch (stmt->type) {
    case STMT_CREATE_TABLE: {
        schema_t schema;
        int rc = ast_build_schema(stmt->create_table.columns,
                                   stmt->create_table.column_count, &schema);
        if (rc != DB_OK) return rc;

        table_id_t tid = catalog_create_table(engine->catalog,
                                                stmt->create_table.table_name, &schema);
        schema_destroy(&schema);
        if (tid == INVALID_TABLE_ID) return DB_DUPLICATE_KEY;
        if (out_affected) *out_affected = 0;
        return DB_OK;
    }

    case STMT_DROP_TABLE: {
        int rc = catalog_drop_table(engine->catalog, stmt->drop_table.table_name);
        if (out_affected) *out_affected = 0;
        return rc;
    }

    case STMT_CREATE_INDEX: {
        /* For now, create a B+ tree index. We need a root page. */
        page_id_t root_pid;
        page_t* root_page = bpm_new_page(engine->bpm, &root_pid);
        if (!root_page) return DB_OUT_OF_MEMORY;
        bpm_unpin_page(engine->bpm, root_pid, 1);

        /* Get table_id */
        table_meta_t meta;
        int rc = catalog_get_table(engine->catalog, stmt->create_index.table_name, &meta);
        if (rc != DB_OK) return rc;

        /* Find column_id */
        schema_t schema;
        rc = catalog_get_schema(engine->catalog, meta.table_id, &schema);
        if (rc != DB_OK) return rc;

        int col_idx = schema_find_column(&schema, stmt->create_index.columns[0]);
        schema_destroy(&schema);
        if (col_idx < 0) return DB_UNKNOWN_COLUMN;

        index_id_t iid = catalog_create_index(engine->catalog,
                                                stmt->create_index.index_name,
                                                meta.table_id,
                                                (column_id_t)col_idx,
                                                root_pid,
                                                stmt->create_index.is_unique);
        if (iid == INVALID_INDEX_ID) return DB_DUPLICATE_KEY;
        if (out_affected) *out_affected = 0;
        return DB_OK;
    }

    case STMT_DROP_INDEX: {
        int rc = catalog_drop_index(engine->catalog, stmt->drop_index.index_name);
        if (out_affected) *out_affected = 0;
        return rc;
    }

    default:
        return DB_INVALID_ARGUMENT;
    }
}

/* ---- DML handling via planner + executor ---- */

static int execute_dml(execution_engine_t* engine, const stmt_t* stmt,
                        vector_t* out_tuples, schema_t** out_schema, int* out_affected) {
    physical_node_t* physical = planner_plan(engine->planner, stmt);
    if (!physical) return DB_INTERNAL_ERROR;

    executor_context_t ctx = make_exec_ctx(engine);

    /* Special-case INSERT: the physical plan doesn't carry value expressions,
     * so we build the insert executor directly from the AST. */
    if (stmt->type == STMT_INSERT) {
        table_meta_t meta;
        schema_t* schema = db_malloc(sizeof(schema_t));
        if (!schema) { physical_node_destroy(physical); return DB_OUT_OF_MEMORY; }

        int rc = catalog_get_table(engine->catalog, stmt->insert.table_name, &meta);
        if (rc != DB_OK) { db_free(schema); physical_node_destroy(physical); return rc; }
        rc = catalog_get_schema(engine->catalog, meta.table_id, schema);
        if (rc != DB_OK) { db_free(schema); physical_node_destroy(physical); return rc; }

        bpm_heap_t* heap = db_malloc(sizeof(bpm_heap_t));
        if (!heap) { schema_destroy(schema); db_free(schema); physical_node_destroy(physical); return DB_OUT_OF_MEMORY; }
        bpm_heap_init(heap, engine->bpm, schema, meta.table_id, meta.root_page_id);

        executor_t* exec = insert_executor_create(&ctx, heap, schema,
                                                    stmt->insert.values,
                                                    stmt->insert.val_row_count,
                                                    stmt->insert.val_col_counts);
        if (!exec) {
            db_free(heap); schema_destroy(schema); db_free(schema);
            physical_node_destroy(physical);
            return DB_OUT_OF_MEMORY;
        }

        exec->init(exec);
        int affected = 0;
        tuple_t tuple;
        while (exec->next(exec, &tuple) == DB_OK) {
            affected++;
            if (out_tuples) {
                vector_push(out_tuples, &tuple);
            } else {
                tuple_destroy(&tuple);
            }
        }
        exec->close(exec);
        exec->destroy(exec);
        db_free(exec);

        /* Sync root_page_id back to catalog (first insert may allocate the first page) */
        if (heap->first_page_id != INVALID_PAGE_ID && meta.root_page_id == INVALID_PAGE_ID) {
            catalog_update_root_page(engine->catalog, meta.table_id, heap->first_page_id);
        }

        db_free(heap);
        schema_destroy(schema);
        db_free(schema);
        physical_node_destroy(physical);

        if (out_affected) *out_affected = affected;
        if (out_schema) {
            *out_schema = db_malloc(sizeof(schema_t));
            catalog_get_schema(engine->catalog, meta.table_id, *out_schema);
        }
        return DB_OK;
    }

    /* For other DML, build executor tree from physical plan */
    executor_t* exec = build_executor(engine, &ctx, physical);
    physical_node_destroy(physical);

    if (!exec) return DB_INTERNAL_ERROR;

    exec->init(exec);

    int affected = 0;
    tuple_t tuple;
    while (exec->next(exec, &tuple) == DB_OK) {
        affected++;
        if (out_tuples) {
            vector_push(out_tuples, &tuple);
        } else {
            tuple_destroy(&tuple);
        }
    }

    exec->close(exec);

    if (out_schema && exec->output_schema) {
        *out_schema = db_malloc(sizeof(schema_t));
        **out_schema = *exec->output_schema;
    }

    /* Don't double-free output_schema since we copied it */
    if (exec->output_schema) {
        exec->output_schema->columns = NULL;
        exec->output_schema->num_columns = 0;
    }

    executor_destroy_tree(exec);

    if (out_affected) *out_affected = affected;
    return DB_OK;
}

/* ---- EXPLAIN handling ---- */

static int execute_explain(execution_engine_t* engine, const stmt_t* stmt) {
    physical_node_t* physical = planner_plan(engine->planner, stmt->explain.inner);
    if (!physical) {
        printf("Cannot plan EXPLAIN query\n");
        return DB_OK;
    }
    explain_print_plan(physical);
    physical_node_destroy(physical);
    return DB_OK;
}

/* ---- Main entry point ---- */

int execution_engine_execute(execution_engine_t* engine, const stmt_t* stmt,
                              vector_t* out_tuples, schema_t** out_schema,
                              int* out_affected) {
    if (!engine || !stmt) return DB_INVALID_ARGUMENT;

    switch (stmt->type) {
    case STMT_CREATE_TABLE:
    case STMT_DROP_TABLE:
    case STMT_CREATE_INDEX:
    case STMT_DROP_INDEX:
        return execute_ddl(engine, stmt, out_affected);

    case STMT_SELECT:
    case STMT_INSERT:
    case STMT_UPDATE:
    case STMT_DELETE:
        return execute_dml(engine, stmt, out_tuples, out_schema, out_affected);

    case STMT_EXPLAIN:
        return execute_explain(engine, stmt);

    case STMT_BEGIN:
    case STMT_COMMIT:
    case STMT_ROLLBACK:
    case STMT_SET_ISOLATION:
        /* Phase 8 — for now, no-op */
        if (out_affected) *out_affected = 0;
        return DB_OK;

    default:
        return DB_INVALID_ARGUMENT;
    }
}
