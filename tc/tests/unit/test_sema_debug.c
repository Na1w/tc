#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/lexer.h"
#include "src/parser.h"
#include "src/sema.h"

static uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    while (*s) { h = h * 33 + (uint32_t)(unsigned char)*s; s++; }
    return h;
}

static SemaContext *parse_and_sema(const char *source, int *sema_ok)
{
    Lexer *lex = lexer_create(source, (int)strlen(source));
    Parser *p  = parser_create(lex);
    Node *ast  = parse_program(p);

    if (!ast || p->error) {
        if (sema_ok) *sema_ok = 0;
        if (ast) node_destroy(ast);
        parser_destroy(p);
        lexer_destroy(lex);
        return NULL;
    }

    SemaContext *sema = sema_create();
    int ok = (sema_analyze(sema, ast) == 0);
    if (sema_ok) *sema_ok = ok;

    (void)ast;
    parser_destroy(p);
    lexer_destroy(lex);
    return sema;
}

int main(void) {
    /* Run test_create_destroy first */
    {
        SemaContext *s = sema_create();
        sema_destroy(s);
        printf("PASS: test_create_destroy\n");
    }

    /* Run test_builtin_types */
    {
        SemaContext *s = sema_create();
        Type *t = sema_get_type(s, sema_int_type(s));
        printf("int type: kind=%d size=%d\n", t->kind, t->size);
        t = sema_get_type(s, sema_void_type(s));
        printf("void type: kind=%d size=%d\n", t->kind, t->size);
        sema_destroy(s);
        printf("PASS: test_builtin_types\n");
    }

    /* Run test_unsigned_types */
    {
        SemaContext *s = sema_create();
        Type *t = sema_get_type(s, s->type_id_uint);
        printf("uint: kind=%d size=%d sign=%d\n", t->kind, t->size, t->sign);
        t = sema_get_type(s, s->type_id_ulong);
        printf("ulong: kind=%d size=%d sign=%d\n", t->kind, t->size, t->sign);
        sema_destroy(s);
        printf("PASS: test_unsigned_types\n");
    }

    /* Run test_var_decl */
    {
        const char *src =
            "int main(void) {\n"
            "    int x = 42;\n"
            "    return x;\n"
            "}\n";
        int ok = 0;
        SemaContext *s = parse_and_sema(src, &ok);
        printf("parse_and_sema: s=%p ok=%d errors=%d\n",
               (void*)s, ok, s ? s->error_count : -1);
        if (s) {
            printf("sym_count=%d\n", s->sym_count);
            for (int i = 0; i < s->sym_count; i++) {
                Symbol *sym = s->resolved_symbols[i];
                printf("  sym[%d]: name='%s' hash=%u kind=%d\n",
                       i, sym ? sym->name : "NULL",
                       sym ? sym->hash : 0,
                       sym ? (int)sym->kind : -1);
            }
            uint32_t h = djb2("x");
            printf("djb2('x')=%u\n", h);
            Symbol *sym = sema_lookup(s, h, "x");
            printf("lookup result: %p kind=%d SYM_VAR=%d\n",
                   (void*)sym, sym ? (int)sym->kind : -1, SYM_VAR);
            sema_destroy(s);
        }
    }

    return 0;
}
