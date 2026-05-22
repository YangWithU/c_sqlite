#include "src/storage/disk_manager.h"
#include "src/buffer/buffer_pool_manager.h"
#include "src/catalog/catalog.h"
#include "src/executor/execution_engine.h"
#include "src/tools/sql_runner.h"
#include "src/tools/repl.h"
#include "src/common/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS] [DATABASE]\n", prog);
    printf("Options:\n");
    printf("  -f FILE    Execute SQL from FILE and exit\n");
    printf("  -s SQL     Execute SQL string and exit\n");
    printf("  -h         Show this help\n");
    printf("\n");
    printf("If no -f or -s is given, starts an interactive REPL.\n");
}

int main(int argc, char* argv[]) {
    const char* db_path = "minisqlite.db";
    const char* sql_file = NULL;
    const char* sql_string = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            sql_file = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            sql_string = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            db_path = argv[i];
        }
    }

    /* Initialize storage stack */
    disk_manager_t dm;
    if (disk_manager_open(&dm, db_path) != DB_OK) {
        fprintf(stderr, "Error: cannot open database '%s'\n", db_path);
        return 1;
    }

    buffer_pool_manager_t bpm;
    if (bpm_init(&bpm, BUFFER_POOL_SIZE, &dm) != DB_OK) {
        fprintf(stderr, "Error: cannot initialize buffer pool\n");
        disk_manager_close(&dm);
        return 1;
    }

    catalog_t cat;
    if (catalog_init(&cat, &bpm) != DB_OK) {
        fprintf(stderr, "Error: cannot initialize catalog\n");
        bpm_destroy(&bpm);
        disk_manager_close(&dm);
        return 1;
    }

    execution_engine_t engine;
    if (execution_engine_init(&engine, &cat, &bpm) != DB_OK) {
        fprintf(stderr, "Error: cannot initialize execution engine\n");
        catalog_destroy(&cat);
        bpm_destroy(&bpm);
        disk_manager_close(&dm);
        return 1;
    }

    int result = 0;

    if (sql_file) {
        /* Execute SQL from file */
        sql_runner_t runner;
        sql_runner_init(&runner, &engine);
        result = sql_runner_execute_file(&runner, sql_file);
        sql_runner_destroy(&runner);
    } else if (sql_string) {
        /* Execute SQL from command line */
        sql_runner_t runner;
        sql_runner_init(&runner, &engine);
        result = sql_runner_execute_string(&runner, sql_string);
        sql_runner_destroy(&runner);
    } else {
        /* Interactive REPL */
        repl_t repl;
        repl_init(&repl, &engine, db_path);
        repl_run(&repl);
    }

    /* Clean up */
    execution_engine_destroy(&engine);
    catalog_destroy(&cat);
    bpm_destroy(&bpm);
    disk_manager_close(&dm);

    return result;
}
