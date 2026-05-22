#include "xtest.h"
#include "src/tools/sql_runner.h"
#include "src/executor/execution_engine.h"
#include "src/storage/disk_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/catalog/catalog.h"
#include "src/common/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char temp_db_path[256];

static void cleanup_temp(void) {
    if (temp_db_path[0]) unlink(temp_db_path);
}

static const char* make_temp_path(void) {
    static int init = 0;
    if (!init) { atexit(cleanup_temp); init = 1; }
    snprintf(temp_db_path, sizeof(temp_db_path),
             "/tmp/test_sql_runner_%d.db", (int)getpid());
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
    return 0;
}

static void teardown_env(test_env_t* env) {
    execution_engine_destroy(&env->engine);
    catalog_destroy(&env->cat);
    bpm_destroy(&env->bpm);
    disk_manager_close(&env->dm);
    unlink(make_temp_path());
}

TEST(sql_runner, execute_string) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    sql_runner_t runner;
    sql_runner_init(&runner, &env.engine);
    runner.echo = 1;

    int rc = sql_runner_execute_string(&runner,
        "CREATE TABLE t (id INTEGER); SELECT * FROM t;");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(runner.error_count, 0);

    sql_runner_destroy(&runner);
    teardown_env(&env);
}

TEST(sql_runner, execute_file) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    sql_runner_t runner;
    sql_runner_init(&runner, &env.engine);
    runner.echo = 1;

    int rc = sql_runner_execute_file(&runner, "sql/test_basic.sql");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(runner.error_count, 0);

    sql_runner_destroy(&runner);
    teardown_env(&env);
}

TEST(sql_runner, syntax_error) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    sql_runner_t runner;
    sql_runner_init(&runner, &env.engine);
    runner.stop_on_error = 0;

    int rc = sql_runner_execute_string(&runner, "SELECTT * FROM");
    EXPECT_NE(rc, 0);
    EXPECT_EQ(runner.error_count, 1);

    sql_runner_destroy(&runner);
    teardown_env(&env);
}

TEST(sql_runner, empty_input) {
    test_env_t env;
    ASSERT_EQ(0, setup_env(&env));

    sql_runner_t runner;
    sql_runner_init(&runner, &env.engine);

    int rc = sql_runner_execute_string(&runner, "");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(runner.error_count, 0);

    rc = sql_runner_execute_string(&runner, NULL);
    EXPECT_EQ(rc, 0);

    sql_runner_destroy(&runner);
    teardown_env(&env);
}
