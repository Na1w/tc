/* tc/src/x86_64.c  --  x86-64 code generator for tc compiler
 *
 * Implements a complete linear-scan register allocator and
 * ELF64 binary emitter targeting the Linux x86-64 System V ABI.
 *
 * Modules in this file:
 *   - Temp location helpers (alloc)
 *   - Register allocator (linear scan) (alloc)
 *   - Call convention (prologue/epilogue, caller-saved) (callconv)
 *   - Entry point (_start) (entry)
 *   - Code gen entry points (cg_create, cg_destroy, cg_generate) (main)
 *
 * Instruction emission: x86_64_instr.c
 * Emit helpers:         x86_64_emit.c
 */

#include "x86_64.h"
#include "x86_64_emit.h"
#include "x86_64_instr.h"
#include "elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Caller-saved registers (used by call convention helpers)          */
/* ------------------------------------------------------------------ */

static const int CALLER_SAVED[] = {
    R_RAX, R_RCX, R_RDX, R_RSI, R_RDI, R_R8, R_R9, R_R10, R_R11
};
#define CALLER_SAVED_COUNT 9

/* ABI parameter registers (System V AMD64) */
const int ABI_PARAM_REGS[] = {
    R_RDI, R_RSI, R_RDX, R_RCX, R_R8, R_R9
};
#define ABI_PARAM_REG_COUNT 6

/* Allocatable registers (rsp and rbp reserved) */
static const int ALLOC_REGS[] = {
    R_RAX, R_RCX, R_RDX, R_RBX, R_RSI, R_RDI,
    R_R8, R_R9, R_R10, R_R11, R_R12, R_R13, R_R14, R_R15
};
#define ALLOC_REG_COUNT 14

/* ------------------------------------------------------------------ */
/*  Temp location helpers                                              */
/* ------------------------------------------------------------------ */

static void temp_init(CodeGen *cg)
{
    memset(cg->temps, 0, sizeof(cg->temps));
    cg->max_temp = 0;
}

static void temp_set_reg(CodeGen *cg, int tid, int reg)
{
    if (tid < 0 || tid >= MAX_TEMP) return;
    cg->temps[tid].assigned = 1;
    cg->temps[tid].is_reg = 1;
    cg->temps[tid].reg_code = reg;
    cg->temps[tid].stack_off = 0;
    if (tid > cg->max_temp)
        cg->max_temp = tid;
}

static void temp_set_stack(CodeGen *cg, int tid, int offset)
{
    if (tid < 0 || tid >= MAX_TEMP) return;
    cg->temps[tid].assigned = 1;
    cg->temps[tid].is_reg = 0;
    cg->temps[tid].stack_off = offset;
    cg->temps[tid].reg_code = 0;
    if (tid > cg->max_temp)
        cg->max_temp = tid;
}

int temp_is_assigned(CodeGen *cg, int tid)
{
    if (tid < 0 || tid >= MAX_TEMP) return 0;
    return cg->temps[tid].assigned;
}

int temp_get_reg(CodeGen *cg, int tid)
{
    if (!temp_is_assigned(cg, tid)) return -1;
    if (cg->temps[tid].is_reg) return cg->temps[tid].reg_code;
    return -1;
}

int temp_get_stack_off(CodeGen *cg, int tid)
{
    if (!temp_is_assigned(cg, tid)) return 0;
    return cg->temps[tid].stack_off;
}

/* ------------------------------------------------------------------ */
/*  Load temp into a register (spills from stack if needed)            */
/*  If avoid_reg >= 0, the function will try not to use that register  */
/*  as scratch.  Returns the register containing the temp's value.     */
/* ------------------------------------------------------------------ */

int cg_load_temp(CodeGen *cg, int tid, int avoid_reg)
{
    if (!temp_is_assigned(cg, tid)) return -1;  // Signal unassigned
    int reg = temp_get_reg(cg, tid);
    if (reg >= 0) return reg;
    /* Spilled to stack: load into a scratch register */
    int scratch = R_RAX;
    if (avoid_reg >= 0 && scratch == avoid_reg)
        scratch = R_RCX;
    emit_mov_rmem_rbp(cg, scratch, temp_get_stack_off(cg, tid));
    return scratch;
}

/* Store rax to temp's location */
void cg_store_temp(CodeGen *cg, int tid)
{
    if (!temp_is_assigned(cg, tid)) return;
    int reg = temp_get_reg(cg, tid);
    if (reg >= 0 && reg != R_RAX) {
        emit_mov_rr(cg, reg, R_RAX);
    } else if (reg < 0) {
        emit_mov_mem_rbp_r(cg, R_RAX, temp_get_stack_off(cg, tid));
    }
    /* if reg == RAX, value is already there */
}

/* ------------------------------------------------------------------ */
/*  Register allocator: linear scan                                    */
/* ------------------------------------------------------------------ */

static int interval_cmp(const void *a, const void *b)
{
    const LiveInterval *ia = (const LiveInterval *)a;
    const LiveInterval *ib = (const LiveInterval *)b;
    if (ia->start < ib->start) return -1;
    if (ia->start > ib->start) return 1;
    return 0;
}

static int is_callee_saved(int reg)
{
    switch (reg) {
        case R_RBX: return 1;
        case R_R12: return 1;
        case R_R13: return 1;
        case R_R14: return 1;
        case R_R15: return 1;
        default: return 0;
    }
}

