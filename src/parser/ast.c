#include "ast.h"
#include "src/common/mem.h"
#include "src/common/logger.h"
#include <stdio.h>
#include <string.h>

expr_t* ast_create_expr(expr_type_t type) {
    expr_t* e = db_calloc(1, sizeof(expr_t));
    if (e) e->type = type;
    return e;
}

stmt_t* ast_create_stmt(stmt_type_t type) {
    stmt_t* s = db_calloc(1, sizeof(stmt_t));
    if (s) s->type = type;
    return s;
}

void ast_destroy_expr(expr_t* expr) {
    if (!expr) return;
    switch (expr->type) {
    case EXPR_BINARY:
        ast_destroy_expr(expr->binary.left);
        ast_destroy_expr(expr->binary.right);
        break;
    case EXPR_UNARY:
        ast_destroy_expr(expr->unary.operand);
        break;
    case EXPR_FUNCTION_CALL:
        if (expr->func_call.args) {
            for (int i = 0; i < expr->func_call.arg_count; i++)
                ast_destroy_expr(expr->func_call.args[i]);
            db_free(expr->func_call.args);
        }
        break;
    case EXPR_SUBQUERY:
        ast_destroy_stmt(expr->subquery.subquery);
        break;
    case EXPR_IS_NULL:
        ast_destroy_expr(expr->is_null.operand);
        break;
    case EXPR_IN_EXPR:
        ast_destroy_expr(expr->in_expr.left);
        if (expr->in_expr.list) {
            for (int i = 0; i < expr->in_expr.list_count; i++)
                ast_destroy_expr(expr->in_expr.list[i]);
            db_free(expr->in_expr.list);
        }
        break;
    case EXPR_BETWEEN:
        ast_destroy_expr(expr->between.expr);
        ast_destroy_expr(expr->between.low);
        ast_destroy_expr(expr->between.high);
        break;
    case EXPR_LIKE:
        ast_destroy_expr(expr->like.expr);
        break;
    default:
        break;
    }
    db_free(expr);
}

void ast_destroy_stmt(stmt_t* stmt) {
    if (!stmt) return;
    switch (stmt->type) {
    case STMT_CREATE_TABLE:
        if (stmt->create_table.columns)
            db_free(stmt->create_table.columns);
        break;
    case STMT_CREATE_INDEX:
        break;
    case STMT_DROP_INDEX:
        break;
    case STMT_DROP_TABLE:
        break;
    case STMT_INSERT:
        if (stmt->insert.columns) {
            for (int i = 0; i < stmt->insert.col_count; i++)
                db_free(stmt->insert.columns[i]);
            db_free(stmt->insert.columns);
        }
        if (stmt->insert.values) {
            for (int r = 0; r < stmt->insert.val_row_count; r++) {
                if (stmt->insert.values[r]) {
                    for (int c = 0; c < stmt->insert.val_col_counts[r]; c++)
                        ast_destroy_expr(stmt->insert.values[r][c]);
                    db_free(stmt->insert.values[r]);
                }
            }
            db_free(stmt->insert.values);
        }
        if (stmt->insert.val_col_counts)
            db_free(stmt->insert.val_col_counts);
        break;
    case STMT_UPDATE:
        if (stmt->update.assignments) {
            for (int i = 0; i < stmt->update.assign_count; i++)
                ast_destroy_expr(stmt->update.assignments[i].value);
            db_free(stmt->update.assignments);
        }
        ast_destroy_expr(stmt->update.where);
        break;
    case STMT_DELETE:
        ast_destroy_expr(stmt->delete_stmt.where);
        break;
    case STMT_SELECT:
        if (stmt->select.select_list) {
            for (int i = 0; i < stmt->select.select_count; i++)
                ast_destroy_expr(stmt->select.select_list[i]);
            db_free(stmt->select.select_list);
        }
        if (stmt->select.from_tables)
            db_free(stmt->select.from_tables);
        if (stmt->select.joins) {
            for (int i = 0; i < stmt->select.join_count; i++)
                ast_destroy_expr(stmt->select.joins[i].condition);
            db_free(stmt->select.joins);
        }
        ast_destroy_expr(stmt->select.where);
        if (stmt->select.group_by) {
            for (int i = 0; i < stmt->select.group_count; i++)
                ast_destroy_expr(stmt->select.group_by[i]);
            db_free(stmt->select.group_by);
        }
        ast_destroy_expr(stmt->select.having);
        if (stmt->select.order_by) {
            for (int i = 0; i < stmt->select.order_count; i++)
                ast_destroy_expr(stmt->select.order_by[i].expr);
            db_free(stmt->select.order_by);
        }
        break;
    case STMT_EXPLAIN:
        ast_destroy_stmt(stmt->explain.inner);
        break;
    default:
        break;
    }
    db_free(stmt);
}

