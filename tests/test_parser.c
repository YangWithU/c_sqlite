#include "xtest.h"
#include "src/parser/lexer.h"
#include "src/parser/parser.h"
#include "src/parser/ast.h"
#include "src/parser/token.h"
#include "src/common/mem.h"
#include <string.h>
#include <stdio.h>

/* Helper: lex + parse a SQL string, return the AST */
static stmt_t* parse_sql(const char* sql) {
    lexer_t lex;
    lexer_init(&lex, sql);
    token_t* tokens = NULL;
    int token_count = 0;
    int rc = lexer_tokenize(&lex, &tokens, &token_count);
    if (rc != 0) return NULL;

    parser_t parser;
    parser_init(&parser, tokens, token_count);
    stmt_t* s = parser_parse(&parser);
    db_free(tokens);
    return s;
}

/* Helper: lex only, return tokens */
static int lex_sql(const char* sql, token_t** out_tokens, int* out_count) {
    lexer_t lex;
    lexer_init(&lex, sql);
    return lexer_tokenize(&lex, out_tokens, out_count);
}

/* ===================== Lexer Tests ===================== */

TEST(lexer, simple_select) {
    token_t* tokens;
    int count;
    int rc = lex_sql("SELECT * FROM t;", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_SELECT);
    EXPECT_EQ(tokens[1].type, TK_STAR);
    EXPECT_EQ(tokens[2].type, TK_FROM);
    EXPECT_EQ(tokens[3].type, TK_IDENTIFIER);
    EXPECT_STR_EQ(tokens[3].lexeme, "t");
    EXPECT_EQ(tokens[4].type, TK_SEMICOLON);
    EXPECT_EQ(tokens[5].type, TK_EOF);

    db_free(tokens);
}

TEST(lexer, keywords_case_insensitive) {
    token_t* tokens;
    int count;
    int rc = lex_sql("select FROM where", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_SELECT);
    EXPECT_EQ(tokens[1].type, TK_FROM);
    EXPECT_EQ(tokens[2].type, TK_WHERE);

    db_free(tokens);
}

TEST(lexer, integer_literal) {
    token_t* tokens;
    int count;
    int rc = lex_sql("42", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_INT_LITERAL);
    EXPECT_STR_EQ(tokens[0].lexeme, "42");

    db_free(tokens);
}

TEST(lexer, float_literal) {
    token_t* tokens;
    int count;
    int rc = lex_sql("3.14", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_FLOAT_LITERAL);
    EXPECT_STR_EQ(tokens[0].lexeme, "3.14");

    db_free(tokens);
}

TEST(lexer, string_literal) {
    token_t* tokens;
    int count;
    int rc = lex_sql("'hello world'", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_STRING_LITERAL);
    EXPECT_STR_EQ(tokens[0].lexeme, "hello world");

    db_free(tokens);
}

TEST(lexer, two_char_operators) {
    token_t* tokens;
    int count;
    int rc = lex_sql("<= >= <> !=", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_LESS_EQUAL);
    EXPECT_EQ(tokens[1].type, TK_GREATER_EQUAL);
    EXPECT_EQ(tokens[2].type, TK_NOT_EQUAL);
    EXPECT_EQ(tokens[3].type, TK_NOT_EQUAL);

    db_free(tokens);
}

TEST(lexer, line_comment) {
    token_t* tokens;
    int count;
    int rc = lex_sql("SELECT -- this is a comment\nFROM", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_SELECT);
    EXPECT_EQ(tokens[1].type, TK_FROM);
    EXPECT_EQ(tokens[2].type, TK_EOF);

    db_free(tokens);
}

TEST(lexer, block_comment) {
    token_t* tokens;
    int count;
    int rc = lex_sql("SELECT /* block comment */ FROM", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_SELECT);
    EXPECT_EQ(tokens[1].type, TK_FROM);

    db_free(tokens);
}

TEST(lexer, scientific_notation) {
    token_t* tokens;
    int count;
    int rc = lex_sql("1.5e10 2E-3", &tokens, &count);
    EXPECT_EQ(rc, 0);
    ASSERT_NOT_NULL(tokens);

    EXPECT_EQ(tokens[0].type, TK_FLOAT_LITERAL);
    EXPECT_EQ(tokens[1].type, TK_FLOAT_LITERAL);

    db_free(tokens);
}

/* ===================== Parser: SELECT Tests ===================== */