static int reg_to_cs_bit(int reg)
{
    switch (reg) {
        case R_RBX: return CS_RBX;
        case R_R12: return CS_R12;
        case R_R13: return CS_R13;
        case R_R14: return CS_R14;
        case R_R15: return CS_R15;
        default: return -1;
    }
}

static void cg_compute_intervals(CodeGen *cg, IRProgram *prog)
{
    cg->interval_count = 0;
    /* Initialize intervals for all possible temps */
    for (int i = 0; i < MAX_TEMP; i++) {
        cg->intervals[i].temp_id = i;
        cg->intervals[i].start = prog->count + 1; /* sentinel */
        cg->intervals[i].end = -1;
    }

    /* Scan IR to find definitions and uses */
    for (int i = 0; i < prog->count; i++) {
        IRInstr *inst = &prog->instrs[i];

        /* Definition */
        if (inst->temp_id >= 0) {
            int tid = inst->temp_id;
            if (i < cg->intervals[tid].start)
                cg->intervals[tid].start = i;
        }

        /* Uses */
        if (inst->src_id >= 0) {
            int tid = inst->src_id;
            if (i > cg->intervals[tid].end)
                cg->intervals[tid].end = i;
        }
        if (inst->src2_id >= 0) {
            int tid = inst->src2_id;
            if (i > cg->intervals[tid].end)
                cg->intervals[tid].end = i;
        }
        /* CALL arg_ids */
        if (inst->kind == IR_CALL || inst->kind == IR_CALL_IND) {
            for (int a = 0; a < inst->arg_count; a++) {
                int tid = inst->arg_ids[a];
                if (i > cg->intervals[tid].end)
                    cg->intervals[tid].end = i;
            }
        }
    }

    /* Collect valid intervals */
    for (int i = 0; i < MAX_TEMP; i++) {
        if (cg->intervals[i].end >= 0 && cg->intervals[i].start <= cg->intervals[i].end) {
            LiveInterval *tmp = &cg->intervals[i];
            cg->intervals[cg->interval_count].temp_id = tmp->temp_id;
            cg->intervals[cg->interval_count].start = tmp->start;
            cg->intervals[cg->interval_count].end = tmp->end;
            cg->interval_count++;
        }
    }

    /* Sort by start */
    if (cg->interval_count > 1) {
        qsort(cg->intervals, (size_t)cg->interval_count,
              sizeof(LiveInterval), interval_cmp);
    }

}

static void cg_linear_scan(CodeGen *cg)
{
    /* Initialize free registers */
    cg->free_reg_count = ALLOC_REG_COUNT;
    for (int i = 0; i < ALLOC_REG_COUNT; i++)
        cg->free_regs[i] = ALLOC_REGS[i];
    memset(cg->reg_owner, -1, sizeof(cg->reg_owner));

    cg->callee_saved_mask = 0;
    cg->frame_size = 0;
    int spill_next = 0; /* next spill slot (negative from rbp) */

    /* Track active intervals and their assigned registers */
    typedef struct {
        int interval_idx;
        int reg;
        int end;
    } Active;
    Active active[ALLOC_REG_COUNT];
    int active_count = 0;

    for (int i = 0; i < cg->interval_count; i++) {
        LiveInterval *iv = &cg->intervals[i];

        /* Expire old intervals */
        for (int j = 0; j < active_count; j++) {
            if (active[j].end < iv->start) {
                /* Free this register */
                int reg = active[j].reg;
                cg->reg_owner[reg] = -1;
                cg->free_regs[cg->free_reg_count++] = reg;
                /* Clear callee-saved mask bit when a callee-saved reg is freed */
                if (is_callee_saved(reg)) {
                    int bit = reg_to_cs_bit(reg);
                    if (bit >= 0)
                        cg->callee_saved_mask &= ~(1 << bit);
                }
                /* Remove from active list */
                active[j] = active[active_count - 1];
                active_count--;
                j--;
            }
        }

        if (cg->free_reg_count > 0) {
            /* Assign a free register */
            int reg = cg->free_regs[--cg->free_reg_count];
            cg->reg_owner[reg] = i;
            temp_set_reg(cg, iv->temp_id, reg);
            if (is_callee_saved(reg)) {
                int bit = reg_to_cs_bit(reg);
                if (bit >= 0)
                    cg->callee_saved_mask |= (1 << bit);
            }
            /* Add to active list */
            if (active_count < ALLOC_REG_COUNT) {
                active[active_count].interval_idx = i;
                active[active_count].reg = reg;
                active[active_count].end = iv->end;
                active_count++;
            }
        } else {
            /* Need to spill. Find the interval that ends furthest in the future */
            int spill_idx = -1;
            int spill_end = -1;
            for (int j = 0; j < active_count; j++) {
                if (active[j].end > spill_end) {
                    spill_end = active[j].end;
                    spill_idx = j;
                }
            }

            if (spill_idx >= 0 && spill_end > iv->end) {
                /* Spill the active interval that ends furthest */
                Active *sa = &active[spill_idx];
                int old_reg = sa->reg;
                int old_tid = cg->intervals[sa->interval_idx].temp_id;

                /* Move old temp to stack */
                int slot = -8 * (1 + spill_next);
                spill_next++;
                temp_set_stack(cg, old_tid, slot);
                if (-slot > cg->frame_size)
                    cg->frame_size = -slot;

                /* Free old register */
                cg->reg_owner[old_reg] = -1;
                cg->free_regs[cg->free_reg_count++] = old_reg;

                /* Assign new temp to this register */
                cg->reg_owner[old_reg] = i;
                temp_set_reg(cg, iv->temp_id, old_reg);
                if (is_callee_saved(old_reg)) {
                    int bit = reg_to_cs_bit(old_reg);
                    if (bit >= 0)
                        cg->callee_saved_mask |= (1 << bit);
                }

                /* Update active entry */
                sa->interval_idx = i;
                sa->reg = old_reg;
                sa->end = iv->end;
            } else {
                /* Spill current interval */
                int slot = -8 * (1 + spill_next);
                spill_next++;
                temp_set_stack(cg, iv->temp_id, slot);
                if (-slot > cg->frame_size)
                    cg->frame_size = -slot;
            }
        }
    }

    /* Align frame_size to 16 bytes */
    cg->frame_size = (cg->frame_size + 15) & ~15;
}