/* ---- Pretty-printing ---- */

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static void print_value(const ast_value_t* v) {
    switch (v->type) {
    case VALUE_NULL:   printf("NULL"); break;
    case VALUE_BOOL:   printf("%s", v->bool_val ? "TRUE" : "FALSE"); break;
    case VALUE_INT:    printf("%ld", (long)v->int_val); break;
    case VALUE_FLOAT:  printf("%g", v->float_val); break;
    case VALUE_STRING: printf("'%s'", v->str_val); break;
    }
}

void ast_print_expr(const expr_t* expr, int indent) {
    if (!expr) { print_indent(indent); printf("(null)\n"); return; }
    print_indent(indent);
    switch (expr->type) {
    case EXPR_LITERAL:
        printf("Literal: ");
        print_value(&expr->literal.value);
        printf("\n");
        break;
    case EXPR_COLUMN_REF:
        printf("ColumnRef: %s.%s\n",
               expr->column_ref.table[0] ? expr->column_ref.table : "",
               expr->column_ref.column);
        break;
    case EXPR_BINARY:
        printf("Binary: %s\n", token_type_to_string(expr->binary.op));
        ast_print_expr(expr->binary.left, indent + 1);
        ast_print_expr(expr->binary.right, indent + 1);
        break;
    case EXPR_UNARY:
        printf("Unary: %s\n", token_type_to_string(expr->unary.op));
        ast_print_expr(expr->unary.operand, indent + 1);
        break;
    case EXPR_FUNCTION_CALL:
        printf("FuncCall: %s(%s)\n", expr->func_call.name,
               expr->func_call.is_star ? "*" : "");
        for (int i = 0; i < expr->func_call.arg_count; i++)
            ast_print_expr(expr->func_call.args[i], indent + 1);
        break;
    case EXPR_IS_NULL:
        printf("IsNull%s\n", expr->is_null.is_not ? " (NOT)" : "");
        ast_print_expr(expr->is_null.operand, indent + 1);
        break;
    case EXPR_IN_EXPR:
        printf("In%s\n", expr->in_expr.is_not ? " (NOT)" : "");
        ast_print_expr(expr->in_expr.left, indent + 1);
        for (int i = 0; i < expr->in_expr.list_count; i++)
            ast_print_expr(expr->in_expr.list[i], indent + 1);
        break;
    case EXPR_BETWEEN:
        printf("Between%s\n", expr->between.is_not ? " (NOT)" : "");
        ast_print_expr(expr->between.expr, indent + 1);
        ast_print_expr(expr->between.low, indent + 1);
        ast_print_expr(expr->between.high, indent + 1);
        break;
    case EXPR_LIKE:
        printf("Like%s: '%s'\n", expr->like.is_not ? " (NOT)" : "", expr->like.pattern);
        ast_print_expr(expr->like.expr, indent + 1);
        break;
    case EXPR_SUBQUERY:
        printf("Subquery:\n");
        ast_print_stmt(expr->subquery.subquery, indent + 1);
        break;
    }
}

