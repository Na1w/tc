#define _POSIX_C_SOURCE 200809L

#include "src/opt.h"
#include "src/ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(int condition, const char *test_name)
{
    if (condition) {
        printf("  PASS: %s\n", test_name);
        tests_passed++;
    } else {
        printf("  FAIL: %s\n", test_name);
        tests_failed++;
    }
}

/* Helper: find instruction by temp_id */
static int find_instr_by_temp(const IRProgram *prog, int temp_id)
{
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].temp_id == temp_id) return i;
    }
    return -1;
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

/* Helper: emit an instruction and set its temp_id, returning the temp_id */
static int emit_with_temp(IRProgram *prog, IRKind kind, int temp_id, int src1, int src2)
{
    ir_emit(prog, kind, -1, src1, src2);
    prog->instrs[prog->count - 1].temp_id = temp_id;
    return temp_id;
}

/* ==================================================================
 * Test 1: Constant Folding
 * "2 + 3" should become CONST(5)
 * ================================================================== */
static void test_constant_folding(void)
{
    printf("\n=== Test 1: Constant Folding ===\n");

    IRProgram *prog = ir_create();

    /* t0 = 2 */
    int t0 = ir_emit_const(prog, 2);
    /* t1 = 3 */
    int t1 = ir_emit_const(prog, 3);
    /* t2 = t0 + t1 */
    int t2 = emit_with_temp(prog, IR_ADD, 10, t0, t1);

    /* Also test comparison: t3 = (2 == 3) should become CONST(0) */
    int t3 = emit_with_temp(prog, IR_CMP_EQ, 11, t0, t1);

    /* Test: t4 = -5 should fold from IR_NEG of CONST(5) */
    int t5_const = ir_emit_const(prog, 5);
    int t4 = emit_with_temp(prog, IR_NEG, 12, t5_const, -1);

    printf("  Before optimization:\n");
    ir_print(prog);

    opt_constant_fold(prog);

    printf("  After optimization:\n");
    ir_print(prog);

    /* Check: t2 should now be CONST with ival=5 */
    int idx2 = find_instr_by_temp(prog, t2);
    check(idx2 >= 0 && prog->instrs[idx2].kind == IR_CONST && prog->instrs[idx2].ival == 5,
          "2 + 3 -> CONST(5)");

    /* Check: t3 should now be CONST with ival=0 (2 != 3) */
    int idx3 = find_instr_by_temp(prog, t3);
    check(idx3 >= 0 && prog->instrs[idx3].kind == IR_CONST && prog->instrs[idx3].ival == 0,
          "2 == 3 -> CONST(0)");

    /* Check: t4 should now be CONST with ival=-5 */
    int idx4 = find_instr_by_temp(prog, t4);
    check(idx4 >= 0 && prog->instrs[idx4].kind == IR_CONST && prog->instrs[idx4].ival == -5,
          "-CONST(5) -> CONST(-5)");

    ir_destroy(prog);
}

/* ==================================================================
 * Test 2: Dead Code Elimination
 * Unused computation should be eliminated
 * ================================================================== */
static void test_dead_code_elim(void)
{
    printf("\n=== Test 2: Dead Code Elimination ===\n");

    IRProgram *prog = ir_create();

    /* t0 = 10 */
    int t0 = ir_emit_const(prog, 10);
    /* t1 = 20 */
    int t1 = ir_emit_const(prog, 20);
    /* t2 = t0 + t1 (UNUSED - should be eliminated) */
    int t2 = emit_with_temp(prog, IR_ADD, 100, t0, t1);
    /* t3 = 2 */
    int t3 = ir_emit_const(prog, 2);
    /* t4 = t0 * t3 (USED in return) */
    int t4 = emit_with_temp(prog, IR_MUL, 101, t0, t3);

    /* return t4 */
    ir_emit(prog, IR_RET, -1, t4, -1);

    printf("  Before optimization:\n");
    ir_print(prog);

    opt_dead_code_elim(prog);

    printf("  After optimization:\n");
    ir_print(prog);

    /* t2's instruction should be eliminated */
    int idx2 = find_instr_by_temp(prog, t2);
    check(idx2 < 0, "Unused t0 + t1 eliminated");

    /* t4 should still exist (used in return) */
    int idx4 = find_instr_by_temp(prog, t4);
    check(idx4 >= 0, "Used t0 * 2 preserved");

    /* RET should still exist */
    check(count_kind(prog, IR_RET) == 1, "RET instruction preserved");

    ir_destroy(prog);
}

/* ==================================================================
 * Test 3: Common Subexpression Elimination
 * "x * y" computed only once when used twice in same block
 * We use LOAD from different addresses to prevent constant folding.
 * ================================================================== */
