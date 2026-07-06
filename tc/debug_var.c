#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "src/lexer.h"
#include "src/parser.h"
#include "src/sema.h"

int main(void) {
    /* Start with the simplest possible program */
    const char *src = "int main(void) { return 0; }\n";

    printf("Creating lexer...\n");
    Lexer *lex = lexer_create(src, strlen(src));
    printf("Creating parser...\n");
    Parser *p = parser_create(lex);
    printf("Parsing...\n");
    Node *ast = parse_program(p);
    
    printf("Parse error: %d\n", p->error);
    if (!ast) { printf("AST is NULL\n"); return 1; }
    
    printf("AST kind: %d\n", ast->kind);
    
    printf("Creating sema...\n");
    SemaContext *sema = sema_create();
    if (!sema) { printf("sema_create failed\n"); return 1; }
    
    printf("Before sema_analyze\n");
    fflush(stdout);
    int ok = (sema_analyze(sema, ast) == 0);
    printf("Sema ok: %d, errors: %d\n", ok, sema->error_count);
    
    printf("Symbols resolved: %d\n", sema->sym_count);
    for (int i = 0; i < sema->sym_count; i++) {
        Symbol *s = sema->resolved_symbols[i];
        printf("  sym[%d]: name='%s' hash=%u kind=%d\n", i, s->name, s->hash, s->kind);
    }
    
    sema_destroy(sema);
    parser_destroy(p);
    lexer_destroy(lex);
    return 0;
}
