#include "xtest.h"
#include "src/executor/execution_engine.h"
#include "src/executor/bpm_heap.h"
#include "src/executor/executor.h"
#include "src/executor/seq_scan_executor.h"
#include "src/executor/filter_executor.h"
#include "src/executor/project_executor.h"
#include "src/executor/insert_executor.h"
#include "src/executor/update_executor.h"
#include "src/executor/delete_executor.h"
#include "src/executor/limit_executor.h"
#include "src/executor/sort_executor.h"
#include "src/executor/expr_evaluator.h"
#include "src/storage/disk_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/catalog/catalog.h"
#include "src/parser/lexer.h"
#include "src/parser/parser.h"
#include "src/parser/ast.h"
#include "src/storage/tuple.h"
#include "src/storage/value.h"
#include "src/common/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- Helpers ---- */

static char temp_db_path[256];

static void cleanup_temp(void) {
    if (temp_db_path[0]) unlink(temp_db_path);
}

static const char* make_temp_path(void) {
    static int init = 0;
    if (!init) { atexit(cleanup_temp); init = 1; }
    snprintf(temp_db_path, sizeof(temp_db_path),
             "/tmp/test_executor_%d.db", (int)getpid());
    return temp_db_path;
}

typedef struct {
    disk_manager_t        dm;
    buffer_pool_manager_t bpm;
    catalog_t             cat;
    execution_engine_t    engine;
} test_env_t;

static int setup_env(test_env_t* env) {
    const char* path = make_temp_path();
    unlink(path);
    if (disk_manager_open(&env->dm, path) != DB_OK) return -1;
    if (bpm_init(&env->bpm, 64, &env->dm) != DB_OK) return -1;
    if (catalog_init(&env->cat, &env->bpm) != DB_OK) return -1;
    if (execution_engine_init(&env->engine, &env->cat, &env->bpm) != DB_OK) return -1;

    /* Create a test table */
    column_def_t cols[] = {
        { .name = "id",   .type = TYPE_INTEGER, .nullable = false },
        { .name = "name", .type = TYPE_VARCHAR, .nullable = true, .max_length = 64 },
        { .name = "age",  .type = TYPE_INTEGER, .nullable = true },
    };
    schema_t schema;
    schema_create(&schema, cols, 3);
    catalog_create_table(&env->cat, "users", &schema);
    schema_destroy(&schema);

    return 0;
}

static void teardown_env(test_env_t* env) {
    execution_engine_destroy(&env->engine);
    catalog_destroy(&env->cat);
    bpm_destroy(&env->bpm);
    disk_manager_close(&env->dm);
    unlink(make_temp_path());
}

static stmt_t* parse_sql(const char* sql) {
    lexer_t lex;
    lexer_init(&lex, sql);
    token_t* tokens = NULL;
    int count = 0;
    if (lexer_tokenize(&lex, &tokens, &count) != 0) return NULL;
    parser_t parser;
    parser_init(&parser, tokens, count);
    stmt_t* s = parser_parse(&parser);
    db_free(tokens);
    return s;
}

static int insert_row(test_env_t* env, int64_t id, const char* name, int64_t age) {
    char sql[256];
    snprintf(sql, sizeof(sql), "INSERT INTO users VALUES (%ld, '%s', %ld)", id, name, age);
    stmt_t* stmt = parse_sql(sql);
    if (!stmt) return -1;

    int affected = 0;
    int rc = execution_engine_execute(&env->engine, stmt, NULL, NULL, &affected);
    ast_destroy_stmt(stmt);
    return rc;
}

/* ---- Tests ---- */

