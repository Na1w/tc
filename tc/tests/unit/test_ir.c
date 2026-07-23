/*
 * test_ir.c -- Tests for the IR module.
 *
 * Parses simple C programs, lowers them to IR, prints the IR,
 * and verifies instruction counts and types.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "src/lexer.h"
#include "src/parser.h"
#include "src/sema.h"
#include "src/ir.h"

static int tests_passed = 0;
static int tests_failed = 0;

/* Helper: run a test that parses source, lowers to IR, prints it. */
static IRProgram *run_ir_test(const char *name, const char *source)
{
    Lexer *lex = lexer_create(source, (int)strlen(source));
    Parser *p = parser_create(lex);
    Node *ast = parse_program(p);

    if (!ast) {
        printf("FAIL: %s -- parse failed\n", name);
        tests_failed++;
        parser_destroy(p);
        lexer_destroy(lex);
        return NULL;
    }

    SemaContext *sema = sema_create();
    if (sema) {
        sema_analyze(sema, ast);
    }

    IRProgram *prog = ir_lower(ast, sema);

    if (!prog) {
        printf("FAIL: %s -- ir_lower returned NULL\n", name);
        tests_failed++;
    } else {
        printf("=== %s (%d instructions) ===\n", name, prog->count);
        ir_print(prog);
        printf("\n");
        tests_passed++;
    }

    if (sema) sema_destroy(sema);
    node_destroy(ast);
    parser_destroy(p);
    lexer_destroy(lex);
    return prog;
}

/* Helper: count instructions of a given kind */
static int count_kind(const IRProgram *prog, IRKind kind)
{
    int count = 0;
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == kind) count++;
    }
    return count;
}

/* Helper: verify a condition */
static void verify(const char *test_name, int condition, const char *msg)
{
    if (condition) {
        printf("  PASS: %s\n", msg);
    } else {
        printf("  FAIL: %s -- %s\n", test_name, msg);
        tests_failed++;
    }
}

/* ==================================================================
 *  Tests
 * ================================================================== */

static void test_const(void)
{
    IRProgram *prog = run_ir_test("const", "int main() { return 42; }");
    if (!prog) return;

    verify("const", count_kind(prog, IR_CONST) >= 1, "has at least 1 IR_CONST");
    verify("const", count_kind(prog, IR_RET) >= 1, "has at least 1 IR_RET");

    ir_destroy(prog);
}

static void test_arithmetic(void)
{
    IRProgram *prog = run_ir_test("arithmetic",
        "int main() { int x = 1 + 2 * 3; return x; }");
    if (!prog) return;

    verify("arithmetic", count_kind(prog, IR_CONST) >= 2, "has at least 2 IR_CONST");
    verify("arithmetic", count_kind(prog, IR_ADD) >= 1, "has at least 1 IR_ADD");
    verify("arithmetic", count_kind(prog, IR_MUL) >= 1, "has at least 1 IR_MUL");
    verify("arithmetic", count_kind(prog, IR_RET) >= 1, "has at least 1 IR_RET");

    ir_destroy(prog);
}

static void test_comparison(void)
{
    IRProgram *prog = run_ir_test("comparison",
        "int main() { int x = 5; int y = 0; if (x > 0) { y = 1; } return 0; }");
    if (!prog) return;

    verify("comparison", count_kind(prog, IR_CMP_GT) >= 1, "has at least 1 IR_CMP_GT");
    verify("comparison", count_kind(prog, IR_BR_IF_NOT) >= 1, "has at least 1 IR_BR_IF_NOT");
    verify("comparison", count_kind(prog, IR_LABEL) >= 2, "has at least 2 IR_LABEL");

    ir_destroy(prog);
}

