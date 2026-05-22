#include "src/tools/sql_runner.h"
#include "src/common/mem.h"
#include "src/common/error.h"
#include "src/parser/lexer.h"
#include "src/parser/parser.h"
#include "src/parser/ast.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void sql_runner_init(sql_runner_t* runner) {
    runner->echo = 0;
    runner->stop_on_error = 1;
    runner->error_count = 0;
}

void sql_runner_destroy(sql_runner_t* runner) {
    (void)runner;
}

int sql_runner_execute_string(sql_runner_t* runner, const char* sql) {
    if (!sql || !*sql) return 0;

    /* Lex */
    lexer_t lex;
    lexer_init(&lex, sql);
    token_t* tokens = NULL;
    int token_count = 0;
    int rc = lexer_tokenize(&lex, &tokens, &token_count);
    if (rc != 0) {
        fprintf(stderr, "Lexer error: failed to tokenize\n");
        runner->error_count++;
        return -1;
    }

    /* Parse */
    parser_t parser;
    parser_init(&parser, tokens, token_count);

    int stmt_count = 0;
    stmt_t** stmts = parser_parse_all(&parser, &stmt_count);

    if (parser.has_error) {
        fprintf(stderr, "Parse error: %s\n", parser.error_msg);
        runner->error_count++;
        db_free(tokens);
        if (stmts) {
            for (int i = 0; i < stmt_count; i++)
                ast_destroy_stmt(stmts[i]);
            db_free(stmts);
        }
        return -1;
    }

    /* Execute each statement (for now, just print the AST) */
    for (int i = 0; i < stmt_count; i++) {
        if (runner->echo) {
            printf("-- Statement %d:\n", i + 1);
            ast_print_stmt(stmts[i], 1);
        }
        /* TODO: hand off to execution engine in Phase 6/7 */
        ast_destroy_stmt(stmts[i]);
    }

    db_free(stmts);
    db_free(tokens);
    return 0;
}

int sql_runner_execute_file(sql_runner_t* runner, const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file '%s'\n", path);
        runner->error_count++;
        return -1;
    }

    /* Read entire file into buffer */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* buf = db_malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, fp);
    buf[nread] = '\0';
    fclose(fp);

    int result = sql_runner_execute_string(runner, buf);

    db_free(buf);
    return result;
}
