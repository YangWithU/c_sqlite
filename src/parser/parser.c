#include "parser.h"
#include "src/common/mem.h"
#include "src/common/error.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

/* ---- Helper macros ---- */

#define PEEK(p)        ((p)->tokens[(p)->pos])
#define PEEK_TYPE(p)   ((p)->tokens[(p)->pos].type)
#define PEEK_NEXT(p)   ((p)->pos + 1 < (p)->token_count ? (p)->tokens[(p)->pos + 1].type : TK_EOF)
#define ADVANCE(p)     ((p)->pos++)
#define IS_AT_END(p)   ((p)->pos >= (p)->token_count || PEEK_TYPE(p) == TK_EOF)

static void parser_error(parser_t* p, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(p->error_msg, MAX_ERROR_MSG, fmt, args);
    va_end(args);
    p->error_line = PEEK(p).line;
    p->error_column = PEEK(p).column;
    p->has_error = 1;
}

static int match(parser_t* p, token_type_t type) {
    if (PEEK_TYPE(p) == type) {
        ADVANCE(p);
        return 1;
    }
    return 0;
}

static int expect(parser_t* p, token_type_t type) {
    if (PEEK_TYPE(p) == type) {
        ADVANCE(p);
        return 1;
    }
    parser_error(p, "Syntax error at line %d, column %d: expected %s but found %s",
                 PEEK(p).line, PEEK(p).column,
                 token_type_to_string(type), token_type_to_string(PEEK_TYPE(p)));
    return 0;
}

/* Forward declarations */
static expr_t*  parse_expression(parser_t* p);
static expr_t*  parse_expression_prec(parser_t* p, int min_prec);
static expr_t*  parse_primary(parser_t* p);
static stmt_t*  parse_statement(parser_t* p);

/* ---- Expression precedence (Pratt parsing) ---- */
static int get_precedence(token_type_t op) {
    switch (op) {
    case TK_OR:     return 1;
    case TK_AND:    return 2;
    case TK_NOT:    return 3;
    case TK_EQUAL: case TK_NOT_EQUAL: case TK_LESS: case TK_LESS_EQUAL:
    case TK_GREATER: case TK_GREATER_EQUAL:
    case TK_IS: case TK_IN: case TK_BETWEEN: case TK_LIKE:
        return 4;
    case TK_PLUS: case TK_MINUS:   return 5;
    case TK_STAR: case TK_SLASH:   return 6;
    default:        return 0;
    }
}

static int is_right_associative(token_type_t op) {
    (void)op;
    return 0;
}

/* ---- Parse expressions ---- */

static expr_t* parse_literal_expr(parser_t* p) {
    token_t tok = PEEK(p);
    expr_t* e = ast_create_expr(EXPR_LITERAL);
    if (!e) return NULL;

    switch (tok.type) {
    case TK_INT_LITERAL: {
        e->literal.value.type = VALUE_INT;
        e->literal.value.int_val = strtoll(tok.lexeme, NULL, 10);
        ADVANCE(p);
        break;
    }
    case TK_FLOAT_LITERAL: {
        e->literal.value.type = VALUE_FLOAT;
        e->literal.value.float_val = strtod(tok.lexeme, NULL);
        ADVANCE(p);
        break;
    }
    case TK_STRING_LITERAL: {
        e->literal.value.type = VALUE_STRING;
        strncpy(e->literal.value.str_val, tok.lexeme, 255);
        e->literal.value.str_val[255] = '\0';
        ADVANCE(p);
        break;
    }
    case TK_TRUE: {
        e->literal.value.type = VALUE_BOOL;
        e->literal.value.bool_val = 1;
        ADVANCE(p);
        break;
    }
    case TK_FALSE: {
        e->literal.value.type = VALUE_BOOL;
        e->literal.value.bool_val = 0;
        ADVANCE(p);
        break;
    }
    case TK_NULL: {
        e->literal.value.type = VALUE_NULL;
        ADVANCE(p);
        break;
    }
    default:
        parser_error(p, "Expected literal but found %s", token_type_to_string(tok.type));
        db_free(e);
        return NULL;
    }
    return e;
}

static expr_t* parse_column_ref_or_identifier(parser_t* p) {
    token_t tok = PEEK(p);
    if (tok.type != TK_IDENTIFIER) {
        parser_error(p, "Expected identifier but found %s", token_type_to_string(tok.type));
        return NULL;
    }

    expr_t* e = ast_create_expr(EXPR_COLUMN_REF);
    if (!e) return NULL;

    strncpy(e->column_ref.column, tok.lexeme, 63);
    e->column_ref.column[63] = '\0';
    e->column_ref.table[0] = '\0';
    ADVANCE(p);

    /* Check for table.column */
    if (PEEK_TYPE(p) == TK_DOT) {
        ADVANCE(p); /* consume . */
        /* Move column to table */
        strncpy(e->column_ref.table, e->column_ref.column, 63);
        e->column_ref.table[63] = '\0';
        token_t col_tok = PEEK(p);
        if (col_tok.type == TK_IDENTIFIER || col_tok.type == TK_STAR) {
            strncpy(e->column_ref.column, col_tok.lexeme, 63);
            e->column_ref.column[63] = '\0';
            ADVANCE(p);
        } else {
            parser_error(p, "Expected column name after '.'");
            ast_destroy_expr(e);
            return NULL;
        }
    }
    return e;
}

