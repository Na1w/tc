#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/lexer.h"
#include "src/parser.h"
#include "src/sema.h"

static uint32_t djb2(const char *s) {
    uint32_t h = 5381;
    while (*s) { h = h * 33 + (uint32_t)(unsigned char)*s; s++; }
    return h;
}

int main(void) {
    const char *src =
        "int main(void) {\n"
        "    int x = 42;\n"
        "    return x;\n"
        "}\n";

    Lexer *lex = lexer_create(src, (int)strlen(src));
    Parser *p  = parser_create(lex);
    Node *ast  = parse_program(p);

    SemaContext *sema = sema_create();
    int ok = (sema_analyze(sema, ast) == 0);

    /* Walk resolved_symbols to find 'x' */
    for (int i = 0; i < sema->sym_count; i++) {
        Symbol *s = sema->resolved_symbols[i];
        if (s && strcmp(s->name, "x") == 0) {
            printf("Found 'x': hash=%u kind=%d\n", s->hash, (int)s->kind);
        }
    }

    /* Try lookup with our djb2 */
    uint32_t our_hash = djb2("x");
    printf("Our djb2('x') = %u\n", our_hash);
    Symbol *sym = sema_lookup(sema, our_hash, "x");
    printf("Lookup result: %s\n", sym ? "FOUND" : "NOT FOUND");
    if (sym) {
        printf("  kind = %d (SYM_VAR=%d)\n", (int)sym->kind, SYM_VAR);
    }

    sema_destroy(sema);
    node_destroy(ast);
    parser_destroy(p);
    lexer_destroy(lex);
    return 0;
}
