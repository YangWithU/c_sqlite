#include "src/tools/repl.h"
#include "src/tools/sql_runner.h"
#include "src/catalog/catalog.h"
#include "src/common/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Execute a dot-command (.help, .tables, .schema, .quit, etc.) */
static int handle_dot_command(repl_t* repl, const char* cmd) {
    if (strcmp(cmd, ".quit") == 0 || strcmp(cmd, ".exit") == 0) {
        repl->running = 0;
        return 0;
    }

    if (strcmp(cmd, ".help") == 0) {
        printf("Available commands:\n");
        printf("  .help       Show this help\n");
        printf("  .tables     List all tables\n");
        printf("  .schema     Show all table schemas\n");
        printf("  .quit       Exit\n");
        printf("  .read FILE  Execute SQL from file\n");
        return 0;
    }

    if (strcmp(cmd, ".tables") == 0) {
        vector_t names;
        vector_init(&names, sizeof(char*));
        catalog_list_tables(repl->engine->catalog, &names);
        for (size_t i = 0; i < vector_size(&names); i++) {
            char* name = *(char**)vector_at(&names, i);
            printf("%s\n", name);
            db_free(name);
        }
        vector_destroy(&names);
        return 0;
    }

    if (strcmp(cmd, ".schema") == 0) {
        vector_t names;
        vector_init(&names, sizeof(char*));
        catalog_list_tables(repl->engine->catalog, &names);
        for (size_t i = 0; i < vector_size(&names); i++) {
            char* name = *(char**)vector_at(&names, i);
            table_meta_t meta;
            if (catalog_get_table(repl->engine->catalog, name, &meta) == DB_OK) {
                schema_t schema;
                if (catalog_get_schema(repl->engine->catalog, meta.table_id, &schema) == DB_OK) {
                    printf("CREATE TABLE %s (", name);
                    for (int j = 0; j < schema.num_columns; j++) {
                        if (j > 0) printf(", ");
                        printf("%s %s", schema.columns[j].name,
                               type_id_name(schema.columns[j].type));
                    }
                    printf(");\n");
                    schema_destroy(&schema);
                }
            }
            db_free(name);
        }
        vector_destroy(&names);
        return 0;
    }

    if (strncmp(cmd, ".read ", 6) == 0) {
        const char* path = cmd + 6;
        sql_runner_t runner;
        sql_runner_init(&runner, repl->engine);
        sql_runner_execute_file(&runner, path);
        sql_runner_destroy(&runner);
        return 0;
    }

    printf("Unknown command: %s\n", cmd);
    return 0;
}

void repl_init(repl_t* repl, execution_engine_t* engine, const char* db_path) {
    repl->engine = engine;
    repl->db_path = db_path;
    repl->running = 1;
}

void repl_run(repl_t* repl) {
    char line[4096];
    char sql_buf[65536];
    sql_buf[0] = '\0';

    sql_runner_t runner;
    sql_runner_init(&runner, repl->engine);

    printf("MiniSQLite v1.0\n");
    printf("Type .help for help, .quit to exit\n");

    while (repl->running) {
        printf("%s", sql_buf[0] ? "  ... > " : "minisqlite> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* Trim trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip empty lines */
        if (len == 0) continue;

        /* Check for dot-commands */
        if (line[0] == '.' && sql_buf[0] == '\0') {
            handle_dot_command(repl, line);
            continue;
        }

        /* Append to SQL buffer */
        if (sql_buf[0]) strcat(sql_buf, " ");
        strcat(sql_buf, line);

        /* Check if the statement is complete (ends with ;) */
        char* semi = strrchr(sql_buf, ';');
        if (semi) {
            /* Execute the accumulated SQL */
            *semi = '\0';  /* remove trailing semicolon for multi-statement */
            *semi = ';';   /* put it back */
            sql_runner_execute_string(&runner, sql_buf);
            sql_buf[0] = '\0';
        }
    }

    sql_runner_destroy(&runner);
    printf("Goodbye!\n");
}

void repl_stop(repl_t* repl) {
    repl->running = 0;
}