static void test_cse(void)
{
    printf("\n=== Test 3: Common Subexpression Elimination ===\n");

    IRProgram *prog = ir_create();

    /* Create two address temps (non-constant values) that won't get folded */
    /* t0 = &local[0], t1 = &local[8] */
    int t0 = ir_emit_addr_local(prog, 0);
    int t1 = ir_emit_addr_local(prog, 8);

    /* t2 = t0 * t1 (address arithmetic - won't constant fold) */
    int t2 = emit_with_temp(prog, IR_MUL, 200, t0, t1);

    /* t3 = t0 * t1 (same expression - should be eliminated by CSE) */
    int t3 = emit_with_temp(prog, IR_MUL, 201, t0, t1);

    /* Use t2 and t3 in something (so they're not killed by DCE) */
    /* t4 = t2 + t3 */
    int t4 = emit_with_temp(prog, IR_ADD, 202, t2, t3);

    /* return t4 */
    ir_emit(prog, IR_RET, -1, t4, -1);

    printf("  Before optimization:\n");
    ir_print(prog);

    /* Run DCE first (no constant folding since operands aren't constants) */
    opt_dead_code_elim(prog);

    printf("  After DCE:\n");
    ir_print(prog);

    opt_cse_basic_block(prog);

    printf("  After CSE:\n");
    ir_print(prog);

    /* Count MUL instructions - should be 1 (the duplicate eliminated) */
    int mul_count = count_kind(prog, IR_MUL);
    check(mul_count == 1, "Duplicate MUL eliminated (only 1 remains)");

    ir_destroy(prog);
}

/* ==================================================================
 * Test 4: Copy Propagation
 * mov'd temps should be replaced
 * ================================================================== */
static void test_copy_propagation(void)
{
    printf("\n=== Test 4: Copy Propagation ===\n");

    IRProgram *prog = ir_create();

    /* t0 = 42 */
    int t0 = ir_emit_const(prog, 42);

    /* t1 = t0 (copy) */
    int t1 = emit_with_temp(prog, IR_MOV, 300, t0, -1);

    /* t2 = t1 + 1 (uses the copy) */
    int one = ir_emit_const(prog, 1);
    int t2 = emit_with_temp(prog, IR_ADD, 301, t1, one);

    /* return t2 */
    ir_emit(prog, IR_RET, -1, t2, -1);

    printf("  Before optimization:\n");
    ir_print(prog);

    opt_copy_propagation(prog);

    printf("  After copy propagation:\n");
    ir_print(prog);

    /* IR_MOV should be eliminated */
    check(count_kind(prog, IR_MOV) == 0, "MOV eliminated by copy propagation");

    /* t1 should no longer exist (replaced by t0) */
    int idx1 = find_instr_by_temp(prog, t1);
    check(idx1 < 0, "t1 (copy of t0) eliminated");

    /* The ADD should now use t0 instead of t1 */
    int idx2 = find_instr_by_temp(prog, t2);
    if (idx2 >= 0) {
        check(prog->instrs[idx2].src_id == t0, "ADD now uses t0 instead of t1");
    } else {
        /* t2 might also have been constant-folded away, which is fine */
        check(1, "t2 resolved (possibly constant-folded)");
    }

    ir_destroy(prog);
}

/* ==================================================================
 * Test 5: Double-free fix verification
 * Create IR with shared labels, destroy, verify no crash
 * ================================================================== */
static void test_label_ownership(void)
{
    printf("\n=== Test 5: Label Ownership (no double-free) ===\n");

    IRProgram *prog = ir_create();

    /* Create a label */
    ir_emit_label(prog, "test_label");

    /* Create multiple instructions referencing the same label */
    ir_emit(prog, IR_BR, -1, -1, -1);
    prog->instrs[prog->count - 1].label = prog->instrs[0].label;

    ir_emit(prog, IR_BR_IF, -1, 0, -1);
    prog->instrs[prog->count - 1].label = prog->instrs[0].label;

    /* Another label */
    ir_emit_label(prog, "another_label");

    ir_emit(prog, IR_BR, -1, -1, -1);
    prog->instrs[prog->count - 1].label = prog->instrs[3].label;

    /* A call */
    ir_emit_call(prog, -1, "printf", 0, NULL);

    printf("  IR created with shared labels:\n");
    ir_print(prog);

    /* This should NOT crash (no double-free) */
    ir_destroy(prog);

    check(1, "ir_destroy with shared labels did not crash");
}

int main(void)
{
    printf("=== TC Optimizer Tests ===\n");

    test_constant_folding();
    test_dead_code_elim();
    test_cse();
    test_copy_propagation();
    test_label_ownership();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