static void test_while_loop(void)
{
    IRProgram *prog = run_ir_test("while_loop",
        "int main() { int x = 0; while (x < 10) { x = x + 1; } return 0; }");
    if (!prog) return;

    verify("while_loop", count_kind(prog, IR_CMP_LT) >= 1, "has at least 1 IR_CMP_LT");
    verify("while_loop", count_kind(prog, IR_BR) >= 1, "has at least 1 IR_BR (back-edge)");
    verify("while_loop", count_kind(prog, IR_BR_IF_NOT) >= 1, "has at least 1 IR_BR_IF_NOT");
    verify("while_loop", count_kind(prog, IR_LABEL) >= 2, "has at least 2 IR_LABEL");

    ir_destroy(prog);
}

static void test_for_loop(void)
{
    IRProgram *prog = run_ir_test("for_loop",
        "int main() { int s = 0; for (int i = 0; i < 10; i = i + 1) { s = s + i; } return 0; }");
    if (!prog) return;

    verify("for_loop", count_kind(prog, IR_CMP_LT) >= 1, "has at least 1 IR_CMP_LT");
    verify("for_loop", count_kind(prog, IR_ADD) >= 1, "has at least 1 IR_ADD");
    verify("for_loop", count_kind(prog, IR_BR) >= 1, "has at least 1 IR_BR (back-edge)");
    verify("for_loop", count_kind(prog, IR_BR_IF_NOT) >= 1, "has at least 1 IR_BR_IF_NOT");
    verify("for_loop", count_kind(prog, IR_LABEL) >= 2, "has at least 2 IR_LABEL");
    verify("for_loop", count_kind(prog, IR_ALLOC) >= 1, "has at least 1 IR_ALLOC (for i)");

    ir_destroy(prog);
}

static void test_function_call(void)
{
    IRProgram *prog = run_ir_test("func_call",
        "int main() { printf(\"hello\"); return 0; }");
    if (!prog) return;

    verify("func_call", count_kind(prog, IR_CALL) >= 1, "has at least 1 IR_CALL");
    verify("func_call", count_kind(prog, IR_GLOBAL_STR) >= 1, "has at least 1 IR_GLOBAL_STR");
    verify("func_call", count_kind(prog, IR_RET) >= 1, "has at least 1 IR_RET");

    ir_destroy(prog);
}

static void test_if_else(void)
{
    IRProgram *prog = run_ir_test("if_else",
        "int main() { int x = 5; int y = 0; if (x > 0) { y = 1; } else { y = 2; } return 0; }");
    if (!prog) return;

    verify("if_else", count_kind(prog, IR_CMP_GT) >= 1, "has at least 1 IR_CMP_GT");
    verify("if_else", count_kind(prog, IR_BR_IF_NOT) >= 1, "has at least 1 IR_BR_IF_NOT");
    verify("if_else", count_kind(prog, IR_BR) >= 1, "has at least 1 IR_BR (skip else)");
    verify("if_else", count_kind(prog, IR_LABEL) >= 2, "has at least 2 IR_LABEL");

    ir_destroy(prog);
}

static void test_negation(void)
{
    IRProgram *prog = run_ir_test("negation",
        "int main() { int x = -5; return x; }");
    if (!prog) return;

    verify("negation", count_kind(prog, IR_CONST) >= 1, "has at least 1 IR_CONST");
    verify("negation", count_kind(prog, IR_NEG) >= 1, "has at least 1 IR_NEG");
    verify("negation", count_kind(prog, IR_RET) >= 1, "has at least 1 IR_RET");

    ir_destroy(prog);
}

static void test_logical_not(void)
{
    IRProgram *prog = run_ir_test("logical_not",
        "int main() { int flag = 0; int x = !flag; return x; }");
    if (!prog) return;

    verify("logical_not", count_kind(prog, IR_NOT) >= 1, "has at least 1 IR_NOT");
    verify("logical_not", count_kind(prog, IR_RET) >= 1, "has at least 1 IR_RET");

    ir_destroy(prog);
}

static void test_assignment(void)
{
    IRProgram *prog = run_ir_test("assignment",
        "int main() { int x = 0; x = 42; return x; }");
    if (!prog) return;

    verify("assignment", count_kind(prog, IR_CONST) >= 1, "has at least 1 IR_CONST");
    verify("assignment", count_kind(prog, IR_STORE) >= 1, "has at least 1 IR_STORE");
    verify("assignment", count_kind(prog, IR_RET) >= 1, "has at least 1 IR_RET");

    ir_destroy(prog);
}