static expr_t* parse_function_call(parser_t* p, const char* name) {
    expr_t* e = ast_create_expr(EXPR_FUNCTION_CALL);
    if (!e) return NULL;

    strncpy(e->func_call.name, name, 63);
    e->func_call.name[63] = '\0';
    e->func_call.is_star = 0;
    e->func_call.arg_count = 0;
    e->func_call.args = NULL;

    expect(p, TK_LEFT_PAREN);

    /* COUNT(*) case */
    if (PEEK_TYPE(p) == TK_STAR) {
        e->func_call.is_star = 1;
        ADVANCE(p);
    } else if (PEEK_TYPE(p) != TK_RIGHT_PAREN) {
        /* Parse argument list */
        int cap = 4;
        e->func_call.args = db_malloc(sizeof(expr_t*) * (size_t)cap);
        while (1) {
            expr_t* arg = parse_expression(p);
            if (!arg) { ast_destroy_expr(e); return NULL; }
            if (e->func_call.arg_count >= cap) {
                cap *= 2;
                e->func_call.args = db_realloc(e->func_call.args, sizeof(expr_t*) * (size_t)cap);
            }
            e->func_call.args[e->func_call.arg_count++] = arg;
            if (!match(p, TK_COMMA)) break;
        }
    }
    expect(p, TK_RIGHT_PAREN);
    return e;
}

static expr_t* parse_primary(parser_t* p) {
    if (p->has_error) return NULL;

    token_type_t tt = PEEK_TYPE(p);

    /* Literals */
    if (tt == TK_INT_LITERAL || tt == TK_FLOAT_LITERAL ||
        tt == TK_STRING_LITERAL || tt == TK_NULL || tt == TK_TRUE || tt == TK_FALSE) {
        return parse_literal_expr(p);
    }

    /* Parenthesized expression */
    if (tt == TK_LEFT_PAREN) {
        ADVANCE(p);
        expr_t* e = parse_expression(p);
        expect(p, TK_RIGHT_PAREN);
        return e;
    }

    /* Unary operators */
    if (tt == TK_MINUS) {
        ADVANCE(p);
        expr_t* operand = parse_primary(p);
        if (!operand) return NULL;
        expr_t* e = ast_create_expr(EXPR_UNARY);
        if (!e) { ast_destroy_expr(operand); return NULL; }
        e->unary.op = TK_MINUS;
        e->unary.operand = operand;
        return e;
    }
    if (tt == TK_NOT) {
        ADVANCE(p);
        expr_t* operand = parse_expression_prec(p, 3); /* NOT has prec 3 */
        if (!operand) return NULL;
        expr_t* e = ast_create_expr(EXPR_UNARY);
        if (!e) { ast_destroy_expr(operand); return NULL; }
        e->unary.op = TK_NOT;
        e->unary.operand = operand;
        return e;
    }

    /* Aggregate functions */
    if (tt == TK_COUNT || tt == TK_SUM || tt == TK_AVG || tt == TK_MIN || tt == TK_MAX) {
        char name[64];
        strncpy(name, PEEK(p).lexeme, 63);
        name[63] = '\0';
        ADVANCE(p);
        return parse_function_call(p, name);
    }

    /* Identifier (column ref or function call) */
    if (tt == TK_IDENTIFIER) {
        /* Check if it's a function call */
        if (PEEK_NEXT(p) == TK_LEFT_PAREN) {
            char name[64];
            strncpy(name, PEEK(p).lexeme, 63);
            name[63] = '\0';
            ADVANCE(p);
            return parse_function_call(p, name);
        }
        return parse_column_ref_or_identifier(p);
    }

    /* Star (SELECT *) */
    if (tt == TK_STAR) {
        expr_t* e = ast_create_expr(EXPR_COLUMN_REF);
        if (!e) return NULL;
        strncpy(e->column_ref.column, "*", 63);
        e->column_ref.table[0] = '\0';
        ADVANCE(p);
        return e;
    }

    parser_error(p, "Unexpected token: %s ('%s')", token_type_to_string(tt), PEEK(p).lexeme);
    return NULL;
}

