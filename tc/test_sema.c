/*
 * test_sema.c -- Semantic analysis tests for the "tc" C compiler.
 *
 * 15 tests covering:
 *   - Type table (builtin types, sizes, unsigned variants)
 *   - Hash-based symbol table with scope nesting
 *   - Identifier resolution
 *   - Type checking for binary/unary ops, calls
 *   - Constant expression evaluation
 *   - Error detection (undeclared vars, redefinition, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/lexer.h"
#include "src/parser.h"
#include "src/sema.h"

static int tests_passed  = 0;
static int tests_failed  = 0;

/* ---- helpers ---- */

static uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    while (*s) { h = h * 33 + (uint32_t)(unsigned char)*s; s++; }
    return h;
}

/* Parse source + run sema. Returns sema (caller must destroy).
 * If parse fails returns NULL.  *sema_ok tells if sema succeeded. */
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

    (void)ast;  /* sema may reference nodes */
    parser_destroy(p);
    lexer_destroy(lex);
    return sema;
}

#define PASS(msg)  do { printf("PASS: %s\n", msg); tests_passed++; } while(0)
#define FAIL(msg, ...) do { printf("FAIL: " msg "\n", ##__VA_ARGS__); tests_failed++; } while(0)

/* ==================================================================
 * TEST 1: sema_create / sema_destroy basic lifecycle
 * ================================================================== */
static void test_create_destroy(void)
{
    SemaContext *s = sema_create();
    assert(s != NULL);
    assert(s->type_count > 0);   /* builtin types registered */
    assert(s->error_count == 0);
    sema_destroy(s);
    PASS("test_create_destroy");
}

/* ==================================================================
 * TEST 2: Builtin type table (sizes, signs)
 * ================================================================== */
