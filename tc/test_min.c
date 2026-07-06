#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "src/lexer.h"
#include "src/parser.h"

static volatile sig_atomic_t g_timeout = 0;
static void handler(int sig) { (void)sig; g_timeout = 1; }

static int test_one(const char *name, const char *source) {
    alarm(2);
    signal(SIGALRM, handler);
    
    Lexer *lex = lexer_create(source, strlen(source));
    Parser *p = parser_create(lex);
    Node *ast = parse_program(p);
    
    alarm(0);
    
    if (g_timeout) {
        printf("TIMEOUT: %s\n", name);
        parser_destroy(p);
        lexer_destroy(lex);
        return -1;
    }
    
    if (!p->error && ast) {
        printf("PASS: %s\n", name);
        node_destroy(ast);
    } else {
        printf("FAIL: %s (error=%d)\n", name, p->error);
        if (ast) node_destroy(ast);
    }
    parser_destroy(p);
    lexer_destroy(lex);
    return 0;
}

int main(void) {
    struct { const char *name; const char *src; } tests[] = {
        {"simple_expr", "int main() { return 42; }"},
        {"if_else", "int main() { if (x > 0) { y = 1; } else { y = 2; } return 0; }"},
        {"while_loop", "int main() { while (x < 10) { x++; } return 0; }"},
        {"for_loop", "int main() { for (int i = 0; i < 10; i++) { sum += i; } return 0; }"},
        {"func_call", "int main() { printf(\"hello\"); return 0; }"},
        {"array_access", "int main() { arr[0] = 42; return 0; }"},
        {"string_lit", "int main() { char *s = \"hello world\"; return 0; }"},
        {"ternary", "int main() { int x = a > b ? a : b; return 0; }"},
        {"compound_assign", "int main() { x += 5; x -= 3; x *= 2; return 0; }"},
        {"do_while", "int main() { do { x++; } while (x < 10); return 0; }"},
        {"switch_case", "int main() { switch (x) { case 1: y = 1; break; default: y = 0; } return 0; }"},
        {"goto_label", "int main() { if (x) goto done; return 1; done: return 0; }"},
        {"break_continue", "int main() { while (1) { if (x) break; if (y) continue; } return 0; }"},
        {"multi_decl", "int x; char c; int *p;"},
        {"complex_expr", "int main() { int x = (a + b) * c - d / e; return 0; }"},
        {"pointer_ops", "int main() { int *p; p = &x; y = *p; return 0; }"},
        {"unary_ops", "int main() { int x = -a; x = !b; x = ~c; return 0; }"},
        {"func_params", "int add(int a, int b) { return a + b; }"},
        {"nested_call", "int main() { x = foo(bar(baz(1), 2), 3); return 0; }"},
        {"sizeof_expr", "int main() { int x = sizeof(int); return 0; }"},
    };
    
    for (int i = 0; i < (int)(sizeof(tests)/sizeof(tests[0])); i++) {
        test_one(tests[i].name, tests[i].src);
    }
    
    return 0;
}