static expr_t* parse_expression_prec(parser_t* p, int min_prec) {
    expr_t* left = parse_primary(p);
    if (!left) return NULL;

    while (1) {
        token_type_t op = PEEK_TYPE(p);
        int prec = get_precedence(op);
        if (prec == 0 || prec < min_prec) break;

        /* IS [NOT] NULL */
        if (op == TK_IS) {
            ADVANCE(p); /* consume IS */
            int is_not = match(p, TK_NOT);
            if (PEEK_TYPE(p) == TK_NULL) {
                ADVANCE(p); /* consume NULL */
                expr_t* e = ast_create_expr(EXPR_IS_NULL);
                if (!e) { ast_destroy_expr(left); return NULL; }
                e->is_null.operand = left;
                e->is_null.is_not = is_not;
                left = e;
            } else {
                /* IS comparison (rare, treat as binary op) */
                expr_t* e = ast_create_expr(EXPR_BINARY);
                if (!e) { ast_destroy_expr(left); return NULL; }
                e->binary.op = is_not ? TK_NOT_EQUAL : TK_EQUAL;
                e->binary.left = left;
                e->binary.right = parse_expression_prec(p, prec + 1);
                if (!e->binary.right) { ast_destroy_expr(e); return NULL; }
                left = e;
            }
            continue;
        }

        /* NOT IN / NOT BETWEEN / NOT LIKE */
        int is_not = 0;
        if (op == TK_NOT) {
            token_type_t next = PEEK_NEXT(p);
            if (next == TK_IN || next == TK_BETWEEN || next == TK_LIKE) {
                ADVANCE(p); /* consume NOT */
                is_not = 1;
                op = PEEK_TYPE(p);
            } else {
                break; /* NOT as unary was handled in parse_primary */
            }
        }

        /* [NOT] IN */
        if (op == TK_IN) {
            ADVANCE(p); /* consume IN */
            expr_t* e = ast_create_expr(EXPR_IN_EXPR);
            if (!e) { ast_destroy_expr(left); return NULL; }
            e->in_expr.left = left;
            e->in_expr.is_not = is_not;
            expect(p, TK_LEFT_PAREN);
            int cap = 4;
            e->in_expr.list = db_malloc(sizeof(expr_t*) * (size_t)cap);
            e->in_expr.list_count = 0;
            while (1) {
                expr_t* item = parse_expression(p);
                if (!item) { ast_destroy_expr(e); return NULL; }
                if (e->in_expr.list_count >= cap) {
                    cap *= 2;
                    e->in_expr.list = db_realloc(e->in_expr.list, sizeof(expr_t*) * (size_t)cap);
                }
                e->in_expr.list[e->in_expr.list_count++] = item;
                if (!match(p, TK_COMMA)) break;
            }
            expect(p, TK_RIGHT_PAREN);
            left = e;
            continue;
        }

        /* [NOT] BETWEEN */
        if (op == TK_BETWEEN) {
            ADVANCE(p); /* consume BETWEEN */
            expr_t* e = ast_create_expr(EXPR_BETWEEN);
            if (!e) { ast_destroy_expr(left); return NULL; }
            e->between.expr = left;
            e->between.is_not = is_not;
            e->between.low = parse_expression_prec(p, prec + 1);
            if (!e->between.low) { ast_destroy_expr(e); return NULL; }
            expect(p, TK_AND);
            e->between.high = parse_expression_prec(p, prec + 1);
            if (!e->between.high) { ast_destroy_expr(e); return NULL; }
            left = e;
            continue;
        }

        /* [NOT] LIKE */
        if (op == TK_LIKE) {
            ADVANCE(p); /* consume LIKE */
            expr_t* e = ast_create_expr(EXPR_LIKE);
            if (!e) { ast_destroy_expr(left); return NULL; }
            e->like.expr = left;
            e->like.is_not = is_not;
            /* The pattern is the next expression (usually a string literal) */
            expr_t* pattern_expr = parse_primary(p);
            if (!pattern_expr) { ast_destroy_expr(e); return NULL; }
            if (pattern_expr->type == EXPR_LITERAL && pattern_expr->literal.value.type == VALUE_STRING) {
                strncpy(e->like.pattern, pattern_expr->literal.value.str_val, 255);
                e->like.pattern[255] = '\0';
                ast_destroy_expr(pattern_expr);
            } else {
                /* Fallback: store as regular binary */
                ast_destroy_expr(e);
                expr_t* bin = ast_create_expr(EXPR_BINARY);
                bin->binary.op = TK_LIKE;
                bin->binary.left = left;
                bin->binary.right = pattern_expr;
                left = bin;
                continue;
            }
            left = e;
            continue;
        }

        /* Regular binary operators */
        ADVANCE(p); /* consume operator */

        int next_prec = is_right_associative(op) ? prec : prec + 1;
        expr_t* right = parse_expression_prec(p, next_prec);
        if (!right) { ast_destroy_expr(left); return NULL; }

        expr_t* e = ast_create_expr(EXPR_BINARY);
        if (!e) { ast_destroy_expr(left); ast_destroy_expr(right); return NULL; }
        e->binary.op = op;
        e->binary.left = left;
        e->binary.right = right;
        left = e;
    }

    return left;
}

static expr_t* parse_expression(parser_t* p) {
    return parse_expression_prec(p, 1);
}

/* ---- Parse SELECT statement ---- */