static void test_bitwise(void)
{
    IRProgram *prog = run_ir_test("bitwise",
        "int main() { int a = 1; int b = 2; int c = 3; int d = 4; int e = 5; int f = 6; "
        "int x = a & b; int y = c | d; int z = e ^ f; return x; }");
    if (!prog) return;

    verify("bitwise", count_kind(prog, IR_BAND) >= 1, "has at least 1 IR_BAND");
    verify("bitwise", count_kind(prog, IR_BOR) >= 1, "has at least 1 IR_BOR");
    verify("bitwise", count_kind(prog, IR_XOR) >= 1, "has at least 1 IR_XOR");

    ir_destroy(prog);
}

static void test_shift(void)
{
    IRProgram *prog = run_ir_test("shift",
        "int main() { int a = 8; int b = 4; int x = a << 2; int y = b >> 1; return x; }");
    if (!prog) return;

    verify("shift", count_kind(prog, IR_SHL) >= 1, "has at least 1 IR_SHL");
    verify("shift", count_kind(prog, IR_SHR) >= 1, "has at least 1 IR_SHR");

    ir_destroy(prog);
}

static void test_string_literal(void)
{
    IRProgram *prog = run_ir_test("string_literal",
        "int main() { char *s = \"hello world\"; return 0; }");
    if (!prog) return;

    verify("string_literal", count_kind(prog, IR_GLOBAL_STR) >= 1, "has at least 1 IR_GLOBAL_STR");
    verify("string_literal", count_kind(prog, IR_ADDR_GLOBAL) >= 1, "has at least 1 IR_ADDR_GLOBAL");

    ir_destroy(prog);
}

static void test_empty_program(void)
{
    IRProgram *prog = run_ir_test("empty", "int main() { return 0; }");
    if (!prog) return;

    verify("empty", prog->count >= 3, "has at least 3 instructions (label, ret, const)");
    verify("empty", count_kind(prog, IR_LABEL) >= 1, "has function label");

    ir_destroy(prog);
}

static void test_nested_if(void)
{
    IRProgram *prog = run_ir_test("nested_if",
        "int main() { int a = 1; int b = 1; int c = 0; if (a) { if (b) { c = 1; } } return 0; }");
    if (!prog) return;

    verify("nested_if", count_kind(prog, IR_BR_IF_NOT) >= 2, "has at least 2 IR_BR_IF_NOT");
    verify("nested_if", count_kind(prog, IR_LABEL) >= 4, "has at least 4 IR_LABEL");

    ir_destroy(prog);
}

static void test_var_decl_init(void)
{
    IRProgram *prog = run_ir_test("var_decl_init",
        "int main() { int x = 10; int y = 20; return x + y; }");
    if (!prog) return;

    verify("var_decl_init", count_kind(prog, IR_ALLOC) >= 2, "has at least 2 IR_ALLOC");
    verify("var_decl_init", count_kind(prog, IR_CONST) >= 2, "has at least 2 IR_CONST");
    verify("var_decl_init", count_kind(prog, IR_STORE) >= 2, "has at least 2 IR_STORE");

    ir_destroy(prog);
}

static void test_api_create_destroy(void)
{
    IRProgram *prog = ir_create();
    assert(prog != NULL);
    assert(prog->count == 0);
    assert(prog->cap > 0);

    /* Emit some instructions */
    int t1 = ir_emit_const(prog, 42);
    int t2 = ir_emit_const(prog, 100);
    int t3 = ir_emit(prog, IR_ADD, t1 + t2, t1, t2);

    assert(prog->count == 3);
    assert(prog->instrs[0].kind == IR_CONST);
    assert(prog->instrs[0].ival == 42);
    assert(prog->instrs[1].kind == IR_CONST);
    assert(prog->instrs[1].ival == 100);
    assert(prog->instrs[2].kind == IR_ADD);
    assert(t3 >= 0);

    printf("  PASS: ir_create/ir_emit_const/ir_emit API works\n");
    tests_passed++;

    ir_destroy(prog);
}

