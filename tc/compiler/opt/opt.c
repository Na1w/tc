/*
 * tc/src/opt.c -- IR optimization passes
 *
 * Passes run in order:
 *   1. Constant Folding
 *   2. Dead Code Elimination
 *   3. Common Subexpression Elimination
 *   4. Copy Propagation
 */

#include "opt.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* ==================================================================
 * Helper: check if an instruction produces a value (has temp_id >= 0)
 * ================================================================== */
static int ir_produces_value(const IRInstr *i)
{
    return i->temp_id >= 0;
}

/* ==================================================================
 * Helper: check if instruction is a binary arithmetic/logic op
 * ================================================================== */
static int is_binary_arith(IRKind kind)
{
    return kind == IR_ADD || kind == IR_SUB || kind == IR_MUL ||
           kind == IR_DIV || kind == IR_MOD || kind == IR_BAND ||
           kind == IR_BOR || kind == IR_XOR || kind == IR_SHL ||
           kind == IR_SHR;
}

static int is_comparison(IRKind kind)
{
    return kind == IR_CMP_EQ || kind == IR_CMP_NEQ ||
           kind == IR_CMP_LT || kind == IR_CMP_GT ||
           kind == IR_CMP_LEQ || kind == IR_CMP_GEQ;
}

/* ==================================================================
 * Helper: find the instruction that defines a temp_id
 * Returns the instruction index, or -1 if not found
 * ================================================================== */
static int find_def(const IRProgram *prog, int temp_id)
{
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].temp_id == temp_id) {
            return i;
        }
    }
    return -1;
}

/* ==================================================================
 * Helper: get the constant value of a temp_id (if it's a CONST)
 * Returns 1 in *is_const if the temp holds a constant, 0 otherwise
 * ================================================================== */
static int get_const_value(const IRProgram *prog, int temp_id, int64_t *value)
{
    int def = find_def(prog, temp_id);
    if (def < 0) return 0;
    if (prog->instrs[def].kind == IR_CONST) {
        *value = prog->instrs[def].ival;
        return 1;
    }
    return 0;
}

/* ==================================================================
 * Helper: replace all uses of old_temp with new_temp within [start, end)
 * If start == -1, replace globally (entire program).
 * ================================================================== */
static void replace_temp(IRProgram *prog, int old_temp, int new_temp, int start, int end)
{
    if (start < 0) {
        start = 0;
        end = prog->count;
    }
    for (int i = start; i < end; i++) {
        if (prog->instrs[i].src_id == old_temp) prog->instrs[i].src_id = new_temp;
        if (prog->instrs[i].src2_id == old_temp) prog->instrs[i].src2_id = new_temp;
        if (prog->instrs[i].arg_ids) {
            for (int a = 0; a < prog->instrs[i].arg_count; a++) {
                if (prog->instrs[i].arg_ids[a] == old_temp) {
                    prog->instrs[i].arg_ids[a] = new_temp;
                }
            }
        }
    }
}

/* ==================================================================
 * Helper: compact instructions (remove dead/eliminated ones)
 * ================================================================== */
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

/* ==================================================================
 * PASS 1: Constant Folding
 *
 * If both operands of an arithmetic/logic instruction are constants,
 * evaluate at compile time and replace with IR_CONST.
 * Replace comparisons of constants with CONST(0) or CONST(1).
 * ================================================================== */