static stmt_t* parse_select(parser_t* p) {
    ADVANCE(p); /* consume SELECT */

    stmt_t* s = ast_create_stmt(STMT_SELECT);
    if (!s) return NULL;
    s->select.is_distinct = 0;
    s->select.select_list = NULL;
    s->select.select_count = 0;
    s->select.from_tables = NULL;
    s->select.from_count = 0;
    s->select.joins = NULL;
    s->select.join_count = 0;
    s->select.where = NULL;
    s->select.group_by = NULL;
    s->select.group_count = 0;
    s->select.having = NULL;
    s->select.order_by = NULL;
    s->select.order_count = 0;
    s->select.limit = -1;
    s->select.offset = -1;

    /* DISTINCT */
    if (match(p, TK_DISTINCT)) {
        s->select.is_distinct = 1;
    }

    /* Select list */
    int sel_cap = 8;
    s->select.select_list = db_malloc(sizeof(expr_t*) * (size_t)sel_cap);
    while (1) {
        expr_t* expr = parse_expression(p);
        if (!expr) { ast_destroy_stmt(s); return NULL; }
        if (s->select.select_count >= sel_cap) {
            sel_cap *= 2;
            s->select.select_list = db_realloc(s->select.select_list, sizeof(expr_t*) * (size_t)sel_cap);
        }
        s->select.select_list[s->select.select_count++] = expr;

        /* Alias */
        if (PEEK_TYPE(p) == TK_AS) {
            ADVANCE(p); /* skip AS */
        }
        /* Consume alias identifier if present */
        if (PEEK_TYPE(p) == TK_IDENTIFIER && PEEK_NEXT(p) != TK_LEFT_PAREN) {
            /* Could be alias or FROM — check if it's a keyword context */
            token_type_t next2 = PEEK_TYPE(p);
            if (next2 == TK_IDENTIFIER) {
                /* Peek if this looks like an alias (next is comma or FROM) */
                int save_pos = p->pos;
                ADVANCE(p);
                if (PEEK_TYPE(p) == TK_COMMA || PEEK_TYPE(p) == TK_FROM) {
                    /* It was an alias — skip it */
                } else {
                    p->pos = save_pos;
                }
            }
        }

        if (!match(p, TK_COMMA)) break;
    }

    /* FROM */
    if (match(p, TK_FROM)) {
        int from_cap = 4;
        s->select.from_tables = db_malloc(sizeof(table_ref_t) * (size_t)from_cap);
        while (1) {
            if (PEEK_TYPE(p) != TK_IDENTIFIER) {
                parser_error(p, "Expected table name after FROM");
                ast_destroy_stmt(s);
                return NULL;
            }
            if (s->select.from_count >= from_cap) {
                from_cap *= 2;
                s->select.from_tables = db_realloc(s->select.from_tables, sizeof(table_ref_t) * (size_t)from_cap);
            }
            table_ref_t* tref = &s->select.from_tables[s->select.from_count++];
            strncpy(tref->name, PEEK(p).lexeme, 63);
            tref->name[63] = '\0';
            tref->alias[0] = '\0';
            ADVANCE(p);

            /* Alias */
            if (match(p, TK_AS)) {
                if (PEEK_TYPE(p) == TK_IDENTIFIER) {
                    strncpy(tref->alias, PEEK(p).lexeme, 63);
                    tref->alias[63] = '\0';
                    ADVANCE(p);
                }
            } else if (PEEK_TYPE(p) == TK_IDENTIFIER) {
                /* Implicit alias (identifier not a keyword) */
                /* Check it's not a JOIN/WHERE/GROUP/etc keyword */
                token_type_t alias_tt = PEEK_TYPE(p);
                if (alias_tt == TK_IDENTIFIER) {
                    strncpy(tref->alias, PEEK(p).lexeme, 63);
                    tref->alias[63] = '\0';
                    ADVANCE(p);
                }
            }

            if (!match(p, TK_COMMA)) break;
        }

        /* JOINs */
        int join_cap = 4;
        s->select.joins = db_malloc(sizeof(join_clause_t) * (size_t)join_cap);
        while (1) {
            token_type_t jt = TK_JOIN;
            int is_join = 0;

            if (match(p, TK_INNER)) {
                jt = TK_INNER;
                is_join = 1;
            } else if (match(p, TK_LEFT)) {
                match(p, TK_OUTER);
                jt = TK_LEFT;
                is_join = 1;
            } else if (match(p, TK_RIGHT)) {
                match(p, TK_OUTER);
                jt = TK_RIGHT;
                is_join = 1;
            } else if (match(p, TK_CROSS)) {
                jt = TK_CROSS;
                is_join = 1;
            } else if (match(p, TK_FULL)) {
                match(p, TK_OUTER);
                jt = TK_FULL;
                is_join = 1;
            }

            if (!is_join) break;
            if (!expect(p, TK_JOIN)) { ast_destroy_stmt(s); return NULL; }

            if (s->select.join_count >= join_cap) {
                join_cap *= 2;
                s->select.joins = db_realloc(s->select.joins, sizeof(join_clause_t) * (size_t)join_cap);
            }
            join_clause_t* jc = &s->select.joins[s->select.join_count++];
            jc->join_type = jt;
            jc->condition = NULL;

            if (PEEK_TYPE(p) != TK_IDENTIFIER) {
                parser_error(p, "Expected table name after JOIN");
                ast_destroy_stmt(s);
                return NULL;
            }
            strncpy(jc->table.name, PEEK(p).lexeme, 63);
            jc->table.name[63] = '\0';
            jc->table.alias[0] = '\0';
            ADVANCE(p);

            /* Alias */
            if (match(p, TK_AS) && PEEK_TYPE(p) == TK_IDENTIFIER) {
                strncpy(jc->table.alias, PEEK(p).lexeme, 63);
                jc->table.alias[63] = '\0';
                ADVANCE(p);
            }

            /* ON condition */
            if (match(p, TK_ON)) {
                jc->condition = parse_expression(p);
            }
        }
    }

    /* WHERE */
    if (match(p, TK_WHERE)) {
        s->select.where = parse_expression(p);
        if (!s->select.where) { ast_destroy_stmt(s); return NULL; }
    }

    /* GROUP BY */
    if (match(p, TK_GROUP)) {
        expect(p, TK_BY);
        int grp_cap = 4;
        s->select.group_by = db_malloc(sizeof(expr_t*) * (size_t)grp_cap);
        while (1) {
            expr_t* expr = parse_expression(p);
            if (!expr) { ast_destroy_stmt(s); return NULL; }
            if (s->select.group_count >= grp_cap) {
                grp_cap *= 2;
                s->select.group_by = db_realloc(s->select.group_by, sizeof(expr_t*) * (size_t)grp_cap);
            }
            s->select.group_by[s->select.group_count++] = expr;
            if (!match(p, TK_COMMA)) break;
        }
    }

    /* HAVING */
    if (match(p, TK_HAVING)) {
        s->select.having = parse_expression(p);
        if (!s->select.having) { ast_destroy_stmt(s); return NULL; }
    }

    /* ORDER BY */
    if (match(p, TK_ORDER)) {
        expect(p, TK_BY);
        int ord_cap = 4;
        s->select.order_by = db_malloc(sizeof(order_by_item_t) * (size_t)ord_cap);
        while (1) {
            order_by_item_t item;
            item.expr = parse_expression(p);
            if (!item.expr) { ast_destroy_stmt(s); return NULL; }
            item.ascending = 1;
            if (match(p, TK_ASC)) {
                item.ascending = 1;
            } else if (match(p, TK_DESC)) {
                item.ascending = 0;
            }
            if (s->select.order_count >= ord_cap) {
                ord_cap *= 2;
                s->select.order_by = db_realloc(s->select.order_by, sizeof(order_by_item_t) * (size_t)ord_cap);
            }
            s->select.order_by[s->select.order_count++] = item;
            if (!match(p, TK_COMMA)) break;
        }
    }

    /* LIMIT */
    if (match(p, TK_LIMIT)) {
        if (PEEK_TYPE(p) == TK_INT_LITERAL) {
            s->select.limit = (int)strtol(PEEK(p).lexeme, NULL, 10);
            ADVANCE(p);
        } else {
            parser_error(p, "Expected integer after LIMIT");
            ast_destroy_stmt(s);
            return NULL;
        }
        /* OFFSET */
        if (match(p, TK_OFFSET)) {
            if (PEEK_TYPE(p) == TK_INT_LITERAL) {
                s->select.offset = (int)strtol(PEEK(p).lexeme, NULL, 10);
                ADVANCE(p);
            }
        }
    }

    return s;
}