TEST(parser, select_star) {
    stmt_t* s = parse_sql("SELECT * FROM t");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.select_count, 1);
    EXPECT_EQ(s->select.from_count, 1);
    EXPECT_STR_EQ(s->select.from_tables[0].name, "t");
    ast_destroy_stmt(s);
}

TEST(parser, select_columns) {
    stmt_t* s = parse_sql("SELECT a, b, c FROM t");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.select_count, 3);
    ast_destroy_stmt(s);
}

TEST(parser, select_where) {
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a > 1 AND b = 'hello'");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    ASSERT_NOT_NULL(s->select.where);
    EXPECT_EQ(s->select.where->type, EXPR_BINARY);
    EXPECT_EQ(s->select.where->binary.op, TK_AND);
    ast_destroy_stmt(s);
}

TEST(parser, select_order_by) {
    stmt_t* s = parse_sql("SELECT * FROM t ORDER BY a ASC, b DESC");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.order_count, 2);
    EXPECT_EQ(s->select.order_by[0].ascending, 1);
    EXPECT_EQ(s->select.order_by[1].ascending, 0);
    ast_destroy_stmt(s);
}

TEST(parser, select_limit) {
    stmt_t* s = parse_sql("SELECT * FROM t LIMIT 10");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.limit, 10);
    ast_destroy_stmt(s);
}

TEST(parser, select_group_by) {
    stmt_t* s = parse_sql("SELECT a, COUNT(*) FROM t GROUP BY a HAVING COUNT(*) > 5");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.group_count, 1);
    ASSERT_NOT_NULL(s->select.having);
    ast_destroy_stmt(s);
}

TEST(parser, select_distinct) {
    stmt_t* s = parse_sql("SELECT DISTINCT a FROM t");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.is_distinct, 1);
    ast_destroy_stmt(s);
}

TEST(parser, select_join) {
    stmt_t* s = parse_sql("SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.join_count, 1);
    EXPECT_EQ(s->select.joins[0].join_type, TK_INNER);
    EXPECT_STR_EQ(s->select.joins[0].table.name, "t2");
    ast_destroy_stmt(s);
}

TEST(parser, select_left_join) {
    stmt_t* s = parse_sql("SELECT * FROM t1 LEFT JOIN t2 ON t1.id = t2.id");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_SELECT);
    EXPECT_EQ(s->select.join_count, 1);
    EXPECT_EQ(s->select.joins[0].join_type, TK_LEFT);
    ast_destroy_stmt(s);
}

/* ===================== Parser: CREATE TABLE Tests ===================== */

TEST(parser, create_table) {
    stmt_t* s = parse_sql("CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL, age INTEGER)");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_CREATE_TABLE);
    EXPECT_STR_EQ(s->create_table.table_name, "users");
    EXPECT_EQ(s->create_table.column_count, 3);

    EXPECT_STR_EQ(s->create_table.columns[0].name, "id");
    EXPECT_EQ(s->create_table.columns[0].data_type, COL_TYPE_INTEGER);
    EXPECT_EQ(s->create_table.columns[0].is_primary_key, 1);

    EXPECT_STR_EQ(s->create_table.columns[1].name, "name");
    EXPECT_EQ(s->create_table.columns[1].data_type, COL_TYPE_VARCHAR);
    EXPECT_EQ(s->create_table.columns[1].varchar_len, 100);
    EXPECT_EQ(s->create_table.columns[1].not_null, 1);

    EXPECT_STR_EQ(s->create_table.columns[2].name, "age");
    EXPECT_EQ(s->create_table.columns[2].data_type, COL_TYPE_INTEGER);

    ast_destroy_stmt(s);
}

TEST(parser, create_table_if_not_exists) {
    stmt_t* s = parse_sql("CREATE TABLE IF NOT EXISTS t (id INTEGER)");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_CREATE_TABLE);
    EXPECT_EQ(s->create_table.if_not_exists, 1);
    ast_destroy_stmt(s);
}

/* ===================== Parser: INSERT Tests ===================== */

TEST(parser, insert_values) {
    stmt_t* s = parse_sql("INSERT INTO t VALUES (1, 2.0, 'test')");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_INSERT);
    EXPECT_STR_EQ(s->insert.table_name, "t");
    EXPECT_EQ(s->insert.val_row_count, 1);
    EXPECT_EQ(s->insert.val_col_counts[0], 3);
    ast_destroy_stmt(s);
}

