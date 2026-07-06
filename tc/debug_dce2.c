#define _POSIX_C_SOURCE 200809L
#include "src/ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Inline the DCE logic for debugging */
static int ir_produces_value(const IRInstr *i) { return i->temp_id >= 0; }

static void compact_program(IRProgram *prog)
{
    int write = 0;
    for (int read = 0; read < prog->count; read++) {
        if (prog->instrs[read].kind != IR_NOP) {
            if (write != read) {
                prog->instrs[write] = prog->instrs[read];
            }
            write++;
        }
    }
    prog->count = write;
}

int main(void) {
    IRProgram *prog = ir_create();
    
    int t0 = ir_emit_const(prog, 10);
    int t1 = ir_emit_const(prog, 20);
    ir_emit(prog, IR_ADD, -1, t0, t1);
    prog->instrs[prog->count - 1].temp_id = 100;
    int t3 = ir_emit_const(prog, 2);
    ir_emit(prog, IR_MUL, -1, t0, t3);
    prog->instrs[prog->count - 1].temp_id = 101;
    ir_emit(prog, IR_RET, -1, 101, -1);
    
    printf("Before: count=%d\n", prog->count);
    ir_print(prog);
    
    /* Phase A: Forward reachability */
    int *reachable = calloc(prog->count, sizeof(int));
    reachable[0] = 1;
    
    /* Mark fall-through */
    for (int i = 0; i < prog->count; i++) {
        if (reachable[i]) {
            printf("  [%d] is reachable\n", i);
            if (i + 1 < prog->count) {
                reachable[i + 1] = 1;
            }
        }
    }
    
    printf("\nReachability: all should be 1\n");
    for (int i = 0; i < prog->count; i++) {
        printf("  reachable[%d] = %d\n", i, reachable[i]);
    }
    
    /* Phase B: Backward use-tracking */
    int max_temp = -1;
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].temp_id > max_temp)
            max_temp = prog->instrs[i].temp_id;
    }
    printf("\nmax_temp = %d\n", max_temp);
    
    int *temp_used = calloc((size_t)(max_temp + 1), sizeof(int));
    
    for (int i = 0; i < prog->count; i++) {
        IRInstr *instr = &prog->instrs[i];
        if (instr->kind == IR_NOP) continue;
        switch (instr->kind) {
            case IR_RET:
                if (instr->src_id >= 0) {
                    temp_used[instr->src_id] = 1;
                    printf("  RET: marking temp_used[%d] = 1\n", instr->src_id);
                }
                break;
            default:
                if (instr->src_id >= 0) temp_used[instr->src_id] = 1;
                if (instr->src2_id >= 0) temp_used[instr->src2_id] = 1;
                break;
        }
    }
    
    printf("\nAfter initial marking:\n");
    printf("  temp_used[0] = %d\n", temp_used[0]);
    printf("  temp_used[1] = %d\n", temp_used[1]);
    printf("  temp_used[3] = %d\n", temp_used[3]);
    printf("  temp_used[100] = %d\n", temp_used[100]);
    printf("  temp_used[101] = %d\n", temp_used[101]);
    
    /* Propagate */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < prog->count; i++) {
            IRInstr *instr = &prog->instrs[i];
            if (instr->kind == IR_NOP) continue;
            if (!ir_produces_value(instr)) continue;
            int tid = instr->temp_id;
            if (tid < 0 || tid > max_temp) continue;
            if (temp_used[tid]) {
                if (instr->src_id >= 0 && instr->src_id <= max_temp) {
                    if (!temp_used[instr->src_id]) {
                        temp_used[instr->src_id] = 1;
                        changed = 1;
                        printf("  Prop: temp_used[%d] = 1 (src of t%d)\n", instr->src_id, tid);
                    }
                }
                if (instr->src2_id >= 0 && instr->src2_id <= max_temp) {
                    if (!temp_used[instr->src2_id]) {
                        temp_used[instr->src2_id] = 1;
                        changed = 1;
                        printf("  Prop: temp_used[%d] = 1 (src2 of t%d)\n", instr->src2_id, tid);
                    }
                }
            }
        }
    }
    
    printf("\nAfter propagation:\n");
    printf("  temp_used[0] = %d\n", temp_used[0]);
    printf("  temp_used[1] = %d\n", temp_used[1]);
    printf("  temp_used[3] = %d\n", temp_used[3]);
    printf("  temp_used[100] = %d\n", temp_used[100]);
    printf("  temp_used[101] = %d\n", temp_used[101]);
    
    /* Kill */
    printf("\nKill phase:\n");
    for (int i = 0; i < prog->count; i++) {
        IRInstr *instr = &prog->instrs[i];
        if (instr->kind == IR_NOP) continue;
        if (!ir_produces_value(instr)) {
            printf("  [%d] no value produced (kind=%d), skip\n", i, instr->kind);
            continue;
        }
        switch (instr->kind) {
            case IR_LABEL: case IR_BR: case IR_BR_IF: case IR_BR_IF_NOT:
            case IR_CALL: case IR_CALL_IND: case IR_RET:
            case IR_STORE: case IR_ALLOC: case IR_GLOBAL_STR:
            case IR_GLOBAL_DATA: case IR_PARAM: case IR_ARG:
            case IR_SYSCALL:
                printf("  [%d] never-kill kind=%d\n", i, instr->kind);
                continue;
            default:
                break;
        }
        int tid = instr->temp_id;
        printf("  [%d] kind=%d temp_id=%d temp_used=%d\n", i, instr->kind, tid, (tid >= 0 && tid <= max_temp) ? temp_used[tid] : -1);
        if (tid >= 0 && tid <= max_temp && !temp_used[tid]) {
            printf("  [%d] KILLED\n", i);
            instr->kind = IR_NOP;
        }
    }
    
    compact_program(prog);
    printf("\nAfter DCE: count=%d\n", prog->count);
    ir_print(prog);
    
    free(reachable);
    free(temp_used);
    ir_destroy(prog);
    return 0;
}