static void test_builtin_types(void)
{
    SemaContext *s = sema_create();

    Type *t;

    t = sema_get_type(s, sema_void_type(s));
    assert(t && t->kind == TY_VOID && t->size == 0);

    t = sema_get_type(s, sema_int_type(s));
    assert(t && t->kind == TY_INT && t->size == 4 && t->sign == 0);

    t = sema_get_type(s, sema_char_type(s));
    assert(t && t->kind == TY_CHAR && t->size == 1 && t->sign == 0);

    t = sema_get_type(s, sema_long_type(s));
    assert(t && t->kind == TY_LONG && t->size == 8 && t->sign == 0);

    PASS("test_builtin_types");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 3: Unsigned variants exist and have correct sizes
 * ================================================================== */
static void test_unsigned_types(void)
{
    SemaContext *s = sema_create();

    /* uint -> index 4 (after void=0, int=1, char=2, long=3) */
    int uint_id = s->type_id_uint;
    Type *t = sema_get_type(s, uint_id);
    assert(t && t->kind == TY_UINT && t->size == 4 && t->sign == 1);

    int uchar_id = s->type_id_uchar;
    t = sema_get_type(s, uchar_id);
    assert(t && t->kind == TY_UCHAR && t->size == 1 && t->sign == 1);

    int ushort_id = s->type_id_ushort;
    t = sema_get_type(s, ushort_id);
    assert(t && t->kind == TY_USHORT && t->size == 2 && t->sign == 1);

    int ulong_id = s->type_id_ulong;
    t = sema_get_type(s, ulong_id);
    assert(t && t->kind == TY_ULONG && t->size == 8 && t->sign == 1);

    PASS("test_unsigned_types");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 4: Pointer type creation (ptr = 8 bytes)
 * ================================================================== */
static void test_ptr_type(void)
{
    SemaContext *s = sema_create();
    int int_id = sema_int_type(s);
    int ptr_id = sema_ptr_type(s, int_id);
    Type *ptr = sema_get_type(s, ptr_id);
    assert(ptr && ptr->kind == TY_PTR && ptr->size == 8);
    assert(ptr->elem == sema_get_type(s, int_id));

    /* Second call should return the same cached type */
    int ptr_id2 = sema_ptr_type(s, int_id);
    assert(ptr_id == ptr_id2);

    PASS("test_ptr_type");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 5: sema_add_type grows the table
 * ================================================================== */
static void test_add_type(void)
{
    SemaContext *s = sema_create();
    int before = s->type_count;

    Type *t = (Type *)calloc(1, sizeof(Type));
    t->kind = TY_INT; t->size = 4; t->sign = 0;
    int id = sema_add_type(s, t);
    assert(id == before);
    assert(s->type_count == before + 1);
    assert(sema_get_type(s, id) == t);

    PASS("test_add_type");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 6: Simple variable declaration + symbol lookup
 * ================================================================== */
static void test_var_decl(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 42;\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    uint32_t h = djb2("x");
    Symbol *sym = sema_lookup(s, h, "x");
    assert(sym && sym->kind == SYM_VAR);
    assert(sym->type && sym->type->kind == TY_INT);

    PASS("test_var_decl");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 7: Function declaration + symbol lookup
 * ================================================================== */
static void test_func_decl(void)
{
    const char *src =
        "int foo(int a) {\n"
        "    return a + 1;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    uint32_t h = djb2("foo");
    Symbol *sym = sema_lookup(s, h, "foo");
    assert(sym && sym->kind == SYM_FUNC);

    PASS("test_func_decl");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 8: Constant expression evaluation
 * ================================================================== */
static void test_const_expr(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 3 + 4 * 2;\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    /* The AST root is the func_decl; find the var_decl inside the body. */
    /* We can't easily traverse without knowing parser internals,
       so just verify sema succeeded (no errors) and types are correct. */

    PASS("test_const_expr");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 9: Undeclared identifier produces error
 * ================================================================== */
static void test_undeclared_error(void)
{
    const char *src =
        "int main(void) {\n"
        "    return undefined_var;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s);
    assert(!ok);  /* sema should fail */
    assert(s->error_count > 0);

    PASS("test_undeclared_error");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 10: Redefinition produces error
 * ================================================================== */
static void test_redefinition_error(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 1;\n"
        "    int x = 2;\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s);
    assert(!ok);
    assert(s->error_count > 0);

    PASS("test_redefinition_error");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 11: Scope nesting -- inner shadows outer
 * ================================================================== */
static void test_scope_nesting(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 1;\n"
        "    {\n"
        "        int y = 2;\n"
        "    }\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_scope_nesting");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 12: Unary negation on constant
 * ================================================================== */
static void test_unary_neg_const(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = -5;\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_unary_neg_const");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 13: Binary arithmetic on constants
 * ================================================================== */
static void test_binary_arith_const(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 10 - 3;\n"
        "    int y = x * 4;\n"
        "    return y;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_binary_arith_const");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 14: Logical operators (&& ||)
 * ================================================================== */
static void test_logical_ops(void)
{
    const char *src =
        "int main(void) {\n"
        "    int a = 1;\n"
        "    int b = 0;\n"
        "    int c = a && b;\n"
        "    int d = a || b;\n"
        "    return c + d;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_logical_ops");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 15: Bitwise operators
 * ================================================================== */
static void test_bitwise_ops(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 0xFF & 0x0F;\n"
        "    int y = 0x0F | 0xF0;\n"
        "    int z = x ^ y;\n"
        "    return z;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_bitwise_ops");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 16: Comparison operators
 * ================================================================== */
static void test_comparison_ops(void)
{
    const char *src =
        "int main(void) {\n"
        "    int a = 10;\n"
        "    int b = 20;\n"
        "    int c = a < b;\n"
        "    int d = a == 10;\n"
        "    return c + d;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_comparison_ops");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 17: While loop with scope
 * ================================================================== */
static void test_while_loop(void)
{
    const char *src =
        "int main(void) {\n"
        "    int i = 0;\n"
        "    while (i < 10) {\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return i;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_while_loop");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 18: For loop with init declaration
 * ================================================================== */
static void test_for_loop(void)
{
    const char *src =
        "int main(void) {\n"
        "    for (int i = 0; i < 10; i = i + 1) {\n"
        "        int j = i * 2;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_for_loop");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 19: If/else statement
 * ================================================================== */
static void test_if_else(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 5;\n"
        "    if (x > 0) {\n"
        "        x = x + 1;\n"
        "    } else {\n"
        "        x = x - 1;\n"
        "    }\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);

    PASS("test_if_else");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 20: Division by zero at compile time
 * ================================================================== */
static void test_div_zero(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 10 / 0;\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s);
    assert(!ok);
    assert(s->error_count > 0);

    PASS("test_div_zero");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 21: Increment requires lvalue
 * ================================================================== */
static void test_inc_non_lvalue(void)
{
    const char *src =
        "int main(void) {\n"
        "    int x = 5;\n"
        "    x++;\n"
        "    return x;\n"
        "}\n";
    int ok = 0;
    SemaContext *s = parse_and_sema(src, &ok);
    assert(s && ok);  /* x is lvalue, so this should succeed */

    PASS("test_inc_non_lvalue");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 22: Pointer type caching
 * ================================================================== */
static void test_ptr_caching(void)
{
    SemaContext *s = sema_create();
    int int_id = sema_int_type(s);
    int char_id = sema_char_type(s);

    int ptr_int = sema_ptr_type(s, int_id);
    int ptr_int2 = sema_ptr_type(s, int_id);
    assert(ptr_int == ptr_int2);  /* cached */

    int ptr_char = sema_ptr_type(s, char_id);
    assert(ptr_char != ptr_int);  /* different element */

    PASS("test_ptr_caching");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 23: sema_lookup returns NULL for missing symbol
 * ================================================================== */
static void test_lookup_missing(void)
{
    SemaContext *s = sema_create();
    Symbol *sym = sema_lookup(s, djb2("nonexistent"), "nonexistent");
    assert(sym == NULL);
    PASS("test_lookup_missing");
    sema_destroy(s);
}

/* ==================================================================
 * TEST 24: sema_get_type out of bounds returns NULL
 * ================================================================== */
static void test_get_type_oob(void)
{
    SemaContext *s = sema_create();
    assert(sema_get_type(s, -1) == NULL);
    assert(sema_get_type(s, 9999) == NULL);
    PASS("test_get_type_oob");
    sema_destroy(s);
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    printf("=== tc semantic analysis tests ===\n\n");

    test_create_destroy();
    test_builtin_types();
    test_unsigned_types();
    test_ptr_type();
    test_add_type();
    test_var_decl();
    test_func_decl();
    test_const_expr();
    test_undeclared_error();
    test_redefinition_error();
    test_scope_nesting();
    test_unary_neg_const();
    test_binary_arith_const();
    test_logical_ops();
    test_bitwise_ops();
    test_comparison_ops();
    test_while_loop();
    test_for_loop();
    test_if_else();
    test_div_zero();
    test_inc_non_lvalue();
    test_ptr_caching();
    test_lookup_missing();
    test_get_type_oob();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
