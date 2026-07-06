#define _POSIX_C_SOURCE 200809L
#include "src/ir.h"
#include <stdio.h>
int main(void) {
    IRProgram *prog = ir_create();
    int t0 = ir_emit_const(prog, 2);
    int t1 = ir_emit_const(prog, 3);
    printf("t0=%d t1=%d count=%d\n", t0, t1, prog->count);
    int idx = ir_emit(prog, IR_ADD, -1, t0, t1);
    printf("idx=%d count=%d\n", idx, prog->count);
    return 0;
}