TEST(executor, create_table) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    /* Create another table */
    stmt_t* stmt = parse_sql("CREATE TABLE products (id INTEGER, name VARCHAR(32), price FLOAT)");
    ASSERT_NOT_NULL(stmt);

    int affected = 0;
    int rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, &affected);
    EXPECT_EQ(DB_OK, rc);

    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(executor, insert_and_select) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    /* Insert rows */
    EXPECT_EQ(DB_OK, insert_row(&env, 1, "Alice", 30));
    EXPECT_EQ(DB_OK, insert_row(&env, 2, "Bob", 25));
    EXPECT_EQ(DB_OK, insert_row(&env, 3, "Charlie", 35));

    /* Select all */
    stmt_t* stmt = parse_sql("SELECT * FROM users");
    ASSERT_NOT_NULL(stmt);

    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    schema_t* schema = NULL;
    int rc = execution_engine_execute(&env.engine, stmt, &tuples, &schema, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(3, (int)vector_size(&tuples));

    /* Clean up tuples */
    for (size_t i = 0; i < vector_size(&tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(&tuples);
    if (schema) { schema_destroy(schema); db_free(schema); }
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(executor, select_with_filter) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    EXPECT_EQ(DB_OK, insert_row(&env, 1, "Alice", 30));
    EXPECT_EQ(DB_OK, insert_row(&env, 2, "Bob", 25));
    EXPECT_EQ(DB_OK, insert_row(&env, 3, "Charlie", 35));

    stmt_t* stmt = parse_sql("SELECT * FROM users WHERE age > 28");
    ASSERT_NOT_NULL(stmt);

    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    schema_t* schema = NULL;
    int rc = execution_engine_execute(&env.engine, stmt, &tuples, &schema, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(2, (int)vector_size(&tuples));  /* Alice(30) and Charlie(35) */

    for (size_t i = 0; i < vector_size(&tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(&tuples);
    if (schema) { schema_destroy(schema); db_free(schema); }
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(executor, delete_rows) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    EXPECT_EQ(DB_OK, insert_row(&env, 1, "Alice", 30));
    EXPECT_EQ(DB_OK, insert_row(&env, 2, "Bob", 25));
    EXPECT_EQ(DB_OK, insert_row(&env, 3, "Charlie", 35));

    /* Delete Bob */
    stmt_t* stmt = parse_sql("DELETE FROM users WHERE id = 2");
    ASSERT_NOT_NULL(stmt);

    int affected = 0;
    int rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, &affected);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(1, affected);

    ast_destroy_stmt(stmt);

    /* Verify only 2 rows remain */
    stmt = parse_sql("SELECT * FROM users");
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    rc = execution_engine_execute(&env.engine, stmt, &tuples, NULL, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(2, (int)vector_size(&tuples));

    for (size_t i = 0; i < vector_size(&tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(&tuples);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(executor, update_rows) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    EXPECT_EQ(DB_OK, insert_row(&env, 1, "Alice", 30));
    EXPECT_EQ(DB_OK, insert_row(&env, 2, "Bob", 25));

    /* Update Alice's age */
    stmt_t* stmt = parse_sql("UPDATE users SET age = 31 WHERE id = 1");
    ASSERT_NOT_NULL(stmt);

    int affected = 0;
    int rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, &affected);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(1, affected);

    ast_destroy_stmt(stmt);

    /* Verify update took effect */
    stmt = parse_sql("SELECT * FROM users WHERE id = 1");
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    schema_t* schema = NULL;
    rc = execution_engine_execute(&env.engine, stmt, &tuples, &schema, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(1, (int)vector_size(&tuples));

    if (vector_size(&tuples) > 0) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, 0);
        const value_t* age = tuple_get_value(t, 2);  /* age is column 2 */
        EXPECT_NOT_NULL(age);
        if (age && age->type == TYPE_INTEGER) {
            EXPECT_EQ(31, age->val.int_val);
        }
    }

    for (size_t i = 0; i < vector_size(&tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(&tuples);
    if (schema) { schema_destroy(schema); db_free(schema); }
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(executor, limit) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    for (int i = 1; i <= 10; i++) {
        char sql[128];
        snprintf(sql, sizeof(sql), "INSERT INTO users VALUES (%d, 'user%d', %d)", i, i, 20 + i);
        stmt_t* stmt = parse_sql(sql);
        execution_engine_execute(&env.engine, stmt, NULL, NULL, NULL);
        ast_destroy_stmt(stmt);
    }

    stmt_t* stmt = parse_sql("SELECT * FROM users LIMIT 3");
    ASSERT_NOT_NULL(stmt);

    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    int rc = execution_engine_execute(&env.engine, stmt, &tuples, NULL, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(3, (int)vector_size(&tuples));

    for (size_t i = 0; i < vector_size(&tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(&tuples);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(executor, expr_evaluator_literals) {
    value_t result = value_make_null();

    /* Integer literal */
    expr_t* lit = ast_create_expr(EXPR_LITERAL);
    lit->literal.value.type = VALUE_INT;
    lit->literal.value.int_val = 42;
    int rc = expr_evaluate(lit, NULL, NULL, &result);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(TYPE_INTEGER, result.type);
    EXPECT_EQ(42, result.val.int_val);
    value_destroy(&result);
    ast_destroy_expr(lit);

    /* String literal */
    lit = ast_create_expr(EXPR_LITERAL);
    lit->literal.value.type = VALUE_STRING;
    strcpy(lit->literal.value.str_val, "hello");
    rc = expr_evaluate(lit, NULL, NULL, &result);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(TYPE_VARCHAR, result.type);
    EXPECT_STR_EQ("hello", result.val.varchar);
    value_destroy(&result);
    ast_destroy_expr(lit);
}

TEST(executor, expr_evaluator_binary) {
    value_t result = value_make_null();

    /* 3 + 4 */
    expr_t* left = ast_create_expr(EXPR_LITERAL);
    left->literal.value.type = VALUE_INT;
    left->literal.value.int_val = 3;

    expr_t* right = ast_create_expr(EXPR_LITERAL);
    right->literal.value.type = VALUE_INT;
    right->literal.value.int_val = 4;

    expr_t* binop = ast_create_expr(EXPR_BINARY);
    binop->binary.left = left;
    binop->binary.op = TK_PLUS;
    binop->binary.right = right;

    int rc = expr_evaluate(binop, NULL, NULL, &result);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(TYPE_INTEGER, result.type);
    EXPECT_EQ(7, result.val.int_val);
    value_destroy(&result);

    /* Don't free left/right separately — binop owns them */
    ast_destroy_expr(binop);
}

TEST(executor, drop_table) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    /* Drop the users table */
    stmt_t* stmt = parse_sql("DROP TABLE users");
    ASSERT_NOT_NULL(stmt);
    int rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, NULL);
    EXPECT_EQ(DB_OK, rc);
    ast_destroy_stmt(stmt);

    /* Verify it's gone — trying to select should fail */
    stmt = parse_sql("SELECT * FROM users");
    rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, NULL);
    EXPECT_NE(DB_OK, rc);
    ast_destroy_stmt(stmt);

    teardown_env(&env);
}

TEST(executor, e2e_workflow) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    /* Insert 5 rows */
    for (int i = 1; i <= 5; i++) {
        char sql[128];
        snprintf(sql, sizeof(sql), "INSERT INTO users VALUES (%d, 'user%d', %d)", i, i, 20 + i);
        stmt_t* stmt = parse_sql(sql);
        int rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, NULL);
        EXPECT_EQ(DB_OK, rc);
        ast_destroy_stmt(stmt);
    }

    /* Select with filter */
    stmt_t* stmt = parse_sql("SELECT * FROM users WHERE age >= 23");
    vector_t tuples;
    vector_init(&tuples, sizeof(tuple_t));
    int rc = execution_engine_execute(&env.engine, stmt, &tuples, NULL, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(3, (int)vector_size(&tuples));  /* users with age 23, 24, 25 */

    for (size_t i = 0; i < vector_size(&tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(&tuples);
    ast_destroy_stmt(stmt);

    /* Delete one row */
    stmt = parse_sql("DELETE FROM users WHERE id = 3");
    int affected = 0;
    rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, &affected);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(1, affected);
    ast_destroy_stmt(stmt);

    /* Update one row */
    stmt = parse_sql("UPDATE users SET age = 100 WHERE id = 1");
    rc = execution_engine_execute(&env.engine, stmt, NULL, NULL, &affected);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(1, affected);
    ast_destroy_stmt(stmt);

    /* Select with limit */
    stmt = parse_sql("SELECT * FROM users LIMIT 2");
    vector_init(&tuples, sizeof(tuple_t));
    rc = execution_engine_execute(&env.engine, stmt, &tuples, NULL, NULL);
    EXPECT_EQ(DB_OK, rc);
    EXPECT_EQ(2, (int)vector_size(&tuples));

    for (size_t i = 0; i < vector_size(&tuples); i++) {
        tuple_t* t = (tuple_t*)vector_at(&tuples, i);
        tuple_destroy(t);
    }
    vector_destroy(&tuples);
    ast_destroy_stmt(stmt);

    teardown_env(&env);
}
