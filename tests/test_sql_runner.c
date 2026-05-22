#include "xtest.h"
#include "src/tools/sql_runner.h"
#include "src/common/mem.h"

TEST(sql_runner, execute_string) {
    sql_runner_t runner;
    sql_runner_init(&runner);
    runner.echo = 1;

    int rc = sql_runner_execute_string(&runner,
        "CREATE TABLE t (id INTEGER); SELECT * FROM t;");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(runner.error_count, 0);

    sql_runner_destroy(&runner);
}

TEST(sql_runner, execute_file) {
    sql_runner_t runner;
    sql_runner_init(&runner);
    runner.echo = 1;

    int rc = sql_runner_execute_file(&runner, "sql/test_basic.sql");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(runner.error_count, 0);

    sql_runner_destroy(&runner);
}

TEST(sql_runner, syntax_error) {
    sql_runner_t runner;
    sql_runner_init(&runner);
    runner.stop_on_error = 0;

    int rc = sql_runner_execute_string(&runner, "SELECTT * FROM");
    EXPECT_NE(rc, 0);
    EXPECT_EQ(runner.error_count, 1);

    sql_runner_destroy(&runner);
}

TEST(sql_runner, empty_input) {
    sql_runner_t runner;
    sql_runner_init(&runner);

    int rc = sql_runner_execute_string(&runner, "");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(runner.error_count, 0);

    rc = sql_runner_execute_string(&runner, NULL);
    EXPECT_EQ(rc, 0);

    sql_runner_destroy(&runner);
}
