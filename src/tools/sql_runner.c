#include "src/tools/sql_runner.h"
#include "src/common/mem.h"
#include "src/common/error.h"
#include "src/storage/tuple.h"
#include "src/storage/value.h"
#include "src/parser/lexer.h"
#include "src/parser/parser.h"
#include "src/parser/ast.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void sql_runner_init(sql_runner_t* runner, execution_engine_t* engine) {
    runner->engine = engine;
    runner->echo = 0;
    runner->stop_on_error = 1;
    runner->error_count = 0;
}

void sql_runner_destroy(sql_runner_t* runner) {
    (void)runner;
}

/* Print a value for display */
static void print_value(const value_t* v) {
    switch (v->type) {
    case TYPE_INTEGER: printf("%ld", (long)v->val.int_val); break;
    case TYPE_FLOAT:   printf("%g", v->val.float_val); break;
    case TYPE_BOOLEAN: printf("%s", v->val.bool_val ? "true" : "false"); break;
    case TYPE_VARCHAR: printf("%s", v->val.varchar); break;
    case TYPE_NULL:    printf("NULL"); break;
    default:           printf("?"); break;
    }
}

/* Print result tuples in a simple format */
static void print_results(vector_t* tuples, const schema_t* schema) {
    if (!tuples || vector_size(tuples) == 0) return;

    /* Print header */
    if (schema) {
        for (int i = 0; i < schema->num_columns; i++) {
            if (i > 0) printf("\t");
            printf("%s", schema->columns[i].name);
        }
        printf("\n");
    }

    /* Print rows */
    size_t n = vector_size(tuples);
    for (size_t i = 0; i < n; i++) {
        tuple_t* t = (tuple_t*)vector_at(tuples, i);
        for (int j = 0; j < t->num_values; j++) {
            if (j > 0) printf("\t");
            const value_t* v = tuple_get_value(t, j);
            if (v) print_value(v);
            else printf("NULL");
        }
        printf("\n");
    }
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

    /* Execute each statement */
    for (int i = 0; i < stmt_count; i++) {
        if (runner->echo) {
            printf("-- Statement %d:\n", i + 1);
        }

        vector_t tuples;
        vector_init(&tuples, sizeof(tuple_t));
        schema_t* out_schema = NULL;
        int affected = 0;

        rc = execution_engine_execute(runner->engine, stmts[i],
                                       &tuples, &out_schema, &affected);
        if (rc != DB_OK) {
            fprintf(stderr, "Execution error: %d\n", rc);
            runner->error_count++;
            if (runner->stop_on_error) {
                /* Clean up remaining stmts */
                for (int j = i; j < stmt_count; j++)
                    ast_destroy_stmt(stmts[j]);
                db_free(stmts);
                db_free(tokens);
                return -1;
            }
        } else {
            switch (stmts[i]->type) {
            case STMT_SELECT:
                print_results(&tuples, out_schema);
                printf("(%zu rows)\n", vector_size(&tuples));
                break;
            case STMT_INSERT:
                printf("INSERT %d\n", affected);
                break;
            case STMT_UPDATE:
                printf("UPDATE %d\n", affected);
                break;
            case STMT_DELETE:
                printf("DELETE %d\n", affected);
                break;
            case STMT_CREATE_TABLE:
                printf("Table created\n");
                break;
            case STMT_DROP_TABLE:
                printf("Table dropped\n");
                break;
            case STMT_CREATE_INDEX:
                printf("Index created\n");
                break;
            case STMT_DROP_INDEX:
                printf("Index dropped\n");
                break;
            case STMT_EXPLAIN:
                /* EXPLAIN already printed the plan */
                break;
            default:
                break;
            }
        }

        /* Clean up tuples */
        size_t ntuples = vector_size(&tuples);
        for (size_t t = 0; t < ntuples; t++) {
            tuple_t* tp = (tuple_t*)vector_at(&tuples, t);
            tuple_destroy(tp);
        }
        vector_destroy(&tuples);
        if (out_schema) {
            schema_destroy(out_schema);
            db_free(out_schema);
        }

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