void opt_constant_fold(IRProgram *prog)
{
    if (!prog) return;

    for (int i = 0; i < prog->count; i++) {
        IRInstr *instr = &prog->instrs[i];

        if (is_binary_arith(instr->kind)) {
            int64_t v1 = 0, v2 = 0;
            int c1 = get_const_value(prog, instr->src_id, &v1);
            int c2 = get_const_value(prog, instr->src2_id, &v2);

            if (c1 && c2) {
                int64_t result = 0;
                switch (instr->kind) {
                    case IR_ADD: result = v1 + v2; break;
                    case IR_SUB: result = v1 - v2; break;
                    case IR_MUL: result = v1 * v2; break;
                    case IR_DIV: result = (v2 != 0) ? (v1 / v2) : 0; break;
                    case IR_MOD: result = (v2 != 0) ? (v1 % v2) : 0; break;
                    case IR_BAND: result = v1 & v2; break;
                    case IR_BOR: result = v1 | v2; break;
                    case IR_XOR: result = v1 ^ v2; break;
                    case IR_SHL: result = (v2 >= 0 && v2 < 64) ? (v1 << v2) : 0; break;
                    case IR_SHR: result = (v2 >= 0 && v2 < 64) ? (v1 >> v2) : 0; break;
                    default: c1 = 0; c2 = 0; break;
                }
                if (c1 && c2) {
                    instr->kind = IR_CONST;
                    instr->ival = result;
                    instr->src_id = -1;
                    instr->src2_id = -1;
                }
            }
        } else if (is_comparison(instr->kind)) {
            int64_t v1 = 0, v2 = 0;
            int c1 = get_const_value(prog, instr->src_id, &v1);
            int c2 = get_const_value(prog, instr->src2_id, &v2);

            if (c1 && c2) {
                int result = 0;
                switch (instr->kind) {
                    case IR_CMP_EQ: result = (v1 == v2); break;
                    case IR_CMP_NEQ: result = (v1 != v2); break;
                    case IR_CMP_LT: result = (v1 < v2); break;
                    case IR_CMP_GT: result = (v1 > v2); break;
                    case IR_CMP_LEQ: result = (v1 <= v2); break;
                    case IR_CMP_GEQ: result = (v1 >= v2); break;
                    default: break;
                }
                instr->kind = IR_CONST;
                instr->ival = result;
                instr->src_id = -1;
                instr->src2_id = -1;
            }
        } else if (instr->kind == IR_NEG) {
            int64_t v = 0;
            if (get_const_value(prog, instr->src_id, &v)) {
                instr->kind = IR_CONST;
                instr->ival = -v;
                instr->src_id = -1;
            }
        } else if (instr->kind == IR_NOT) {
            int64_t v = 0;
            if (get_const_value(prog, instr->src_id, &v)) {
                instr->kind = IR_CONST;
                instr->ival = (v == 0) ? 1 : 0;
                instr->src_id = -1;
            }
        } else if (instr->kind == IR_BITNOT) {
            int64_t v = 0;
            if (get_const_value(prog, instr->src_id, &v)) {
                instr->kind = IR_CONST;
                instr->ival = ~v;
                instr->src_id = -1;
            }
        }
    }
}

/* ==================================================================
 * PASS 2: Dead Code Elimination
 *
 * a) Forward reachability: compute which labels are reachable,
 *    remove unreachable instructions.
 * b) Backward use-tracking: remove instructions whose result temp
 *    is never used.
 * ================================================================== */