TEST(parser, insert_columns) {
    stmt_t* s = parse_sql("INSERT INTO t (a, b) VALUES (1, 2)");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_INSERT);
    EXPECT_EQ(s->insert.col_count, 2);
    EXPECT_EQ(s->insert.val_row_count, 1);
    ast_destroy_stmt(s);
}

TEST(parser, insert_multiple_rows) {
    stmt_t* s = parse_sql("INSERT INTO t VALUES (1, 2), (3, 4), (5, 6)");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_INSERT);
    EXPECT_EQ(s->insert.val_row_count, 3);
    ast_destroy_stmt(s);
}

/* ===================== Parser: UPDATE Tests ===================== */

TEST(parser, update) {
    stmt_t* s = parse_sql("UPDATE t SET a = 1, b = 'hello' WHERE id = 5");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_UPDATE);
    EXPECT_STR_EQ(s->update.table_name, "t");
    EXPECT_EQ(s->update.assign_count, 2);
    ASSERT_NOT_NULL(s->update.where);
    ast_destroy_stmt(s);
}

/* ===================== Parser: DELETE Tests ===================== */

TEST(parser, delete) {
    stmt_t* s = parse_sql("DELETE FROM t WHERE id = 5");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_DELETE);
    EXPECT_STR_EQ(s->delete_stmt.table_name, "t");
    ASSERT_NOT_NULL(s->delete_stmt.where);
    ast_destroy_stmt(s);
}

/* ===================== Parser: DROP Tests ===================== */

TEST(parser, drop_table) {
    stmt_t* s = parse_sql("DROP TABLE t");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_DROP_TABLE);
    EXPECT_STR_EQ(s->drop_table.table_name, "t");
    ast_destroy_stmt(s);
}

TEST(parser, drop_table_if_exists) {
    stmt_t* s = parse_sql("DROP TABLE IF EXISTS t");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_DROP_TABLE);
    EXPECT_EQ(s->drop_table.if_exists, 1);
    ast_destroy_stmt(s);
}

/* ===================== Parser: Transaction Tests ===================== */

TEST(parser, begin_txn) {
    stmt_t* s = parse_sql("BEGIN");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_BEGIN);
    ast_destroy_stmt(s);
}

TEST(parser, begin_isolation) {
    stmt_t* s = parse_sql("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_BEGIN);
    EXPECT_STR_EQ(s->begin_txn.isolation_level, "READ COMMITTED");
    ast_destroy_stmt(s);
}

TEST(parser, commit) {
    stmt_t* s = parse_sql("COMMIT");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_COMMIT);
    ast_destroy_stmt(s);
}

TEST(parser, rollback) {
    stmt_t* s = parse_sql("ROLLBACK");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_ROLLBACK);
    ast_destroy_stmt(s);
}

/* ===================== Parser: Expression Tests ===================== */

TEST(parser, expr_precedence) {
    /* a OR b AND c should parse as a OR (b AND c) */
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a OR b AND c");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_BINARY);
    EXPECT_EQ(where->binary.op, TK_OR);
    EXPECT_EQ(where->binary.right->type, EXPR_BINARY);
    EXPECT_EQ(where->binary.right->binary.op, TK_AND);
    ast_destroy_stmt(s);
}

TEST(parser, expr_parens) {
    /* (a OR b) AND c */
    stmt_t* s = parse_sql("SELECT * FROM t WHERE (a OR b) AND c");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_BINARY);
    EXPECT_EQ(where->binary.op, TK_AND);
    EXPECT_EQ(where->binary.left->type, EXPR_BINARY);
    EXPECT_EQ(where->binary.left->binary.op, TK_OR);
    ast_destroy_stmt(s);
}

TEST(parser, expr_arithmetic) {
    stmt_t* s = parse_sql("SELECT a + b * c FROM t");
    ASSERT_NOT_NULL(s);
    expr_t* col = s->select.select_list[0];
    ASSERT_NOT_NULL(col);
    /* a + (b * c) */
    EXPECT_EQ(col->type, EXPR_BINARY);
    EXPECT_EQ(col->binary.op, TK_PLUS);
    EXPECT_EQ(col->binary.right->type, EXPR_BINARY);
    EXPECT_EQ(col->binary.right->binary.op, TK_STAR);
    ast_destroy_stmt(s);
}

TEST(parser, expr_unary_minus) {
    stmt_t* s = parse_sql("SELECT -a FROM t");
    ASSERT_NOT_NULL(s);
    expr_t* col = s->select.select_list[0];
    ASSERT_NOT_NULL(col);
    EXPECT_EQ(col->type, EXPR_UNARY);
    EXPECT_EQ(col->unary.op, TK_MINUS);
    ast_destroy_stmt(s);
}