/* ------------------------------------------------------------------ */
/*  Spill/reload helpers for CALL instructions                         */
/* ------------------------------------------------------------------ */

/* Before a call: save all caller-saved regs that hold live temps */
void cg_save_caller_saved(CodeGen *cg)
{
    /* Save caller-saved regs EXCEPT RAX (return value) before call.
     * RAX will be overwritten by the callee's return value. */
    for (int i = 0; i < CALLER_SAVED_COUNT; i++) {
        if (CALLER_SAVED[i] == R_RAX) continue;
        emit_push_r(cg, CALLER_SAVED[i]);
    }
    cg->pushed_bytes += 8 * (CALLER_SAVED_COUNT - 1);
}

/* After a call: restore caller-saved regs (not RAX - it has return value) */
void cg_restore_caller_saved(CodeGen *cg)
{
    /* Pop in reverse order, skip RAX */
    for (int i = CALLER_SAVED_COUNT - 1; i >= 0; i--) {
        if (CALLER_SAVED[i] == R_RAX) continue;
        emit_pop_r(cg, CALLER_SAVED[i]);
    }
    cg->pushed_bytes -= 8 * (CALLER_SAVED_COUNT - 1);
}

/* ------------------------------------------------------------------ */
/*  Stack alignment before CALL                                        */
/* ------------------------------------------------------------------ */

void cg_emit_call_align(CodeGen *cg)
{
    int cs_count = 0;
    for (int b = 0; b < CS_COUNT; b++) {
        if (cg->callee_saved_mask & (1 << b))
            cs_count++;
    }
    int total_pushes = 1 + cs_count;
    if ((total_pushes & 1) == 1) {
        emit_push_r(cg, R_RAX);
        cg->pushed_bytes += 8;
    }
}

void cg_emit_call_unalign(CodeGen *cg)
{
    int cs_count = 0;
    for (int b = 0; b < CS_COUNT; b++) {
        if (cg->callee_saved_mask & (1 << b))
            cs_count++;
    }
    int total_pushes = 1 + cs_count;
    if ((total_pushes & 1) == 1) {
        emit_pop_r(cg, R_RAX);
        cg->pushed_bytes -= 8;
    }
}

/* ------------------------------------------------------------------ */
/*  Emit prologue                                                      */
/* ------------------------------------------------------------------ */

static void cg_emit_prologue(CodeGen *cg, int param_count)
{
    /* push rbp */
    emit_push_r(cg, R_RBP);
    /* mov rbp, rsp */
    emit_mov_rr(cg, R_RBP, R_RSP);

    /* Push callee-saved regs (order: rbx, r12, r13, r14, r15) */
    static const int CS_REGS[] = { R_RBX, R_R12, R_R13, R_R14, R_R15 };
    for (int b = 0; b < CS_COUNT; b++) {
        if (cg->callee_saved_mask & (1 << b)) {
            emit_push_r(cg, CS_REGS[b]);
        }
    }

    /* sub rsp, frame_size */
    if (cg->frame_size > 0) {
        emit_sub_rsp_imm32(cg, (int32_t)cg->frame_size);
    }

    /* Save ABI parameter registers to [rbp+16+n*8]
     * 
     * After push rbp + mov rbp,rsp:
     *   [rbp+8]  = return address
     *   [rbp+16] = first param (was rdi)
     *   [rbp+24] = second param (was rsi)
     *   etc.
     * 
     * IR_ADDR_PARAM generates: lea rX, [rbp + 16 + n*8]
     * So we need to save the ABI registers to these locations.
     */
    for (int i = 0; i < param_count && i < ABI_PARAM_REG_COUNT; i++) {
        int disp = 16 + i * 8;
        emit_mov_mem_rbp_r(cg, ABI_PARAM_REGS[i], (int32_t)disp);
    }
}

/* ------------------------------------------------------------------ */
/*  Emit epilogue                                                      */
/* ------------------------------------------------------------------ */

