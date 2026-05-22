#pragma once

#include "src/parser/token.h"
#include "src/common/types.h"
#include "src/common/config.h"

/* ---- Statement types ---- */
typedef enum {
    STMT_CREATE_TABLE, STMT_DROP_TABLE, STMT_CREATE_INDEX, STMT_DROP_INDEX,
    STMT_INSERT, STMT_UPDATE, STMT_DELETE, STMT_SELECT,
    STMT_BEGIN, STMT_COMMIT, STMT_ROLLBACK, STMT_SET_ISOLATION,
    STMT_EXPLAIN,
} stmt_type_t;

/* ---- Expression types ---- */
typedef enum {
    EXPR_LITERAL, EXPR_COLUMN_REF, EXPR_BINARY, EXPR_UNARY,
    EXPR_FUNCTION_CALL, EXPR_SUBQUERY,
    EXPR_IS_NULL, EXPR_IN_EXPR, EXPR_BETWEEN, EXPR_LIKE,
} expr_type_t;

/* ---- Value types (for literal expressions) ---- */
typedef enum {
    VALUE_NULL, VALUE_BOOL, VALUE_INT, VALUE_FLOAT, VALUE_STRING,
} value_type_t;

typedef struct {
    value_type_t type;
    union {
        int     bool_val;
        int64_t int_val;
        double  float_val;
        char    str_val[256];
    };
} ast_value_t;

/* ---- Column definition (for CREATE TABLE) ---- */
typedef enum {
    COL_TYPE_INTEGER, COL_TYPE_FLOAT, COL_TYPE_VARCHAR, COL_TYPE_BOOLEAN,
} col_data_type_t;

typedef struct {
    char            name[MAX_COLUMN_NAME];
    col_data_type_t data_type;
    int             varchar_len;    /* for VARCHAR(n) */
    int            is_primary_key;
    int            not_null;
    int            is_unique;
    int            has_default;
    ast_value_t   default_value;
} ast_column_def_t;

/* ---- Expression ---- */
typedef struct expr expr_t;

struct expr {
    expr_type_t type;
    union {
        struct { ast_value_t value; } literal;
        struct { char table[64]; char column[64]; } column_ref;
        struct { expr_t* left; token_type_t op; expr_t* right; } binary;
        struct { token_type_t op; expr_t* operand; } unary;
        struct { char name[64]; expr_t** args; int arg_count; int is_star; } func_call;
        struct { struct stmt* subquery; } subquery;
        struct { expr_t* operand; int is_not; } is_null;
        struct { expr_t* left; expr_t** list; int list_count; int is_not; } in_expr;
        struct { expr_t* expr; expr_t* low; expr_t* high; int is_not; } between;
        struct { expr_t* expr; char pattern[256]; int is_not; } like;
    };
};

/* ---- Table reference (FROM clause) ---- */
typedef struct {
    char name[64];
    char alias[64];
} table_ref_t;

/* ---- Join clause ---- */
typedef struct {
    token_type_t join_type;  /* TK_INNER, TK_LEFT, TK_RIGHT, TK_JOIN */
    table_ref_t  table;
    expr_t*      condition;
} join_clause_t;

/* ---- Assignment (for UPDATE SET) ---- */
typedef struct {
    char   column[MAX_COLUMN_NAME];
    expr_t* value;
} assignment_t;

/* ---- Order by item ---- */
typedef struct {
    expr_t* expr;
    int     ascending;  /* 1=ASC, 0=DESC */
} order_by_item_t;

/* ---- Statement (tagged union) ---- */
typedef struct stmt stmt_t;

struct stmt {
    stmt_type_t type;
    union {
        struct {
            char          table_name[MAX_TABLE_NAME];
            ast_column_def_t* columns;
            int           column_count;
            int           if_not_exists;
        } create_table;
        struct {
            char table_name[MAX_TABLE_NAME];
            int  if_exists;
        } drop_table;
        struct {
            char table_name[MAX_TABLE_NAME];
            char index_name[64];
            char columns[8][MAX_COLUMN_NAME];
            int  column_count;
            int  is_unique;
        } create_index;
        struct {
            char index_name[64];
            char table_name[MAX_TABLE_NAME];
        } drop_index;
        struct {
            char    table_name[MAX_TABLE_NAME];
            char**  columns;
            int     col_count;
            expr_t*** values;    /* array of rows, each row is array of exprs */
            int     val_row_count;
            int*    val_col_counts;
        } insert;
        struct {
            char          table_name[MAX_TABLE_NAME];
            assignment_t* assignments;
            int           assign_count;
            expr_t*       where;
        } update;
        struct {
            char   table_name[MAX_TABLE_NAME];
            expr_t* where;
        } delete_stmt;
        struct {
            expr_t**        select_list;
            int             select_count;
            int             is_distinct;
            table_ref_t*    from_tables;
            int             from_count;
            join_clause_t*  joins;
            int             join_count;
            expr_t*         where;
            expr_t**        group_by;
            int             group_count;
            expr_t*         having;
            order_by_item_t* order_by;
            int             order_count;
            int             limit;
            int             offset;
        } select;
        struct {
            char isolation_level[32];
        } begin_txn;
        struct {
            char level[32];
        } set_isolation;
        struct {
            stmt_t* inner;
        } explain;
    };
};

/* ---- AST utility functions ---- */
expr_t*  ast_create_expr(expr_type_t type);
stmt_t*  ast_create_stmt(stmt_type_t type);
void     ast_destroy_expr(expr_t* expr);
void     ast_destroy_stmt(stmt_t* stmt);
void     ast_print_expr(const expr_t* expr, int indent);
void     ast_print_stmt(const stmt_t* stmt, int indent);