/* ---- Parse CREATE TABLE ---- */

static stmt_t* parse_create_table(parser_t* p) {
    ADVANCE(p); /* consume CREATE */
    expect(p, TK_TABLE);

    stmt_t* s = ast_create_stmt(STMT_CREATE_TABLE);
    if (!s) return NULL;

    s->create_table.if_not_exists = 0;
    s->create_table.columns = NULL;
    s->create_table.column_count = 0;

    /* IF NOT EXISTS */
    if (match(p, TK_IF)) {
        expect(p, TK_NOT);
        expect(p, TK_EXISTS);
        s->create_table.if_not_exists = 1;
    }

    /* Table name */
    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected table name");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->create_table.table_name, PEEK(p).lexeme, MAX_TABLE_NAME - 1);
    ADVANCE(p);

    /* Column definitions */
    expect(p, TK_LEFT_PAREN);

    int col_cap = 8;
    s->create_table.columns = db_calloc((size_t)col_cap, sizeof(column_def_t));

    while (1) {
        if (s->create_table.column_count >= col_cap) {
            col_cap *= 2;
            s->create_table.columns = db_realloc(s->create_table.columns, sizeof(column_def_t) * (size_t)col_cap);
            memset(s->create_table.columns + s->create_table.column_count, 0,
                   sizeof(column_def_t) * (size_t)(col_cap - s->create_table.column_count));
        }

        column_def_t* col = &s->create_table.columns[s->create_table.column_count++];

        /* Column name */
        if (PEEK_TYPE(p) != TK_IDENTIFIER) {
            parser_error(p, "Expected column name");
            ast_destroy_stmt(s);
            return NULL;
        }
        strncpy(col->name, PEEK(p).lexeme, MAX_COLUMN_NAME - 1);
        ADVANCE(p);

        /* Data type */
        switch (PEEK_TYPE(p)) {
        case TK_INTEGER:
            col->data_type = COL_TYPE_INTEGER;
            ADVANCE(p);
            break;
        case TK_FLOAT:
            col->data_type = COL_TYPE_FLOAT;
            ADVANCE(p);
            break;
        case TK_VARCHAR:
            col->data_type = COL_TYPE_VARCHAR;
            ADVANCE(p);
            if (match(p, TK_LEFT_PAREN)) {
                if (PEEK_TYPE(p) == TK_INT_LITERAL) {
                    col->varchar_len = (int)strtol(PEEK(p).lexeme, NULL, 10);
                    ADVANCE(p);
                }
                expect(p, TK_RIGHT_PAREN);
            }
            break;
        case TK_BOOLEAN:
            col->data_type = COL_TYPE_BOOLEAN;
            ADVANCE(p);
            break;
        default:
            parser_error(p, "Expected data type but found %s", token_type_to_string(PEEK_TYPE(p)));
            ast_destroy_stmt(s);
            return NULL;
        }

        /* Column constraints */
        while (1) {
            if (match(p, TK_PRIMARY)) {
                expect(p, TK_KEY);
                col->is_primary_key = 1;
                col->not_null = 1;
            } else if (match(p, TK_NOT)) {
                expect(p, TK_NULL);
                col->not_null = 1;
            } else if (match(p, TK_UNIQUE)) {
                col->is_unique = 1;
            } else if (match(p, TK_DEFAULT)) {
                col->has_default = 1;
                expr_t* def = parse_expression(p);
                if (def && def->type == EXPR_LITERAL) {
                    col->default_value = def->literal.value;
                }
                ast_destroy_expr(def);
            } else {
                break;
            }
        }

        if (!match(p, TK_COMMA)) break;
    }

    expect(p, TK_RIGHT_PAREN);
    return s;
}