void cg_emit_epilogue(CodeGen *cg)
{
    /* First restore rsp past the local variable area */
    if (cg->frame_size > 0) {
        /* add rsp, frame_size  (48 81 C4 imm32) */
        cg_emit_byte(cg, 0x48);
        cg_emit_byte(cg, 0x81);
        cg_emit_byte(cg, 0xC4);
        cg_emit_int32(cg, (int32_t)cg->frame_size);
    }

    /* Pop callee-saved regs in reverse order
     *
     * CRITICAL: Must pop BEFORE add rsp, frame_size because
     * callee-saved regs were pushed BEFORE sub rsp, frame_size
     * in the prologue.  The stack layout is:
     *
     *   [rbp]          <- saved RBP
     *   [rbp+8]        <- return address
     *   [rbp+16..]     <- pushed callee-saved regs (rbx, r12, r13, r14, r15)
     *   [rbp+16+8*N]   <- local variable area (frame_size bytes)
     *
     * After "add rsp, frame_size", RSP points to the local variable
     * area boundary.  Pops must happen while RSP is still pointing
     * past the local variable area, so we pop first, then restore rsp.
     *
     * Wait - actually the stack grows DOWN. Let me reconsider:
     *
     * Prologue: push rbp, mov rbp,rsp, push cs-regs, sub rsp,frame_size
     *   RSP = RBP - (N*8 + frame_size)
     *   Callee-saved regs live at [RBP - 8], [RBP - 16], ...
     *
     * Epilogue needs: add rsp, frame_size -> RSP = RBP - N*8
     *   then pop cs-regs from [RBP - 8], [RBP - 16], ...
     *   then pop rbp, ret
     *
     * Actually the current order (add rsp first, then pop) IS correct
     * for the stack layout. The real issue is different.
     */
    static const int CS_REGS[] = { R_RBX, R_R12, R_R13, R_R14, R_R15 };
    for (int b = CS_COUNT - 1; b >= 0; b--) {
        if (cg->callee_saved_mask & (1 << b)) {
            emit_pop_r(cg, CS_REGS[b]);
        }
    }

    /* pop rbp */
    emit_pop_r(cg, R_RBP);
    /* ret */
    emit_ret(cg);
}

/* ------------------------------------------------------------------ */
/*  Emit _start entry point                                            */
/* ------------------------------------------------------------------ */

static void cg_emit_start(CodeGen *cg)
{
    /* Record _start label */
    cg_add_label(cg, "_start", (int)cg->code_size);

    /* mov rdi, [rsp] -- load argc */
    cg_emit_byte(cg, 0x48);
    cg_emit_byte(cg, 0x8B);
    cg_emit_byte(cg, 0x3C);
    cg_emit_byte(cg, 0x24);

    /* mov rsi, [rsp+8] -- load argv */
    cg_emit_byte(cg, 0x48);
    cg_emit_byte(cg, 0x8B);
    cg_emit_byte(cg, 0x74);
    cg_emit_byte(cg, 0x24);
    cg_emit_byte(cg, 8);

    /* and rsp, -16 */
    cg_emit_byte(cg, 0x48);
    cg_emit_byte(cg, 0x83);
    cg_emit_byte(cg, 0xE4);
    cg_emit_byte(cg, (uint8_t)(-16));

    /* sub rsp, 8 */
    cg_emit_byte(cg, 0x48);
    cg_emit_byte(cg, 0x83);
    cg_emit_byte(cg, 0xEC);
    cg_emit_byte(cg, 8);

    /* call main (patch later) */
    cg_emit_byte(cg, 0xE8);
    int call_offset = (int)cg->code_size;
    cg_emit_int32(cg, 0);
    if (cg->patch_count < MAX_PATCHES) {
        /* Don't create a "main" label here with offset 0 — that would
         * collide with _start at offset 0 and cause cg_patch_branches
         * to overwrite the first instruction of _start with null bytes.
         * The "main" label will be created when cg_emit_function runs. */
        int lbl = cg_find_label(cg, "main");
        if (lbl < 0) {
            /* Reserve a new label slot but mark it as unresolved (-1).
             * cg_emit_function will update the offset later. */
            lbl = cg_add_label(cg, "main", -1);
        }
        cg->patches[cg->patch_count].code_offset = call_offset;
        cg->patches[cg->patch_count].target_label_idx = lbl;
        cg->patches[cg->patch_count].patch_size = 4;
        cg->patch_count++;
    }

    /* mov rdi, rax (main's return value -> exit status) */
    emit_mov_rr(cg, R_RDI, R_RAX);
    /* mov rax, 231 (exit_group syscall on Linux x86-64) */
    emit_mov_rimm(cg, R_RAX, 231);
    /* syscall */
    emit_syscall(cg);
}

/* ------------------------------------------------------------------ */
/*  Remap temp_ids in a function range to be unique (0,1,2,...)        */
/*  This prevents temp_id collisions across multiple functions.        */
/* ------------------------------------------------------------------ */

/* Check if instruction kind defines a temp (has temp_id as destination) */
static int instr_defines_temp(int kind)
{
    switch (kind) {
        case IR_MOV: case IR_CONST: case IR_ADDR_GLOBAL: case IR_ADDR_LOCAL:
        case IR_LOAD: case IR_ADDR_PARAM: case IR_CALL: case IR_CALL_IND:
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
        case IR_BAND: case IR_BOR: case IR_XOR: case IR_SHL: case IR_SHR:
        case IR_NEG: case IR_NOT: case IR_BITNOT:
        case IR_CMP_EQ: case IR_CMP_NEQ: case IR_CMP_LT: case IR_CMP_GT:
        case IR_CMP_LEQ: case IR_CMP_GEQ:
            return 1;
        default:
            return 0;
    }
}