void ast_print_stmt(const stmt_t* stmt, int indent) {
    if (!stmt) { print_indent(indent); printf("(null)\n"); return; }
    print_indent(indent);
    switch (stmt->type) {
    case STMT_CREATE_TABLE:
        printf("CreateTable: %s (%d columns)\n",
               stmt->create_table.table_name, stmt->create_table.column_count);
        for (int i = 0; i < stmt->create_table.column_count; i++) {
            ast_column_def_t* col = &stmt->create_table.columns[i];
            print_indent(indent + 1);
            printf("Column: %s type=%d", col->name, col->data_type);
            if (col->is_primary_key) printf(" PRIMARY_KEY");
            if (col->not_null) printf(" NOT_NULL");
            if (col->is_unique) printf(" UNIQUE");
            printf("\n");
        }
        break;
    case STMT_DROP_TABLE:
        printf("DropTable: %s\n", stmt->drop_table.table_name);
        break;
    case STMT_CREATE_INDEX:
        printf("CreateIndex: %s ON %s (", stmt->create_index.index_name,
               stmt->create_index.table_name);
        for (int i = 0; i < stmt->create_index.column_count; i++) {
            if (i > 0) printf(", ");
            printf("%s", stmt->create_index.columns[i]);
        }
        printf(")%s\n", stmt->create_index.is_unique ? " UNIQUE" : "");
        break;
    case STMT_DROP_INDEX:
        printf("DropIndex: %s ON %s\n", stmt->drop_index.index_name,
               stmt->drop_index.table_name);
        break;
    case STMT_INSERT:
        printf("Insert: %s (%d cols, %d rows)\n",
               stmt->insert.table_name, stmt->insert.col_count,
               stmt->insert.val_row_count);
        break;
    case STMT_UPDATE:
        printf("Update: %s\n", stmt->update.table_name);
        for (int i = 0; i < stmt->update.assign_count; i++) {
            print_indent(indent + 1);
            printf("Set: %s =\n", stmt->update.assignments[i].column);
            ast_print_expr(stmt->update.assignments[i].value, indent + 2);
        }
        if (stmt->update.where) {
            print_indent(indent + 1);
            printf("Where:\n");
            ast_print_expr(stmt->update.where, indent + 2);
        }
        break;
    case STMT_DELETE:
        printf("Delete: %s\n", stmt->delete_stmt.table_name);
        if (stmt->delete_stmt.where) {
            print_indent(indent + 1);
            printf("Where:\n");
            ast_print_expr(stmt->delete_stmt.where, indent + 2);
        }
        break;
    case STMT_SELECT:
        printf("Select%s:\n", stmt->select.is_distinct ? " DISTINCT" : "");
        print_indent(indent + 1);
        printf("Columns (%d):\n", stmt->select.select_count);
        for (int i = 0; i < stmt->select.select_count; i++)
            ast_print_expr(stmt->select.select_list[i], indent + 2);
        if (stmt->select.from_count > 0) {
            print_indent(indent + 1);
            printf("From:\n");
            for (int i = 0; i < stmt->select.from_count; i++) {
                print_indent(indent + 2);
                printf("%s%s%s\n", stmt->select.from_tables[i].name,
                       stmt->select.from_tables[i].alias[0] ? " AS " : "",
                       stmt->select.from_tables[i].alias[0] ? stmt->select.from_tables[i].alias : "");
            }
        }
        if (stmt->select.join_count > 0) {
            print_indent(indent + 1);
            printf("Joins (%d):\n", stmt->select.join_count);
        }
        if (stmt->select.where) {
            print_indent(indent + 1);
            printf("Where:\n");
            ast_print_expr(stmt->select.where, indent + 2);
        }
        if (stmt->select.group_count > 0) {
            print_indent(indent + 1);
            printf("GroupBy (%d):\n", stmt->select.group_count);
            for (int i = 0; i < stmt->select.group_count; i++)
                ast_print_expr(stmt->select.group_by[i], indent + 2);
        }
        if (stmt->select.having) {
            print_indent(indent + 1);
            printf("Having:\n");
            ast_print_expr(stmt->select.having, indent + 2);
        }
        if (stmt->select.order_count > 0) {
            print_indent(indent + 1);
            printf("OrderBy:\n");
            for (int i = 0; i < stmt->select.order_count; i++) {
                print_indent(indent + 2);
                ast_print_expr(stmt->select.order_by[i].expr, 0);
                printf(" %s\n", stmt->select.order_by[i].ascending ? "ASC" : "DESC");
            }
        }
        if (stmt->select.limit >= 0) {
            print_indent(indent + 1);
            printf("Limit: %d\n", stmt->select.limit);
        }
        break;
    case STMT_BEGIN:
        printf("Begin: %s\n", stmt->begin_txn.isolation_level);
        break;
    case STMT_COMMIT:
        printf("Commit\n");
        break;
    case STMT_ROLLBACK:
        printf("Rollback\n");
        break;
    case STMT_SET_ISOLATION:
        printf("SetIsolation: %s\n", stmt->set_isolation.level);
        break;
    case STMT_EXPLAIN:
        printf("Explain:\n");
        ast_print_stmt(stmt->explain.inner, indent + 1);
        break;
    }
}