void opt_dead_code_elim(IRProgram *prog)
{
    if (!prog) return;

    /* ---- Phase A: Forward reachability ---- */
    /* Build label -> instruction index map */
    int *label_indices = calloc((size_t)prog->count, sizeof(int));
    int label_count = 0;
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == IR_LABEL) {
            label_indices[label_count++] = i;
        }
    }

    /* BFS from entry point */
    int *reachable = calloc((size_t)prog->count, sizeof(int));
    int *worklist = malloc((size_t)(prog->count * 2) * sizeof(int));
    int wl_top = 0;

    /* Entry: instruction 0 is reachable */
    reachable[0] = 1;
    worklist[wl_top++] = 0;

    /* Also mark all label targets as potentially reachable */
    /* First pass: mark all labels as reachable targets */
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == IR_BR) {
            /* Find target label */
            const char *target = prog->instrs[i].label;
            for (int j = 0; j < prog->count; j++) {
                if (prog->instrs[j].kind == IR_LABEL &&
                    prog->instrs[j].label &&
                    strcmp(prog->instrs[j].label, target) == 0) {
                    if (wl_top >= prog->count * 2) continue;
                    worklist[wl_top++] = j;
                    reachable[j] = 1;
                }
            }
        } else if (prog->instrs[i].kind == IR_BR_IF ||
                   prog->instrs[i].kind == IR_BR_IF_NOT) {
            /* Conditional: both fall-through and target are reachable */
            const char *target = prog->instrs[i].label;
            for (int j = 0; j < prog->count; j++) {
                if (prog->instrs[j].kind == IR_LABEL &&
                    prog->instrs[j].label &&
                    strcmp(prog->instrs[j].label, target) == 0) {
                    if (wl_top >= prog->count * 2) continue;
                    worklist[wl_top++] = j;
                    reachable[j] = 1;
                }
            }
            /* Fall-through */
            if (i + 1 < prog->count) {
                if (wl_top >= prog->count * 2) continue;
                worklist[wl_top++] = i + 1;
                reachable[i + 1] = 1;
            }
        }
    }

    /* Process worklist */
    for (int wi = 0; wi < wl_top; wi++) {
        int idx = worklist[wi];
        if (idx < 0 || idx >= prog->count) continue;
        /* Mark next instruction as reachable (fall-through) */
        if (idx + 1 < prog->count) {
            if (!reachable[idx + 1]) {
                reachable[idx + 1] = 1;
                if (wl_top >= prog->count * 2) continue;
                worklist[wl_top++] = idx + 1;
            }
        }
    }

    /* Remove unreachable instructions */
    for (int i = 0; i < prog->count; i++) {
        if (!reachable[i]) {
            prog->instrs[i].kind = IR_NOP;
        }
    }

    free(label_indices);
    free(reachable);
    free(worklist);

    /* ---- Phase B: Backward use-tracking ---- */
    /* Mark temps that are used */
    int *temp_used = NULL;
    int max_temp = -1;
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].temp_id > max_temp)
            max_temp = prog->instrs[i].temp_id;
    }
    if (max_temp >= 0) {
        temp_used = calloc((size_t)(max_temp + 1), sizeof(int));

        /* RET, STORE, CALL args, BR_IF conditions use temps */
        for (int i = 0; i < prog->count; i++) {
            IRInstr *instr = &prog->instrs[i];
            if (instr->kind == IR_NOP) continue;

            /* Instructions that consume values */
            switch (instr->kind) {
                case IR_RET:
                    if (instr->src_id >= 0 && instr->src_id <= max_temp) temp_used[instr->src_id] = 1;
                    break;
                case IR_STORE:
                    if (instr->src_id >= 0 && instr->src_id <= max_temp) temp_used[instr->src_id] = 1;
                    if (instr->src2_id >= 0 && instr->src2_id <= max_temp) temp_used[instr->src2_id] = 1;
                    break;
                case IR_BR_IF:
                case IR_BR_IF_NOT:
                    if (instr->src_id >= 0 && instr->src_id <= max_temp) temp_used[instr->src_id] = 1;
                    break;
                case IR_CALL:
                case IR_CALL_IND:
                    if (instr->src_id >= 0 && instr->src_id <= max_temp) temp_used[instr->src_id] = 1;
                    if (instr->arg_ids) {
                        for (int a = 0; a < instr->arg_count; a++) {
                            if (instr->arg_ids[a] >= 0 && instr->arg_ids[a] <= max_temp)
                                temp_used[instr->arg_ids[a]] = 1;
                        }
                    }
                    break;
                default:
                    if (instr->src_id >= 0 && instr->src_id <= max_temp) temp_used[instr->src_id] = 1;
                    if (instr->src2_id >= 0 && instr->src2_id <= max_temp) temp_used[instr->src2_id] = 1;
                    break;
            }
        }

        /* Iteratively propagate: if a temp is used, its def's sources are used */
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
                    /* This temp is used, mark its sources */
                    if (instr->src_id >= 0 && instr->src_id <= max_temp) {
                        if (!temp_used[instr->src_id]) {
                            temp_used[instr->src_id] = 1;
                            changed = 1;
                        }
                    }
                    if (instr->src2_id >= 0 && instr->src2_id <= max_temp) {
                        if (!temp_used[instr->src2_id]) {
                            temp_used[instr->src2_id] = 1;
                            changed = 1;
                        }
                    }
                }
            }
        }

        /* Kill instructions whose result is unused */
        for (int i = 0; i < prog->count; i++) {
            IRInstr *instr = &prog->instrs[i];
            if (instr->kind == IR_NOP) continue;
            if (!ir_produces_value(instr)) continue;

            /* Never kill LABELs, BRs, CALLs (side effects), STOREs, RETs, ALLOCs */
            switch (instr->kind) {
                case IR_LABEL: case IR_BR: case IR_BR_IF: case IR_BR_IF_NOT:
                case IR_CALL: case IR_CALL_IND: case IR_RET:
                case IR_STORE: case IR_ALLOC: case IR_GLOBAL_STR:
                case IR_GLOBAL_DATA: case IR_PARAM: case IR_ARG:
                case IR_SYSCALL:
                    continue;
                default:
                    break;
            }

            int tid = instr->temp_id;
            if (tid >= 0 && tid <= max_temp && !temp_used[tid]) {
                instr->kind = IR_NOP;
            }
        }

        free(temp_used);
    }

    /* Compact */
    compact_program(prog);
}