/* Check if instruction kind uses src_id */
static int instr_uses_src(int kind)
{
    switch (kind) {
        case IR_MOV: case IR_LOAD: case IR_STORE:
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
        case IR_BAND: case IR_BOR: case IR_XOR: case IR_SHL: case IR_SHR:
        case IR_NEG: case IR_NOT: case IR_BITNOT:
        case IR_CMP_EQ: case IR_CMP_NEQ: case IR_CMP_LT: case IR_CMP_GT:
        case IR_CMP_LEQ: case IR_CMP_GEQ:
        case IR_BR_IF: case IR_BR_IF_NOT:
        case IR_RET: case IR_CALL_IND:
            return 1;
        default:
            return 0;
    }
}

/* Check if instruction kind uses src2_id */
static int instr_uses_src2(int kind)
{
    switch (kind) {
        case IR_STORE:
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
        case IR_BAND: case IR_BOR: case IR_XOR: case IR_SHL: case IR_SHR:
        case IR_CMP_EQ: case IR_CMP_NEQ: case IR_CMP_LT: case IR_CMP_GT:
        case IR_CMP_LEQ: case IR_CMP_GEQ:
            return 1;
        default:
            return 0;
    }
}

static void cg_remap_temps_func(IRInstr *instrs, int start, int end, int base)
{
    /* First pass: find max temp_id in this range */
    int max_tid = -1;
    for (int i = start; i < end; i++) {
        IRInstr *inst = &instrs[i];
        if (inst->temp_id > max_tid) max_tid = inst->temp_id;
        if (inst->src_id > max_tid) max_tid = inst->src_id;
        if (inst->src2_id > max_tid) max_tid = inst->src2_id;
        if (inst->kind == IR_CALL || inst->kind == IR_CALL_IND) {
            for (int a = 0; a < inst->arg_count; a++) {
                if (inst->arg_ids[a] > max_tid) max_tid = inst->arg_ids[a];
            }
        }
    }
    if (max_tid < 0) return;

    /*
     * Each DEFINITION of a temp gets a UNIQUE new ID, even if the old
     * temp_id was reused.  This is required because the register
     * allocator relies on per-definition live intervals.  Reusing an
     * old temp_id creates a merged live range that spans both
     * definitions, causing the allocator to assign the same register
     * to two overlapping (but semantically different) values.
     *
     * We do this in a single forward pass:
     *  - current_def[old_id] tracks the new_id of the most recent
     *    definition of each old temp_id.
     *  - When we see a definition, we assign a new ID and update
     *    current_def.
     *  - When we see a use, we remap to current_def[old_id].
     */

    int current_def[MAX_TEMP];  /* old_id -> new_id of current definition */
    int next_new = base;
    for (int i = 0; i <= max_tid; i++)
        current_def[i] = -1;

    for (int i = start; i < end; i++) {
        IRInstr *inst = &instrs[i];

        /* Remap uses BEFORE remapping the definition on this instruction */
        if (instr_uses_src(inst->kind) && inst->src_id >= 0) {
            int old_tid = inst->src_id;
            if (current_def[old_tid] >= 0)
                inst->src_id = current_def[old_tid];
        }
        if (instr_uses_src2(inst->kind) && inst->src2_id >= 0) {
            int old_tid = inst->src2_id;
            if (current_def[old_tid] >= 0)
                inst->src2_id = current_def[old_tid];
        }
        if (inst->kind == IR_CALL || inst->kind == IR_CALL_IND) {
            for (int a = 0; a < inst->arg_count; a++) {
                if (inst->arg_ids[a] >= 0) {
                    int old_tid = inst->arg_ids[a];
                    if (current_def[old_tid] >= 0)
                        inst->arg_ids[a] = current_def[old_tid];
                }
            }
        }

        /* Remap definition (temp_id) */
        if (instr_defines_temp(inst->kind) && inst->temp_id >= 0) {
            int old_tid = inst->temp_id;
            int new_id = next_new++;
            inst->temp_id = new_id;
            current_def[old_tid] = new_id;
        }
    }
}
static void cg_emit_function(CodeGen *cg, IRProgram *prog, int start, int end, int param_count)
{
    /* Remap temp_ids for this function to be unique */
    cg_remap_temps_func(prog->instrs, start, end, 0);

    /* Reset temp assignments for per-function register allocation */
    temp_init(cg);

    /* Compute live intervals for this function range */
    cg->interval_count = 0;
    for (int i = 0; i < MAX_TEMP; i++) {
        cg->intervals[i].temp_id = i;
        cg->intervals[i].start = end + 1;
        cg->intervals[i].end = -1;
    }

    for (int i = start; i < end; i++) {
        IRInstr *inst = &prog->instrs[i];

        if (instr_defines_temp(inst->kind) && inst->temp_id >= 0) {
            int tid = inst->temp_id;
            if (i < cg->intervals[tid].start)
                cg->intervals[tid].start = i;
        }

        if (instr_uses_src(inst->kind) && inst->src_id >= 0) {
            int tid = inst->src_id;
            if (i > cg->intervals[tid].end)
                cg->intervals[tid].end = i;
        }
        if (instr_uses_src2(inst->kind) && inst->src2_id >= 0) {
            int tid = inst->src2_id;
            if (i > cg->intervals[tid].end)
                cg->intervals[tid].end = i;
        }
        if (inst->kind == IR_CALL || inst->kind == IR_CALL_IND) {
            for (int a = 0; a < inst->arg_count; a++) {
                int tid = inst->arg_ids[a];
                if (i > cg->intervals[tid].end)
                    cg->intervals[tid].end = i;
            }
        }
    }

    /* Collect valid intervals */
    for (int i = 0; i < MAX_TEMP; i++) {
        if (cg->intervals[i].end >= 0 && cg->intervals[i].start <= cg->intervals[i].end) {
            LiveInterval *tmp = &cg->intervals[i];
            cg->intervals[cg->interval_count].temp_id = tmp->temp_id;
            cg->intervals[cg->interval_count].start = tmp->start;
            cg->intervals[cg->interval_count].end = tmp->end;
            cg->interval_count++;
        }
    }

    if (cg->interval_count > 1) {
        qsort(cg->intervals, (size_t)cg->interval_count,
              sizeof(LiveInterval), interval_cmp);
    }

    /* Reset register allocator state */
    cg->free_reg_count = ALLOC_REG_COUNT;
    for (int i = 0; i < ALLOC_REG_COUNT; i++)
        cg->free_regs[i] = ALLOC_REGS[i];
    memset(cg->reg_owner, -1, sizeof(cg->reg_owner));

    /* Collect callee-saved mask from this function's allocation */
    int func_callee_saved_mask = 0;
    int func_frame_size = 0;

    /* Run linear scan for this function */
    {
        int spill_next = 0;
        typedef struct {
            int interval_idx;
            int reg;
            int end;
        } Active;
        Active active[ALLOC_REG_COUNT];
        int active_count = 0;

        for (int i = 0; i < cg->interval_count; i++) {
            LiveInterval *iv = &cg->intervals[i];

            for (int j = 0; j < active_count; j++) {
                if (active[j].end <= iv->start) {
                    int reg = active[j].reg;
                    cg->reg_owner[reg] = -1;
                    cg->free_regs[cg->free_reg_count++] = reg;
                    /* Clear callee-saved mask bit when a callee-saved reg is freed */
                    if (is_callee_saved(reg)) {
                        int bit = reg_to_cs_bit(reg);
                        if (bit >= 0)
                            func_callee_saved_mask &= ~(1 << bit);
                    }
                    active[j] = active[active_count - 1];
                    active_count--;
                    j--;
                }
            }

            if (cg->free_reg_count > 0) {
                int reg = cg->free_regs[--cg->free_reg_count];
                cg->reg_owner[reg] = i;
                temp_set_reg(cg, iv->temp_id, reg);
                if (is_callee_saved(reg)) {
                    int bit = reg_to_cs_bit(reg);
                    if (bit >= 0)
                        func_callee_saved_mask |= (1 << bit);
                }
                if (active_count < ALLOC_REG_COUNT) {
                    active[active_count].interval_idx = i;
                    active[active_count].reg = reg;
                    active[active_count].end = iv->end;
                    active_count++;
                }
            } else {
                int spill_idx = -1;
                int spill_end = -1;
                for (int j = 0; j < active_count; j++) {
                    if (active[j].end > spill_end) {
                        spill_end = active[j].end;
                        spill_idx = j;
                    }
                }

                if (spill_idx >= 0 && spill_end > iv->end) {
                    Active *sa = &active[spill_idx];
                    int old_reg = sa->reg;
                    int old_tid = cg->intervals[sa->interval_idx].temp_id;

                    int slot = -8 * (1 + spill_next);
                    spill_next++;
                    temp_set_stack(cg, old_tid, slot);
                    if (-slot > func_frame_size)
                        func_frame_size = -slot;

                    cg->reg_owner[old_reg] = -1;
                    cg->free_regs[cg->free_reg_count++] = old_reg;

                    cg->reg_owner[old_reg] = i;
                    temp_set_reg(cg, iv->temp_id, old_reg);
                    if (is_callee_saved(old_reg)) {
                        int bit = reg_to_cs_bit(old_reg);
                        if (bit >= 0)
                            func_callee_saved_mask |= (1 << bit);
                    }

                    sa->interval_idx = i;
                    sa->reg = old_reg;
                    sa->end = iv->end;
                } else {
                    int slot = -8 * (1 + spill_next);
                    spill_next++;
                    temp_set_stack(cg, iv->temp_id, slot);
                    if (-slot > func_frame_size)
                        func_frame_size = -slot;
                }
            }
        }

        /* SHIFT SPILL SLOTS AND LOCAL ADDRS to avoid overlapping with pushed
         * callee-saved regs. After prologue, pushed regs occupy
         * [rbp-8] through [rbp - cs_count*8]. Spill slots and local
         * variables must start AFTER those. */
        int cs_count = 0;
        for (int b = 0; b < CS_COUNT; b++) {
            if (func_callee_saved_mask & (1 << b))
                cs_count++;
        }
        if (cs_count > 0) {
            int shift = cs_count * 8;
            /* Shift spill slot offsets */
            for (int t = 0; t < cg->max_temp && t < MAX_TEMP; t++) {
                if (cg->temps[t].assigned && !cg->temps[t].is_reg) {
                    cg->temps[t].stack_off -= shift;
                }
            }
            /* Shift IR_ADDR_LOCAL offsets in the function's IR */
            for (int i = start; i < end; i++) {
                if (prog->instrs[i].kind == IR_ADDR_LOCAL) {
                    prog->instrs[i].ival -= shift;
                }
            }
        }
        /* Recalculate func_frame_size from shifted offsets (spills + locals) */
        func_frame_size = 0;
        for (int t = 0; t < cg->max_temp && t < MAX_TEMP; t++) {
            if (cg->temps[t].assigned && !cg->temps[t].is_reg) {
                int abs_off = -cg->temps[t].stack_off;
                if (abs_off > func_frame_size)
                    func_frame_size = abs_off;
            }
        }
        for (int i = start; i < end; i++) {
            if (prog->instrs[i].kind == IR_ADDR_LOCAL) {
                int abs_off = (int)(-prog->instrs[i].ival);
                if (abs_off > func_frame_size)
                    func_frame_size = abs_off;
            }
        }
        func_frame_size = (func_frame_size + 15) & ~15;
    }

    /* Also account for IR_ALLOC sizes */
    for (int i = start; i < end; i++) {
        if (prog->instrs[i].kind == IR_ALLOC) {
            int needed = (int)prog->instrs[i].ival;
            if (needed > func_frame_size)
                func_frame_size = needed;
        }
    }
    func_frame_size = (func_frame_size + 15) & ~15;

    /* Save original callee_saved_mask and frame_size, set function-specific values */
    int saved_callee_saved_mask = cg->callee_saved_mask;
    int saved_frame_size = cg->frame_size;
    cg->callee_saved_mask = func_callee_saved_mask;
    cg->frame_size = func_frame_size;

    /* Emit function label — update existing label or create new one */
    {
        int existing = cg_find_label(cg, prog->instrs[start].label);
        if (existing >= 0) {
            cg->labels[existing].code_offset = (int)cg->code_size;
        } else {
            cg_add_label(cg, prog->instrs[start].label, (int)cg->code_size);
        }
    }

    /* Emit prologue with param count */
    cg_emit_prologue(cg, param_count);

    /* No pre-initialization of callee-saved registers needed.
     * The body emitter already emits LEA for every ADDR_LOCAL instruction
     * at the correct time (unless pre_init is set).  Since all ADDR_LOCAL
     * temps are emitted in order, each gets its LEA before being consumed.
     * The previous pre-init approach was broken: it emitted LEAs for the
     * LAST ADDR_LOCAL per register, but body LEAs for EARLIER ADDR_LOCALs
     * using the same register would overwrite the pre-init value.
     */
    {
        /* intentionally empty - all ADDR_LOCAL LEAs are emitted by the body */
    }

    /* Emit instructions */
    for (int i = start + 1; i < end; i++) {
        IRInstr *inst = &prog->instrs[i];
        cg_emit_instr(cg, inst);
    }

    /* If no RET was emitted, add default return */
    int has_ret = 0;
    for (int i = start; i < end; i++) {
        if (prog->instrs[i].kind == IR_RET) {
            has_ret = 1;
            break;
        }
    }
    if (!has_ret) {
        emit_mov_rimm(cg, R_RAX, 0);
        cg_emit_epilogue(cg);
    }

    /* Restore original state */
    cg->callee_saved_mask = saved_callee_saved_mask;
    cg->frame_size = saved_frame_size;
}

