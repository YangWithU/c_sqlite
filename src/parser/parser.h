#pragma once

#include "token.h"
#include "ast.h"

typedef struct {
    token_t* tokens;
    int      token_count;
    int      pos;
    char     error_msg[MAX_ERROR_MSG];
    int      error_line;
    int      error_column;
    int      has_error;
} parser_t;

void     parser_init(parser_t* p, token_t* tokens, int count);
void     parser_destroy(parser_t* p);
stmt_t*  parser_parse(parser_t* p);

/* Parse multiple statements separated by semicolons */
stmt_t** parser_parse_all(parser_t* p, int* out_count);
