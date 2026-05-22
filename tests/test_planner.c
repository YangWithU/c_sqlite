#include "xtest.h"
#include "src/planner/planner.h"
#include "src/planner/logical_plan.h"
#include "src/planner/physical_plan.h"
#include "src/planner/explain.h"
#include "src/planner/value_bridge.h"
#include "src/parser/lexer.h"
#include "src/parser/parser.h"
#include "src/parser/ast.h"
#include "src/storage/disk_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/catalog/catalog.h"
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
             "/tmp/test_planner_%d.db", (int)getpid());
    return temp_db_path;
}

typedef struct {
    disk_manager_t        dm;
    buffer_pool_manager_t bpm;
    catalog_t             cat;
    planner_t             planner;
} test_env_t;

static int setup_env(test_env_t* env) {
    const char* path = make_temp_path();
    unlink(path);
    if (disk_manager_open(&env->dm, path) != DB_OK) return -1;
    if (bpm_init(&env->bpm, 64, &env->dm) != DB_OK) return -1;
    if (catalog_init(&env->cat, &env->bpm) != DB_OK) return -1;
    if (planner_init(&env->planner, &env->cat) != DB_OK) return -1;

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
    planner_destroy(&env->planner);
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

/* ---- Tests ---- */

TEST(planner, select_simple) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("SELECT * FROM users");
    ASSERT_NOT_NULL(stmt);

    logical_node_t* logical = planner_plan_logical(&env.planner, stmt);
    ASSERT_NOT_NULL(logical);
    EXPECT_EQ(LOP_SCAN, logical->op);
    EXPECT_STR_EQ("users", logical->scan.table_name);

    logical_node_destroy(logical);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(planner, select_where) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("SELECT * FROM users WHERE id = 1");
    ASSERT_NOT_NULL(stmt);

    logical_node_t* logical = planner_plan_logical(&env.planner, stmt);
    ASSERT_NOT_NULL(logical);
    /* Should be: Scan -> Filter (since filter is pushed down, it may be
     * merged depending on implementation — check the tree structure) */
    EXPECT_TRUE(logical->op == LOP_FILTER || logical->op == LOP_SCAN);

    logical_node_destroy(logical);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(planner, insert) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("INSERT INTO users VALUES (1, 'Alice', 30)");
    ASSERT_NOT_NULL(stmt);

    logical_node_t* logical = planner_plan_logical(&env.planner, stmt);
    ASSERT_NOT_NULL(logical);
    EXPECT_EQ(LOP_INSERT, logical->op);
    EXPECT_STR_EQ("users", logical->insert.table_name);

    logical_node_destroy(logical);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(planner, update) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("UPDATE users SET age = 31 WHERE id = 1");
    ASSERT_NOT_NULL(stmt);

    logical_node_t* logical = planner_plan_logical(&env.planner, stmt);
    ASSERT_NOT_NULL(logical);
    EXPECT_EQ(LOP_UPDATE, logical->op);
    EXPECT_EQ(1, logical->update.assign_count);

    logical_node_destroy(logical);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(planner, delete_stmt) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("DELETE FROM users WHERE id = 1");
    ASSERT_NOT_NULL(stmt);

    logical_node_t* logical = planner_plan_logical(&env.planner, stmt);
    ASSERT_NOT_NULL(logical);
    EXPECT_EQ(LOP_DELETE, logical->op);
    EXPECT_STR_EQ("users", logical->delete_node.table_name);

    logical_node_destroy(logical);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(planner, physical_plan_select) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("SELECT * FROM users WHERE age > 20");
    ASSERT_NOT_NULL(stmt);

    physical_node_t* physical = planner_plan(&env.planner, stmt);
    ASSERT_NOT_NULL(physical);
    /* Should be SeqScan with a filter predicate (merged) */
    EXPECT_EQ(POP_SEQ_SCAN, physical->op);
    EXPECT_NOT_NULL(physical->seq_scan.predicate);

    physical_node_destroy(physical);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(planner, value_bridge) {
    ast_value_t vi = { .type = VALUE_INT, .int_val = 42 };
    ast_value_t vs = { .type = VALUE_STRING };
    strcpy(vs.str_val, "hello");
    ast_value_t vn = { .type = VALUE_NULL };

    value_t si = ast_value_to_storage(&vi);
    EXPECT_EQ(TYPE_INTEGER, si.type);
    EXPECT_EQ(42, si.val.int_val);

    value_t ss = ast_value_to_storage(&vs);
    EXPECT_EQ(TYPE_VARCHAR, ss.type);
    EXPECT_STR_EQ("hello", ss.val.varchar);

    value_t sn = ast_value_to_storage(&vn);
    EXPECT_EQ(TYPE_NULL, sn.type);

    value_destroy(&si);
    value_destroy(&ss);
    value_destroy(&sn);
}

TEST(planner, column_type_conversion) {
    EXPECT_EQ(TYPE_INTEGER, ast_col_type_to_storage(COL_TYPE_INTEGER));
    EXPECT_EQ(TYPE_FLOAT,   ast_col_type_to_storage(COL_TYPE_FLOAT));
    EXPECT_EQ(TYPE_VARCHAR, ast_col_type_to_storage(COL_TYPE_VARCHAR));
    EXPECT_EQ(TYPE_BOOLEAN, ast_col_type_to_storage(COL_TYPE_BOOLEAN));
}

TEST(planner, explain_output) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("SELECT * FROM users WHERE id = 1 LIMIT 10");
    ASSERT_NOT_NULL(stmt);

    physical_node_t* physical = planner_plan(&env.planner, stmt);
    ASSERT_NOT_NULL(physical);
    /* Just verify it doesn't crash */
    explain_print_plan(physical);

    physical_node_destroy(physical);
    ast_destroy_stmt(stmt);
    teardown_env(&env);
}

TEST(planner, ddl_returns_null) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    stmt_t* stmt = parse_sql("CREATE TABLE test (id INTEGER PRIMARY KEY)");
    ASSERT_NOT_NULL(stmt);

    logical_node_t* logical = planner_plan_logical(&env.planner, stmt);
    EXPECT_NULL(logical);  /* DDL should return NULL */

    ast_destroy_stmt(stmt);
    teardown_env(&env);
}