/* ==================================================================
 * PASS 3: Common Subexpression Elimination (within basic blocks)
 *
 * For each instruction, hash by (kind, src_id, src2_id).
 * If an identical instruction exists earlier and its result is still
 * live, replace references with the earlier result.
 * ================================================================== */
void opt_cse_basic_block(IRProgram *prog)
{
    if (!prog) return;

    /* Simple approach: for each instruction, look back in the current
       basic block (since last label/branch) for an equivalent expression. */

    /* Identify basic block boundaries */
    int *bb_start = malloc((size_t)prog->count * sizeof(int));
    int bb_count = 0;
    int current_bb_start = 0;

    for (int i = 0; i < prog->count; i++) {
        IRInstr *instr = &prog->instrs[i];
        if (instr->kind == IR_NOP) continue;

        /* A new basic block starts at: LABEL, or after any terminator
           (BR, BR_IF, BR_IF_NOT, RET). These instructions end the current
           basic block because control flow diverges. */
        if (instr->kind == IR_LABEL) {
            if (i != current_bb_start) {
                bb_start[bb_count++] = i;
                current_bb_start = i;
            }
        } else if (instr->kind == IR_BR ||
                   instr->kind == IR_BR_IF ||
                   instr->kind == IR_BR_IF_NOT ||
                   instr->kind == IR_RET) {
            /* Current BB ends at this terminator instruction.
               Next non-NOP instruction starts a new BB (fall-through). */
            bb_start[bb_count++] = current_bb_start;
            /* Find next instruction */
            int next = i + 1;
            while (next < prog->count && prog->instrs[next].kind == IR_NOP) next++;
            if (next < prog->count) {
                current_bb_start = next;
            }
        }
    }
    /* Last BB */
    if (current_bb_start < prog->count) {
        bb_start[bb_count++] = current_bb_start;
    }

    /* For each BB, do CSE */
    for (int bb = 0; bb < bb_count; bb++) {
        int start = bb_start[bb];
        int end = (bb + 1 < bb_count) ? bb_start[bb + 1] : prog->count;

        for (int i = start; i < end; i++) {
            IRInstr *instr = &prog->instrs[i];
            if (instr->kind == IR_NOP) continue;
            if (!ir_produces_value(instr)) continue;

            /* Only CSE commutative binary ops and some unary ops */
            int cseable = 0;
            if (is_binary_arith(instr->kind)) cseable = 1;
            else if (is_comparison(instr->kind)) cseable = 1;
            else if (instr->kind == IR_LOAD) cseable = 1;

            if (!cseable) continue;

            /* Look back in this BB for equivalent expression */
            for (int j = start; j < i; j++) {
                IRInstr *prev = &prog->instrs[j];
                if (prev->kind == IR_NOP) continue;
                if (!ir_produces_value(prev)) continue;
                if (prev->kind != instr->kind) continue;

                /* Check operand equality (handle commutativity) */
                int match = 0;
                if (prev->src_id == instr->src_id && prev->src2_id == instr->src2_id) {
                    match = 1;
                }
                /* Commutative ops */
                if (instr->kind == IR_ADD || instr->kind == IR_MUL ||
                    instr->kind == IR_BAND || instr->kind == IR_BOR ||
                    instr->kind == IR_XOR ||
                    instr->kind == IR_CMP_EQ || instr->kind == IR_CMP_NEQ) {
                    if (prev->src_id == instr->src2_id && prev->src2_id == instr->src_id) {
                        match = 1;
                    }
                }

                if (match) {
                    /* Replace all uses of instr->temp_id with prev->temp_id
                     * ONLY within the current basic block [start, end) */
                    replace_temp(prog, instr->temp_id, prev->temp_id, start, end);
                    instr->kind = IR_NOP;
                    break;
                }
            }
        }
    }

    free(bb_start);

    /* Compact */
    compact_program(prog);
}