/* ---- Parse CREATE INDEX ---- */

static stmt_t* parse_create_index(parser_t* p) {
    ADVANCE(p); /* consume CREATE */
    expect(p, TK_INDEX);

    stmt_t* s = ast_create_stmt(STMT_CREATE_INDEX);
    if (!s) return NULL;

    s->create_index.is_unique = 0;
    s->create_index.column_count = 0;

    /* Index name */
    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected index name");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->create_index.index_name, PEEK(p).lexeme, 63);
    ADVANCE(p);

    /* ON table */
    expect(p, TK_ON);
    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected table name after ON");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->create_index.table_name, PEEK(p).lexeme, MAX_TABLE_NAME - 1);
    ADVANCE(p);

    /* Column list */
    expect(p, TK_LEFT_PAREN);
    while (1) {
        if (s->create_index.column_count >= 8) {
            parser_error(p, "Too many columns in index");
            ast_destroy_stmt(s);
            return NULL;
        }
        if (PEEK_TYPE(p) != TK_IDENTIFIER) {
            parser_error(p, "Expected column name in index");
            ast_destroy_stmt(s);
            return NULL;
        }
        strncpy(s->create_index.columns[s->create_index.column_count], PEEK(p).lexeme, MAX_COLUMN_NAME - 1);
        s->create_index.column_count++;
        ADVANCE(p);
        if (!match(p, TK_COMMA)) break;
    }
    expect(p, TK_RIGHT_PAREN);

    return s;
}

/* ---- Parse INSERT ---- */

static stmt_t* parse_insert(parser_t* p) {
    ADVANCE(p); /* consume INSERT */
    expect(p, TK_INTO);

    stmt_t* s = ast_create_stmt(STMT_INSERT);
    if (!s) return NULL;

    s->insert.columns = NULL;
    s->insert.col_count = 0;
    s->insert.values = NULL;
    s->insert.val_row_count = 0;
    s->insert.val_col_counts = NULL;

    /* Table name */
    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected table name after INSERT INTO");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->insert.table_name, PEEK(p).lexeme, MAX_TABLE_NAME - 1);
    ADVANCE(p);

    /* Optional column list */
    if (PEEK_TYPE(p) == TK_LEFT_PAREN && PEEK_NEXT(p) == TK_IDENTIFIER) {
        /* Check if this is column list or VALUES (could be ambiguous) */
        int save_pos = p->pos;
        ADVANCE(p); /* consume ( */
        /* Peek ahead to see if this is a column list or VALUES */
        if (PEEK_TYPE(p) == TK_IDENTIFIER) {
            /* It's a column list */
            int col_cap = 8;
            s->insert.columns = db_malloc(sizeof(char*) * (size_t)col_cap);
            while (1) {
                if (s->insert.col_count >= col_cap) {
                    col_cap *= 2;
                    s->insert.columns = db_realloc(s->insert.columns, sizeof(char*) * (size_t)col_cap);
                }
                if (PEEK_TYPE(p) != TK_IDENTIFIER) break;
                s->insert.columns[s->insert.col_count] = db_malloc(64);
                strncpy(s->insert.columns[s->insert.col_count], PEEK(p).lexeme, 63);
                s->insert.columns[s->insert.col_count][63] = '\0';
                s->insert.col_count++;
                ADVANCE(p);
                if (!match(p, TK_COMMA)) break;
            }
            expect(p, TK_RIGHT_PAREN);
        } else {
            /* Not a column list, restore position */
            p->pos = save_pos;
        }
    }

    /* VALUES */
    expect(p, TK_VALUES);

    int row_cap = 4;
    s->insert.values = db_malloc(sizeof(expr_t**) * (size_t)row_cap);
    s->insert.val_col_counts = db_malloc(sizeof(int) * (size_t)row_cap);

    while (1) {
        expect(p, TK_LEFT_PAREN);
        int col_cap = 8;
        expr_t** row = db_malloc(sizeof(expr_t*) * (size_t)col_cap);
        int col_count = 0;

        while (1) {
            if (col_count >= col_cap) {
                col_cap *= 2;
                row = db_realloc(row, sizeof(expr_t*) * (size_t)col_cap);
            }
            expr_t* val = parse_expression(p);
            if (!val) {
                for (int i = 0; i < col_count; i++) ast_destroy_expr(row[i]);
                db_free(row);
                ast_destroy_stmt(s);
                return NULL;
            }
            row[col_count++] = val;
            if (!match(p, TK_COMMA)) break;
        }
        expect(p, TK_RIGHT_PAREN);

        if (s->insert.val_row_count >= row_cap) {
            row_cap *= 2;
            s->insert.values = db_realloc(s->insert.values, sizeof(expr_t**) * (size_t)row_cap);
            s->insert.val_col_counts = db_realloc(s->insert.val_col_counts, sizeof(int) * (size_t)row_cap);
        }
        s->insert.values[s->insert.val_row_count] = row;
        s->insert.val_col_counts[s->insert.val_row_count] = col_count;
        s->insert.val_row_count++;

        if (!match(p, TK_COMMA)) break;
    }

    return s;
}

