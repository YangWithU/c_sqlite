#include "lexer.h"
#include "src/common/mem.h"
#include "src/common/macros.h"
#include <ctype.h>
#include <string.h>

/* Keyword lookup table */
typedef struct {
    const char*   keyword;
    token_type_t  type;
} keyword_entry_t;

static keyword_entry_t KEYWORDS[] = {
    {"ADD",          TK_ADD},
    {"ALL",          TK_ALL},
    {"ALTER",        TK_ALTER},
    {"AND",          TK_AND},
    {"ASC",          TK_ASC},
    {"AVG",          TK_AVG},
    {"BEGIN",        TK_BEGIN},
    {"BETWEEN",      TK_BETWEEN},
    {"BOOLEAN",      TK_BOOLEAN},
    {"BY",           TK_BY},
    {"CASCADE",      TK_CASCADE},
    {"CHECK",        TK_CHECK},
    {"COLUMN",       TK_COLUMN},
    {"COMMIT",       TK_COMMIT},
    {"COMMITTED",    TK_COMMITTED},
    {"COUNT",        TK_COUNT},
    {"CREATE",       TK_CREATE},
    {"CROSS",        TK_CROSS},
    {"DEFAULT",      TK_DEFAULT},
    {"DELETE",       TK_DELETE},
    {"DESC",         TK_DESC},
    {"DISTINCT",     TK_DISTINCT},
    {"DROP",         TK_DROP},
    {"EXISTS",       TK_EXISTS},
    {"EXPLAIN",      TK_EXPLAIN},
    {"FLOAT",        TK_FLOAT},
    {"FOREIGN",      TK_FOREIGN},
    {"FROM",         TK_FROM},
    {"FULL",         TK_FULL},
    {"GROUP",        TK_GROUP},
    {"HAVING",       TK_HAVING},
    {"IF",           TK_IF},
    {"IN",           TK_IN},
    {"INDEX",        TK_INDEX},
    {"INNER",        TK_INNER},
    {"INSERT",       TK_INSERT},
    {"INTO",         TK_INTO},
    {"IS",           TK_IS},
    {"ISOLATION",    TK_ISOLATION},
    {"JOIN",         TK_JOIN},
    {"KEY",          TK_KEY},
    {"LEFT",         TK_LEFT},
    {"LEVEL",        TK_LEVEL},
    {"LIKE",         TK_LIKE},
    {"LIMIT",        TK_LIMIT},
    {"MAX",          TK_MAX},
    {"MIN",          TK_MIN},
    {"NOT",          TK_NOT},
    {"NULL",         TK_NULL},
    {"OFFSET",       TK_OFFSET},
    {"ON",           TK_ON},
    {"ONLY",         TK_ONLY},
    {"OR",           TK_OR},
    {"ORDER",        TK_ORDER},
    {"OUTER",        TK_OUTER},
    {"PRIMARY",      TK_PRIMARY},
    {"READ",         TK_READ},
    {"REFERENCES",   TK_REFERENCES},
    {"REPEATABLE",   TK_REPEATABLE},
    {"RIGHT",        TK_RIGHT},
    {"ROLLBACK",     TK_ROLLBACK},
    {"SELECT",       TK_SELECT},
    {"SERIALIZABLE", TK_SERIALIZABLE},
    {"SET",          TK_SET},
    {"SUM",          TK_SUM},
    {"TABLE",        TK_TABLE},
    {"TRANSACTION",  TK_TRANSACTION},
    {"TRUE",         TK_TRUE},
    {"FALSE",        TK_FALSE},
    {"UNION",        TK_UNION},
    {"UNIQUE",       TK_UNIQUE},
    {"UNCOMMITTED",  TK_UNCOMMITTED},
    {"UPDATE",       TK_UPDATE},
    {"VALUES",       TK_VALUES},
    {"VARCHAR",      TK_VARCHAR},
    {"WHERE",        TK_WHERE},
    {"INTEGER",      TK_INTEGER},
};
#define KEYWORD_COUNT (sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))