/* ==================================================================
 * PASS 4: Copy Propagation
 *
 * If IR_MOV t1 = t2 and t1 is only used (not redefined),
 * replace all uses of t1 with t2.
 * ================================================================== */
void opt_copy_propagation(IRProgram *prog)
{
    if (!prog) return;

    /* Iterate until no more changes (fixed-point) */
    int changed = 1;
    while (changed) {
        changed = 0;

        for (int i = 0; i < prog->count; i++) {
            IRInstr *instr = &prog->instrs[i];
            if (instr->kind != IR_MOV) continue;

            int t1 = instr->temp_id;   /* t1 = t2 */
            int t2 = instr->src_id;

            if (t1 < 0 || t2 < 0) continue;
            if (t1 == t2) continue;

            /* Check that t1 is NOT redefined after this MOV */
            int redefined = 0;
            for (int j = i + 1; j < prog->count; j++) {
                if (prog->instrs[j].kind == IR_NOP) continue;
                if (prog->instrs[j].temp_id == t1) {
                    redefined = 1;
                    break;
                }
            }
            if (redefined) continue;

            /* Check that t1 is actually used somewhere after this MOV */
            int used = 0;
            for (int j = i + 1; j < prog->count; j++) {
                if (prog->instrs[j].kind == IR_NOP) continue;
                if (prog->instrs[j].src_id == t1 || prog->instrs[j].src2_id == t1) {
                    used = 1;
                    break;
                }
                if (prog->instrs[j].arg_ids) {
                    for (int a = 0; a < prog->instrs[j].arg_count; a++) {
                        if (prog->instrs[j].arg_ids[a] == t1) {
                            used = 1;
                            break;
                        }
                    }
                }
            }
            if (!used) continue;

            /* Propagate: replace all uses of t1 with t2 (globally) */
            replace_temp(prog, t1, t2, -1, -1);
            instr->kind = IR_NOP;
            changed = 1;
        }
    }

    /* Compact */
    compact_program(prog);
}

/* ==================================================================
 * Run all passes in order
 * ================================================================== */
void opt_run_all(IRProgram *prog)
{
    if (!prog) return;
    opt_constant_fold(prog);
    opt_dead_code_elim(prog);
    opt_cse_basic_block(prog);
    opt_copy_propagation(prog);
}