/* ------------------------------------------------------------------ */
/*  Code generation entry points                                       */
/* ------------------------------------------------------------------ */

CodeGen *cg_create(void)
{
    CodeGen *cg = calloc(1, sizeof(*cg));
    if (!cg) return NULL;
    cg->code_cap = 8192;
    cg->code = malloc(cg->code_cap);
    if (!cg->code) {
        free(cg);
        return NULL;
    }
    cg->rodata_cap = 4096;
    cg->rodata = malloc(cg->rodata_cap);
    if (!cg->rodata) {
        free(cg->code);
        free(cg);
        return NULL;
    }
    cg->data_cap = 4096;
    cg->data = malloc(cg->data_cap);
    if (!cg->data) {
        free(cg->code);
        free(cg->rodata);
        free(cg);
        return NULL;
    }
    memset(cg->code, 0, cg->code_cap);
    memset(cg->rodata, 0, cg->rodata_cap);
    return cg;
}

void cg_destroy(CodeGen *cg)
{
    if (!cg) return;
    free(cg->code);
    free(cg->rodata);
    free(cg->data);
    free(cg);
}

int cg_generate(CodeGen *cg, IRProgram *prog, const char *output_path)
{
    if (!cg || !prog) return -1;

    snprintf(cg->output_path, sizeof(cg->output_path), "%s", output_path);

    /* Reset state */
    cg->code_size = 0;
    cg->rodata_size = 0;
    cg->data_size = 0;
    cg->label_count = 0;
    cg->patch_count = 0;
    cg->global_count = 0;
    cg->param_count = 0;
    cg->frame_size = 0;
    cg->callee_saved_mask = 0;
    cg->pushed_bytes = 0;
    temp_init(cg);

    /* Discover function boundaries: only labels followed by IR_PARAM are functions */
    /* Internal control flow labels (while_X, if_X, etc.) are NOT functions */
    int func_starts[MAX_LABELS];
    int func_param_counts[MAX_LABELS];
    int func_count = 0;

    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == IR_LABEL) {
            /* Check if this label is followed by IR_PARAM (function declaration) */
            int has_params = 0;
            for (int j = i + 1; j < prog->count; j++) {
                if (prog->instrs[j].kind == IR_LABEL) break;
                if (prog->instrs[j].kind == IR_PARAM) {
                    has_params = 1;
                    break;
                }
            }
            /* Only emit as a function if it has params or is named "main" */
            if (has_params || strcmp(prog->instrs[i].label, "main") == 0) {
                func_starts[func_count] = i;

                /* Count params for this function */
                int param_count = 0;
                for (int j = i + 1; j < prog->count; j++) {
                    if (prog->instrs[j].kind == IR_LABEL) break;
                    if (prog->instrs[j].kind == IR_PARAM) {
                        param_count++;
                    }
                }
                func_param_counts[func_count] = param_count;
                func_count++;
            }
        }
    }

    /* Emit _start entry point (calls main) — skip in object mode */
    if (!cg->object_mode) {
        cg_emit_start(cg);
    }

    /* Resolve "main" label placeholder */
    if (!cg->object_mode) {
        for (int i = 0; i < cg->label_count; i++) {
            if (strcmp(cg->labels[i].name, "main") == 0 && cg->labels[i].code_offset < 0) {
                /* Will be set when we emit the main function */
                break;
            }
        }
    }

    /* Emit global data/strings BEFORE any functions (they live outside function boundaries) */
    for (int i = 0; i < prog->count; i++) {
        IRInstr *inst = &prog->instrs[i];
        switch (inst->kind) {
            case IR_GLOBAL_STR:
            case IR_GLOBAL_DATA:
                cg_emit_instr(cg, inst);
                break;
            default:
                break;
        }
    }

    /* Emit each function independently with per-function register allocation */
    for (int f = 0; f < func_count; f++) {
        int start = func_starts[f];
        int end = (f + 1 < func_count) ? func_starts[f + 1] : prog->count;
        int param_count = func_param_counts[f];

        cg_emit_function(cg, prog, start, end, param_count);
    }

    /* Patch branches (internal labels only; skip unresolved externals in object mode) */
    if (!cg->object_mode) {
        cg_patch_branches(cg);
    } else {
        /* In object mode, only patch branches to known internal labels */
        fprintf(stderr, "[OBJ-PATCH] patch_count=%d, label_count=%d\n", cg->patch_count, cg->label_count);
        for (int i = 0; i < cg->patch_count; i++) {
            BranchPatch *patch = &cg->patches[i];
            if (patch->target_label_idx >= 0) {
                int label_off = cg->labels[patch->target_label_idx].code_offset;
                if (label_off >= 0) {
                    /* Internal label - patch it */
                    /* code_offset points to displacement bytes. x86 rel = target - (code_offset + 4). */
                    int32_t rel = label_off - (patch->code_offset + 4);
                    fprintf(stderr, "[OBJ-PATCH] patch[%d]: code_off=%d label='%s' target=%d rel=%d\n",
                            i, patch->code_offset, cg->labels[patch->target_label_idx].name, label_off, rel);
                    uint8_t *target = cg->code + patch->code_offset;
                    memcpy(target, &rel, 4);
                }
                /* External labels (offset < 0) are left unpatched; handled by relocations below */
            }
        }
    }

    /* Write ELF */
    {
        ElfWriter *elf = elf_create();
        if (elf) {
            if (cg->object_mode) {
                /* --- Object mode (ET_REL) --- */
                elf_set_object_mode(elf, 1);

                elf_add_text(elf, cg->code, cg->code_size);
                elf_add_rodata(elf, cg->rodata, cg->rodata_size);
                if (cg->data_size > 0) {
                    elf_add_data(elf, cg->data, cg->data_size, 0);
                }

                /* Define main as a global function symbol in .text (section 1).
                 * Look up the actual offset from the label table instead of hardcoding 0. */
                int main_sym_offset = 0;
                {
                    int main_lbl = cg_find_label(cg, "main");
                    if (main_lbl >= 0)
                        main_sym_offset = cg->labels[main_lbl].code_offset;
                }
                elf_define_symbol_obj(elf, "main", 1, main_sym_offset, STB_GLOBAL, STT_FUNC);

                /* For external calls, add undefined symbols and relocations.
                 * Scan patches: if target label has code_offset < 0, it's external. */
                for (int i = 0; i < cg->patch_count; i++) {
                    const BranchPatch *patch = &cg->patches[i];
                    if (patch->target_label_idx >= 0) {
                        int label_off = cg->labels[patch->target_label_idx].code_offset;
                        if (label_off < 0) {
                            /* External reference - add undefined symbol and relocation */
                            const char *ext_name = cg->labels[patch->target_label_idx].name;
                            size_t sym_idx = elf_extern_symbol(elf, ext_name);
                            /* Add PC32 relocation for call instruction (addend=-4 for PC-relative from next instr) */
                            elf_add_rela_text(elf, (uint64_t)patch->code_offset, sym_idx,
                                              R_X86_64_PC32, -4);
                        }
                    }
                }

                elf_write(elf, cg->output_path);
            } else {
                /* --- Executable mode (ET_EXEC) --- */
                elf_add_text(elf, cg->code, cg->code_size);
                elf_add_rodata(elf, cg->rodata, cg->rodata_size);
                elf->entry = ELF_TEXT_BASE;
                elf_define_symbol(elf, "_start", ELF_TEXT_BASE);
                elf_write(elf, cg->output_path);
            }
            elf_destroy(elf);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Backend init                                                       */
/* ------------------------------------------------------------------ */

void x86_64_init(Backend *be, const char *output)
{
    (void)be;
    (void)output;
}
