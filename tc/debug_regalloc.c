#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Minimal debug program to trace register assignments
// We'll compile this with the tc compiler's source to debug

int main() {
    // Simulate the register allocator for function_call.c
    // Original IR:
    // [002] IR_ADDR_PARAM   t2 = &param[0]
    // [003] IR_LOAD         t0 = *(4)t2
    // [004] IR_CONST        t1 = 2
    // [005] IR_MUL          t2 = t0 MUL t1
    // [006] IR_RET          return t2
    
    // After remapping (first appearance order):
    // t2 -> 0, t0 -> 1, t1 -> 2
    // [002] IR_ADDR_PARAM   t0 = &param[0]
    // [003] IR_LOAD         t1 = *(4)t0
    // [004] IR_CONST        t2 = 2
    // [005] IR_MUL          t0 = t1 MUL t2
    // [006] IR_RET          return t0
    
    // Live intervals:
    // t0: start=2, end=6
    // t1: start=3, end=5
    // t2: start=4, end=5
    
    // Sorted by start: t0(2,6), t1(3,5), t2(4,5)
    
    // ALLOC_REGS = [0,1,2,3,4,5,6,7,8,9,10,11,12,13]
    // free_regs = [0,1,2,3,4,5,6,7,8,9,10,11,12,13]
    // free_reg_count = 14
    
    // Linear scan:
    // t0(2,6) -> free_regs[--14] = free_regs[13] = 13 (R_R15)
    // t1(3,5) -> free_regs[--13] = free_regs[12] = 12 (R_R14)
    // t2(4,5) -> free_regs[--12] = free_regs[11] = 11 (R_R13)
    
    int ALLOC_REGS[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13};
    int free_regs[14];
    int free_reg_count = 14;
    
    for (int i = 0; i < 14; i++) free_regs[i] = ALLOC_REGS[i];
    
    // Intervals sorted by start
    int temp_ids[] = {0, 1, 2};
    int starts[] = {2, 3, 4};
    int ends[] = {6, 5, 5};
    
    printf("Linear scan register allocation:\n");
    for (int i = 0; i < 3; i++) {
        int reg = free_regs[--free_reg_count];
        printf("  temp %d (start=%d, end=%d) -> reg %d\n", temp_ids[i], starts[i], ends[i], reg);
    }
    
    printf("\nExpected assignments: t0->15(r15), t1->14(r14), t2->13(r13)\n");
    
    return 0;
}