/* ---- Parse UPDATE ---- */

static stmt_t* parse_update(parser_t* p) {
    ADVANCE(p); /* consume UPDATE */

    stmt_t* s = ast_create_stmt(STMT_UPDATE);
    if (!s) return NULL;

    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected table name after UPDATE");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->update.table_name, PEEK(p).lexeme, MAX_TABLE_NAME - 1);
    ADVANCE(p);

    /* SET */
    expect(p, TK_SET);

    int assign_cap = 4;
    s->update.assignments = db_malloc(sizeof(assignment_t) * (size_t)assign_cap);
    s->update.assign_count = 0;
    s->update.where = NULL;

    while (1) {
        if (s->update.assign_count >= assign_cap) {
            assign_cap *= 2;
            s->update.assignments = db_realloc(s->update.assignments, sizeof(assignment_t) * (size_t)assign_cap);
        }
        assignment_t* a = &s->update.assignments[s->update.assign_count++];

        if (PEEK_TYPE(p) != TK_IDENTIFIER) {
            parser_error(p, "Expected column name in SET clause");
            ast_destroy_stmt(s);
            return NULL;
        }
        strncpy(a->column, PEEK(p).lexeme, MAX_COLUMN_NAME - 1);
        ADVANCE(p);

        expect(p, TK_EQUAL);

        a->value = parse_expression(p);
        if (!a->value) { ast_destroy_stmt(s); return NULL; }

        if (!match(p, TK_COMMA)) break;
    }

    /* WHERE */
    if (match(p, TK_WHERE)) {
        s->update.where = parse_expression(p);
        if (!s->update.where) { ast_destroy_stmt(s); return NULL; }
    }

    return s;
}

/* ---- Parse DELETE ---- */

static stmt_t* parse_delete(parser_t* p) {
    ADVANCE(p); /* consume DELETE */
    expect(p, TK_FROM);

    stmt_t* s = ast_create_stmt(STMT_DELETE);
    if (!s) return NULL;

    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected table name after DELETE FROM");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->delete_stmt.table_name, PEEK(p).lexeme, MAX_TABLE_NAME - 1);
    ADVANCE(p);

    s->delete_stmt.where = NULL;
    if (match(p, TK_WHERE)) {
        s->delete_stmt.where = parse_expression(p);
        if (!s->delete_stmt.where) { ast_destroy_stmt(s); return NULL; }
    }

    return s;
}

/* ---- Parse DROP TABLE ---- */

static stmt_t* parse_drop_table(parser_t* p) {
    ADVANCE(p); /* consume DROP */
    expect(p, TK_TABLE);

    stmt_t* s = ast_create_stmt(STMT_DROP_TABLE);
    if (!s) return NULL;
    s->drop_table.if_exists = 0;

    if (match(p, TK_IF)) {
        expect(p, TK_EXISTS);
        s->drop_table.if_exists = 1;
    }

    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected table name after DROP TABLE");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->drop_table.table_name, PEEK(p).lexeme, MAX_TABLE_NAME - 1);
    ADVANCE(p);

    return s;
}

/* ---- Parse DROP INDEX ---- */

