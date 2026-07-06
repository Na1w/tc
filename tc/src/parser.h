#ifndef TC_PARSER_H
#define TC_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct Parser {
    Lexer *lexer;
    Token current;
    int error;
    Node **string_pool;
    int string_count;
    int string_cap;
} Parser;

Parser* parser_create(Lexer *lex);
void parser_destroy(Parser *parser);
Node* parse_program(Parser *parser);
const char* parser_error(Parser *p, const char *fmt, ...);

#endif