static void test_api_label(void)
{
    IRProgram *prog = ir_create();
    (void)ir_emit_label(prog, "foo");
    assert(prog->count == 1);
    assert(prog->instrs[0].kind == IR_LABEL);
    assert(strcmp(prog->instrs[0].label, "foo") == 0);

    printf("  PASS: ir_emit_label API works\n");
    tests_passed++;

    ir_destroy(prog);
}

static void test_api_call(void)
{
    IRProgram *prog = ir_create();
    int t1 = ir_emit_const(prog, 1);
    int t2 = ir_emit_const(prog, 2);
    int args[2] = { t1, t2 };
    int t3 = ir_emit_call(prog, 99, "printf", 2, args);

    assert(prog->count == 3);
    assert(prog->instrs[2].kind == IR_CALL);
    assert(prog->instrs[2].temp_id == 99);
    assert(prog->instrs[2].arg_count == 2);
    assert(prog->instrs[2].arg_ids[0] == t1);
    assert(prog->instrs[2].arg_ids[1] == t2);
    assert(t3 == 99);

    printf("  PASS: ir_emit_call API works\n");
    tests_passed++;

    ir_destroy(prog);
}

static void test_api_alloc(void)
{
    IRProgram *prog = ir_create();
    ir_emit_alloc(prog, 16);
    assert(prog->count == 1);
    assert(prog->instrs[0].kind == IR_ALLOC);
    assert(prog->instrs[0].ival == 16);

    printf("  PASS: ir_emit_alloc API works\n");
    tests_passed++;

    ir_destroy(prog);
}

static void test_api_addr(void)
{
    IRProgram *prog = ir_create();

    int ag = ir_emit_addr_global(prog, "myvar");
    assert(prog->instrs[0].kind == IR_ADDR_GLOBAL);
    assert(strcmp(prog->instrs[0].label, "myvar") == 0);

    int al = ir_emit_addr_local(prog, -16);
    assert(prog->instrs[1].kind == IR_ADDR_LOCAL);
    assert(prog->instrs[1].ival == -16);

    int ap = ir_emit_addr_param(prog, 2);
    assert(prog->instrs[2].kind == IR_ADDR_PARAM);
    assert(prog->instrs[2].ival == 2);

    (void)ag; (void)al; (void)ap;

    printf("  PASS: ir_emit_addr_* API works\n");
    tests_passed++;

    ir_destroy(prog);
}

static void test_api_load_store(void)
{
    IRProgram *prog = ir_create();

    int t1 = ir_emit_const(prog, 100);
    int t2 = ir_emit_addr_local(prog, -8);
    int t3 = ir_emit_load(prog, t2, 4, 1);
    ir_emit_store(prog, t2, t1, 4, 1);

    assert(prog->instrs[2].kind == IR_LOAD);
    assert(prog->instrs[2].src_id == t2);
    assert(prog->instrs[2].type_size == 4);
    assert(prog->instrs[2].is_signed == 1);

    assert(prog->instrs[3].kind == IR_STORE);
    assert(prog->instrs[3].src_id == t2);
    assert(prog->instrs[3].src2_id == t1);

    (void)t3;

    printf("  PASS: ir_emit_load/store API works\n");
    tests_passed++;

    ir_destroy(prog);
}

/* ==================================================================
 *  Main
 * ================================================================== */

int main(void)
{
    printf("=== IR API Tests ===\n");
    test_api_create_destroy();
    test_api_label();
    test_api_call();
    test_api_alloc();
    test_api_addr();
    test_api_load_store();
    printf("\n");

    printf("=== IR Lowering Tests ===\n");
    test_const();
    test_arithmetic();
    test_comparison();
    test_while_loop();
    test_for_loop();
    test_function_call();
    test_if_else();
    test_negation();
    test_logical_not();
    test_assignment();
    test_bitwise();
    test_shift();
    test_string_literal();
    test_empty_program();
    test_nested_if();
    test_var_decl_init();
    printf("\n");

    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
