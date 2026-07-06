#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "src/lexer.h"
#include "src/parser.h"
#include "src/sema.h"

int main(void) {
    const char *src = "int main(void) { return 0; }\n";

    Lexer *lex = lexer_create(src, strlen(src));
    Parser *p = parser_create(lex);
    Node *ast = parse_program(p);
    
    SemaContext *sema = sema_create();
    int ok = (sema_analyze(sema, ast) == 0);
    printf("Sema ok: %d, errors: %d\n", ok, sema->error_count);
    printf("Symbols resolved: %d\n", sema->sym_count);
    printf("resolved_symbols ptr: %p\n", sema->resolved_symbols);
    fflush(stdout);
    
    /* Check if resolved_symbols[0] is valid */
    if (sema->sym_count > 0) {
        printf("resolved_symbols[0] = %p\n", sema->resolved_symbols[0]);
        Symbol *s = sema->resolved_symbols[0];
        printf("s->name = %p\n", (void*)s->name);
        if (s->name) printf("s->name = '%s'\n", s->name);
    }
    
    sema_destroy(sema);
    parser_destroy(p);
    lexer_destroy(lex);
    return 0;
}
