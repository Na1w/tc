#define _POSIX_C_SOURCE 200809L
#include "src/opt.h"
#include "src/ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    IRProgram *prog = ir_create();
    
    /* t0 = 10 (temp_id=0) */
    int t0 = ir_emit_const(prog, 10);
    /* t1 = 20 (temp_id=1) */
    int t1 = ir_emit_const(prog, 20);
    /* t2 = t0 + t1 (temp_id=100, UNUSED) */
    ir_emit(prog, IR_ADD, -1, t0, t1);
    prog->instrs[prog->count - 1].temp_id = 100;
    /* t3 = 2 (temp_id=3) */
    int t3 = ir_emit_const(prog, 2);
    /* t4 = t0 * t3 (temp_id=101, USED in return) */
    ir_emit(prog, IR_MUL, -1, t0, t3);
    prog->instrs[prog->count - 1].temp_id = 101;
    
    /* return t4 (src_id=101) */
    ir_emit(prog, IR_RET, -1, 101, -1);
    
    printf("Before DCE (count=%d):\n", prog->count);
    ir_print(prog);
    
    printf("\nmax_temp will be 101\n");
    printf("temp_used array size will be 102\n");
    printf("RET src_id=101, so temp_used[101]=1\n");
    printf("MUL temp_id=101, since temp_used[101]=1, sources t0(0) and t3(3) are marked used\n");
    printf("MUL should NOT be killed because temp_used[101]=1\n");
    printf("RET should NOT be killed because it's in the never-kill list\n");
    
    opt_dead_code_elim(prog);
    
    printf("\nAfter DCE (count=%d):\n", prog->count);
    ir_print(prog);
    
    ir_destroy(prog);
    return 0;
}
