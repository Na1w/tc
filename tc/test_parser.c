#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/lexer.h"
#include "src/parser.h"

static int tests_passed = 0;
static int tests_failed = 0;

static void test_parse(const char *name, const char *source, int expect_success) {
    Lexer *lex = lexer_create(source, strlen(source));
    Parser *p = parser_create(lex);
    Node *ast = parse_program(p);
    int success = !p->error && ast != NULL;

    if (success == expect_success) {
        printf("PASS: %s\n", name);
        tests_passed++;
        if (expect_success) {
            node_dump(ast, 0);
            printf("\n");
        }
    } else {
        printf("FAIL: %s (expected %s, got %s)\n",
               name, expect_success ? "success" : "error",
               success ? "success" : "error");
        tests_failed++;
    }

    if (ast) node_destroy(ast);
    parser_destroy(p);
    lexer_destroy(lex);
}

int main(void) {
    /* Test 1: Simple expression */
    test_parse("simple_expr", "int main() { return 42; }", 1);

    /* Test 2: If/else */
    test_parse("if_else", "int main() { if (x > 0) { y = 1; } else { y = 2; } return 0; }", 1);

    /* Test 3: While loop */
    test_parse("while_loop", "int main() { while (x < 10) { x++; } return 0; }", 1);

    /* Test 4: For loop */
    test_parse("for_loop", "int main() { for (int i = 0; i < 10; i++) { sum += i; } return 0; }", 1);

    /* Test 5: Function call */
    test_parse("func_call", "int main() { printf(\"hello\"); return 0; }", 1);

    /* Test 6: Array access */
    test_parse("array_access", "int main() { arr[0] = 42; return 0; }", 1);

    /* Test 7: String literal */
    test_parse("string_lit", "int main() { char *s = \"hello world\"; return 0; }", 1);

    /* Test 8: Ternary */
    test_parse("ternary", "int main() { int x = a > b ? a : b; return 0; }", 1);

    /* Test 9: Compound assignment */
    test_parse("compound_assign", "int main() { x += 5; x -= 3; x *= 2; return 0; }", 1);

    /* Test 10: Do-while */
    test_parse("do_while", "int main() { do { x++; } while (x < 10); return 0; }", 1);

    /* Test 11: Switch/case */
    test_parse("switch_case", "int main() { switch (x) { case 1: y = 1; break; default: y = 0; } return 0; }", 1);

    /* Test 12: Goto and labels */
    test_parse("goto_label", "int main() { if (x) goto done; return 1; done: return 0; }", 1);

    /* Test 13: Break/continue */
    test_parse("break_continue", "int main() { while (1) { if (x) break; if (y) continue; } return 0; }", 1);

    /* Test 14: Multiple declarations */
    test_parse("multi_decl", "int x; char c; int *p;", 1);

    /* Test 15: Complex expression */
    test_parse("complex_expr", "int main() { int x = (a + b) * c - d / e; return 0; }", 1);

    /* Test 16: Pointer operations */
    test_parse("pointer_ops", "int main() { int *p; p = &x; y = *p; return 0; }", 1);

    /* Test 17: Unary operators */
    test_parse("unary_ops", "int main() { int x = -a; x = !b; x = ~c; return 0; }", 1);

    /* Test 18: Function with parameters */
    test_parse("func_params", "int add(int a, int b) { return a + b; }", 1);

    /* Test 19: Nested function calls */
    test_parse("nested_call", "int main() { x = foo(bar(baz(1), 2), 3); return 0; }", 1);

    /* Test 20: Sizeof */
    test_parse("sizeof_expr", "int main() { int x = sizeof(int); return 0; }", 1);

    /* Test 21: Error - missing semicolon */
    test_parse("error_missing_semi", "int x", 0);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