static token_type_t lookup_keyword(const char* word, size_t len) {
    /* Keywords are uppercase, compare case-insensitively */
    for (int i = 0; i < (int)KEYWORD_COUNT; i++) {
        if (strlen(KEYWORDS[i].keyword) == len) {
            int match = 1;
            for (size_t j = 0; j < len; j++) {
                if (toupper((unsigned char)word[j]) != KEYWORDS[i].keyword[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) return KEYWORDS[i].type;
        }
    }
    return TK_IDENTIFIER;
}

static char peek(lexer_t* lex) {
    if (lex->pos >= lex->length) return '\0';
    return lex->source[lex->pos];
}

static char peek_next(lexer_t* lex) {
    if (lex->pos + 1 >= lex->length) return '\0';
    return lex->source[lex->pos + 1];
}

static char advance(lexer_t* lex) {
    char c = lex->source[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->column = 1;
    } else {
        lex->column++;
    }
    return c;
}

static void skip_whitespace(lexer_t* lex) {
    while (lex->pos < lex->length) {
        char c = peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
        } else {
            break;
        }
    }
}

static void skip_line_comment(lexer_t* lex) {
    while (lex->pos < lex->length && peek(lex) != '\n')
        advance(lex);
}

static void skip_block_comment(lexer_t* lex) {
    while (lex->pos < lex->length) {
        if (peek(lex) == '*' && peek_next(lex) == '/') {
            advance(lex); /* consume * */
            advance(lex); /* consume / */
            break;
        }
        advance(lex);
    }
}

static token_t make_token(token_type_t type, const char* start, size_t len,
                          int line, int col) {
    token_t tok;
    tok.type = type;
    tok.line = line;
    tok.column = col;
    size_t copy_len = len < 255 ? len : 255;
    memcpy(tok.lexeme, start, copy_len);
    tok.lexeme[copy_len] = '\0';
    return tok;
}

void lexer_init(lexer_t* lex, const char* source) {
    lex->source = source;
    lex->length = strlen(source);
    lex->pos = 0;
    lex->line = 1;
    lex->column = 1;
}

int lexer_tokenize(lexer_t* lex, token_t** out_tokens, int* out_count) {
    /* We'll use a simple growing array */
    int capacity = 64;
    int count = 0;
    token_t* tokens = db_malloc(sizeof(token_t) * (size_t)capacity);
    if (!tokens) return -1;

    while (lex->pos < lex->length) {
        skip_whitespace(lex);
        if (lex->pos >= lex->length) break;

        int line = lex->line;
        int col = lex->column;
        char c = peek(lex);

        /* Comments */
        if (c == '-' && peek_next(lex) == '-') {
            skip_line_comment(lex);
            continue;
        }
        if (c == '/' && peek_next(lex) == '*') {
            advance(lex); /* / */
            advance(lex); /* * */
            skip_block_comment(lex);
            continue;
        }

        /* Grow array if needed */
        if (count >= capacity) {
            capacity *= 2;
            token_t* new_tokens = db_realloc(tokens, sizeof(token_t) * (size_t)capacity);
            if (!new_tokens) { db_free(tokens); return -1; }
            tokens = new_tokens;
        }

        /* Identifiers and keywords */
        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = lex->pos;
            while (lex->pos < lex->length &&
                   (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')) {
                advance(lex);
            }
            size_t len = lex->pos - start;
            token_type_t type = lookup_keyword(lex->source + start, len);
            tokens[count++] = make_token(type, lex->source + start, len, line, col);
            continue;
        }

        /* Number literals */
        if (isdigit((unsigned char)c)) {
            size_t start = lex->pos;
            token_type_t type = TK_INT_LITERAL;
            while (lex->pos < lex->length && isdigit((unsigned char)peek(lex)))
                advance(lex);
            if (lex->pos < lex->length && peek(lex) == '.') {
                type = TK_FLOAT_LITERAL;
                advance(lex); /* consume . */
                while (lex->pos < lex->length && isdigit((unsigned char)peek(lex)))
                    advance(lex);
            }
            /* Scientific notation */
            if (lex->pos < lex->length && (peek(lex) == 'e' || peek(lex) == 'E')) {
                type = TK_FLOAT_LITERAL;
                advance(lex);
                if (lex->pos < lex->length && (peek(lex) == '+' || peek(lex) == '-'))
                    advance(lex);
                while (lex->pos < lex->length && isdigit((unsigned char)peek(lex)))
                    advance(lex);
            }
            size_t len = lex->pos - start;
            tokens[count++] = make_token(type, lex->source + start, len, line, col);
            continue;
        }

        /* String literals */
        if (c == '\'') {
            advance(lex); /* opening quote */
            size_t start = lex->pos;
            while (lex->pos < lex->length && peek(lex) != '\'') {
                if (peek(lex) == '\\' && peek_next(lex) == '\'') {
                    advance(lex); /* skip escape */
                }
                advance(lex);
            }
            size_t len = lex->pos - start;
            if (lex->pos < lex->length)
                advance(lex); /* closing quote */
            tokens[count++] = make_token(TK_STRING_LITERAL, lex->source + start, len, line, col);
            continue;
        }

        /* Two-character operators */
        if (lex->pos + 1 < lex->length) {
            char two[3] = { c, peek_next(lex), '\0' };
            token_type_t two_type = TK_ERROR;
            if (strcmp(two, "<>") == 0) two_type = TK_NOT_EQUAL;
            else if (strcmp(two, "<=") == 0) two_type = TK_LESS_EQUAL;
            else if (strcmp(two, ">=") == 0) two_type = TK_GREATER_EQUAL;
            else if (strcmp(two, "!=") == 0) two_type = TK_NOT_EQUAL;

            if (two_type != TK_ERROR) {
                tokens[count++] = make_token(two_type, lex->source + lex->pos, 2, line, col);
                advance(lex);
                advance(lex);
                continue;
            }
        }

        /* Single-character tokens */
        advance(lex); /* consume the character */
        token_type_t type;
        switch (c) {
            case '=':  type = TK_EQUAL;       break;
            case '<':  type = TK_LESS;         break;
            case '>':  type = TK_GREATER;      break;
            case '+':  type = TK_PLUS;         break;
            case '-':  type = TK_MINUS;        break;
            case '*':  type = TK_STAR;         break;
            case '/':  type = TK_SLASH;        break;
            case '(':  type = TK_LEFT_PAREN;   break;
            case ')':  type = TK_RIGHT_PAREN;  break;
            case ',':  type = TK_COMMA;        break;
            case ';':  type = TK_SEMICOLON;    break;
            case '.':  type = TK_DOT;          break;
            default:   type = TK_ERROR;        break;
        }
        char ch[2] = { c, '\0' };
        tokens[count++] = make_token(type, ch, 1, line, col);
    }

    /* Add EOF token */
    if (count >= capacity) {
        capacity++;
        token_t* new_tokens = db_realloc(tokens, sizeof(token_t) * (size_t)capacity);
        if (!new_tokens) { db_free(tokens); return -1; }
        tokens = new_tokens;
    }
    token_t eof;
    eof.type = TK_EOF;
    eof.lexeme[0] = '\0';
    eof.line = lex->line;
    eof.column = lex->column;
    tokens[count++] = eof;

    *out_tokens = tokens;
    *out_count = count;
    return 0;
}

void lexer_destroy(lexer_t* lex) {
    (void)lex;
}