static stmt_t* parse_drop_index(parser_t* p) {
    ADVANCE(p); /* consume DROP */
    expect(p, TK_INDEX);

    stmt_t* s = ast_create_stmt(STMT_DROP_INDEX);
    if (!s) return NULL;

    if (PEEK_TYPE(p) != TK_IDENTIFIER) {
        parser_error(p, "Expected index name after DROP INDEX");
        ast_destroy_stmt(s);
        return NULL;
    }
    strncpy(s->drop_index.index_name, PEEK(p).lexeme, 63);
    ADVANCE(p);

    if (match(p, TK_ON)) {
        if (PEEK_TYPE(p) == TK_IDENTIFIER) {
            strncpy(s->drop_index.table_name, PEEK(p).lexeme, MAX_TABLE_NAME - 1);
            ADVANCE(p);
        }
    }

    return s;
}

/* ---- Parse BEGIN/COMMIT/ROLLBACK ---- */

static stmt_t* parse_begin(parser_t* p) {
    ADVANCE(p); /* consume BEGIN */

    stmt_t* s = ast_create_stmt(STMT_BEGIN);
    if (!s) return NULL;
    s->begin_txn.isolation_level[0] = '\0';

    if (match(p, TK_TRANSACTION)) {
        /* Optional TRANSACTION keyword */
    }

    if (match(p, TK_ISOLATION)) {
        match(p, TK_LEVEL); /* optional LEVEL keyword */
        /* Parse isolation level */
        if (match(p, TK_READ)) {
            if (match(p, TK_UNCOMMITTED)) {
                strcpy(s->begin_txn.isolation_level, "READ UNCOMMITTED");
            } else if (match(p, TK_COMMITTED)) {
                strcpy(s->begin_txn.isolation_level, "READ COMMITTED");
            }
        } else if (match(p, TK_REPEATABLE)) {
            expect(p, TK_READ);
            strcpy(s->begin_txn.isolation_level, "REPEATABLE READ");
        } else if (match(p, TK_SERIALIZABLE)) {
            strcpy(s->begin_txn.isolation_level, "SERIALIZABLE");
        }
    }

    return s;
}

/* ---- Parse EXPLAIN ---- */

static stmt_t* parse_explain(parser_t* p) {
    ADVANCE(p); /* consume EXPLAIN */

    stmt_t* s = ast_create_stmt(STMT_EXPLAIN);
    if (!s) return NULL;

    s->explain.inner = parse_statement(p);
    if (!s->explain.inner) { ast_destroy_stmt(s); return NULL; }

    return s;
}

/* ---- Top-level statement parser ---- */

static stmt_t* parse_statement(parser_t* p) {
    if (p->has_error) return NULL;
    if (IS_AT_END(p)) return NULL;

    switch (PEEK_TYPE(p)) {
    case TK_SELECT:  return parse_select(p);
    case TK_CREATE:
        if (PEEK_NEXT(p) == TK_INDEX || PEEK_NEXT(p) == TK_UNIQUE)
            return parse_create_index(p);
        return parse_create_table(p);
    case TK_DROP:
        if (PEEK_NEXT(p) == TK_INDEX)
            return parse_drop_index(p);
        return parse_drop_table(p);
    case TK_INSERT:  return parse_insert(p);
    case TK_UPDATE:  return parse_update(p);
    case TK_DELETE:  return parse_delete(p);
    case TK_BEGIN:   return parse_begin(p);
    case TK_COMMIT:
        ADVANCE(p);
        return ast_create_stmt(STMT_COMMIT);
    case TK_ROLLBACK:
        ADVANCE(p);
        return ast_create_stmt(STMT_ROLLBACK);
    case TK_EXPLAIN: return parse_explain(p);
    default:
        parser_error(p, "Unexpected token: %s ('%s') at line %d, column %d",
                     token_type_to_string(PEEK_TYPE(p)), PEEK(p).lexeme,
                     PEEK(p).line, PEEK(p).column);
        return NULL;
    }
}

/* ---- Public API ---- */

void parser_init(parser_t* p, token_t* tokens, int count) {
    p->tokens = tokens;
    p->token_count = count;
    p->pos = 0;
    p->error_msg[0] = '\0';
    p->error_line = 0;
    p->error_column = 0;
    p->has_error = 0;
}

void parser_destroy(parser_t* p) {
    (void)p;
}

stmt_t* parser_parse(parser_t* p) {
    stmt_t* s = parse_statement(p);
    if (s && !p->has_error) {
        match(p, TK_SEMICOLON); /* optional trailing semicolon */
    }
    return s;
}

stmt_t** parser_parse_all(parser_t* p, int* out_count) {
    int cap = 16;
    int count = 0;
    stmt_t** stmts = db_malloc(sizeof(stmt_t*) * (size_t)cap);
    if (!stmts) return NULL;

    while (!IS_AT_END(p) && !p->has_error) {
        if (PEEK_TYPE(p) == TK_SEMICOLON) {
            ADVANCE(p);
            continue;
        }
        stmt_t* s = parse_statement(p);
        if (!s) break;

        if (count >= cap) {
            cap *= 2;
            stmts = db_realloc(stmts, sizeof(stmt_t*) * (size_t)cap);
        }
        stmts[count++] = s;

        /* Consume optional semicolon */
        match(p, TK_SEMICOLON);
    }

    *out_count = count;
    return stmts;
}
