#pragma once

#include "token.h"
#include <stddef.h>

typedef struct {
    const char* source;
    size_t      pos;
    size_t      length;
    int         line;
    int         column;
} lexer_t;

void  lexer_init(lexer_t* lex, const char* source);
int   lexer_tokenize(lexer_t* lex, token_t** out_tokens, int* out_count);
void  lexer_destroy(lexer_t* lex);