TEST(parser, expr_is_null) {
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a IS NULL");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_IS_NULL);
    EXPECT_EQ(where->is_null.is_not, 0);
    ast_destroy_stmt(s);
}

TEST(parser, expr_is_not_null) {
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a IS NOT NULL");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_IS_NULL);
    EXPECT_EQ(where->is_null.is_not, 1);
    ast_destroy_stmt(s);
}

TEST(parser, expr_in) {
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a IN (1, 2, 3)");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_IN_EXPR);
    EXPECT_EQ(where->in_expr.list_count, 3);
    EXPECT_EQ(where->in_expr.is_not, 0);
    ast_destroy_stmt(s);
}

TEST(parser, expr_not_in) {
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a NOT IN (1, 2)");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_IN_EXPR);
    EXPECT_EQ(where->in_expr.is_not, 1);
    ast_destroy_stmt(s);
}

TEST(parser, expr_between) {
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a BETWEEN 1 AND 10");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_BETWEEN);
    EXPECT_EQ(where->between.is_not, 0);
    ast_destroy_stmt(s);
}

TEST(parser, expr_like) {
    stmt_t* s = parse_sql("SELECT * FROM t WHERE a LIKE 'test'");
    ASSERT_NOT_NULL(s);
    expr_t* where = s->select.where;
    ASSERT_NOT_NULL(where);
    EXPECT_EQ(where->type, EXPR_LIKE);
    EXPECT_STR_EQ(where->like.pattern, "test");
    ast_destroy_stmt(s);
}

TEST(parser, expr_aggregate) {
    stmt_t* s = parse_sql("SELECT COUNT(*), SUM(a), AVG(b) FROM t");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->select.select_count, 3);

    EXPECT_EQ(s->select.select_list[0]->type, EXPR_FUNCTION_CALL);
    EXPECT_STR_EQ(s->select.select_list[0]->func_call.name, "COUNT");
    EXPECT_EQ(s->select.select_list[0]->func_call.is_star, 1);

    EXPECT_EQ(s->select.select_list[1]->type, EXPR_FUNCTION_CALL);
    EXPECT_STR_EQ(s->select.select_list[1]->func_call.name, "SUM");

    EXPECT_EQ(s->select.select_list[2]->type, EXPR_FUNCTION_CALL);
    EXPECT_STR_EQ(s->select.select_list[2]->func_call.name, "AVG");

    ast_destroy_stmt(s);
}

/* ===================== Parser: EXPLAIN ===================== */

TEST(parser, explain) {
    stmt_t* s = parse_sql("EXPLAIN SELECT * FROM t");
    ASSERT_NOT_NULL(s);
    EXPECT_EQ(s->type, STMT_EXPLAIN);
    ASSERT_NOT_NULL(s->explain.inner);
    EXPECT_EQ(s->explain.inner->type, STMT_SELECT);
    ast_destroy_stmt(s);
}

/* ===================== Parser: Multiple Statements ===================== */

TEST(parser, parse_all) {
    const char* sql = "CREATE TABLE t (id INTEGER); INSERT INTO t VALUES (1); SELECT * FROM t;";
    lexer_t lex;
    lexer_init(&lex, sql);
    token_t* tokens = NULL;
    int token_count = 0;
    int rc = lexer_tokenize(&lex, &tokens, &token_count);
    EXPECT_EQ(rc, 0);

    parser_t parser;
    parser_init(&parser, tokens, token_count);

    int stmt_count = 0;
    stmt_t** stmts = parser_parse_all(&parser, &stmt_count);
    ASSERT_NOT_NULL(stmts);
    EXPECT_EQ(stmt_count, 3);

    EXPECT_EQ(stmts[0]->type, STMT_CREATE_TABLE);
    EXPECT_EQ(stmts[1]->type, STMT_INSERT);
    EXPECT_EQ(stmts[2]->type, STMT_SELECT);

    for (int i = 0; i < stmt_count; i++)
        ast_destroy_stmt(stmts[i]);
    db_free(stmts);
    db_free(tokens);
}

/* ===================== AST Print Test ===================== */

TEST(parser, ast_print) {
    stmt_t* s = parse_sql("SELECT a, b FROM t WHERE a > 1");
    ASSERT_NOT_NULL(s);
    /* Just verify it doesn't crash */
    ast_print_stmt(s, 0);
    ast_destroy_stmt(s);
}
