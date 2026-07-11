/* tc/src/x86_64_instr.c -- IR instruction dispatch and branch patching */

#include "x86_64_instr.h"
#include "x86_64_emit.h"
#include "elf.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>


/* ------------------------------------------------------------------ */
/*  Label / global lookup                                              */
/* ------------------------------------------------------------------ */

int cg_find_label(CodeGen *cg, const char *name)
{
    for (int i = 0; i < cg->label_count; i++) {
        if (strcmp(cg->labels[i].name, name) == 0)
            return i;
    }
    return -1;
}

int cg_add_label(CodeGen *cg, const char *name, int offset)
{
    if (cg->label_count >= MAX_LABELS) {
        fprintf(stderr, "Too many labels\n");
        return -1;
    }
    LabelEntry *e = &cg->labels[cg->label_count];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->code_offset = offset;
    cg->label_count++;
    return cg->label_count - 1;
}

int cg_find_global(CodeGen *cg, const char *name)
{
    for (int i = 0; i < cg->global_count; i++) {
        if (strcmp(cg->globals[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Instruction dispatch                                               */
/* ------------------------------------------------------------------ */

void cg_emit_instr(CodeGen *cg, IRInstr *inst)
{
    switch (inst->kind) {
        /* Data movement */
        case IR_MOV: {
            if (inst->temp_id < 0) break;
            int src_reg = cg_load_temp(cg, inst->src_id);
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg)
                    emit_mov_rr(cg, dst_reg, src_reg);
            } else {
                if (src_reg != R_RAX)
                    emit_mov_rr(cg, R_RAX, src_reg);
                emit_mov_mem_rbp_r(cg, R_RAX, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        case IR_CONST: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg >= 0) {
                emit_mov_rimm(cg, dst_reg, inst->ival);
            } else {
                emit_mov_rimm(cg, R_RAX, inst->ival);
                emit_mov_mem_rbp_r(cg, R_RAX, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        case IR_ADDR_GLOBAL: {
            if (inst->temp_id < 0) break;
            int gidx = cg_find_global(cg, inst->label);
            int rodata_off = 0;
            if (gidx >= 0)
                rodata_off = cg->globals[gidx].rodata_offset;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;
            int lea_start = (int)cg->code_size;
            emit_lea_r_rip(cg, dst_reg, 0);
            if (cg->patch_count < MAX_PATCHES) {
                int32_t disp = (int32_t)(ELF_RODATA_BASE + (uint64_t)rodata_off - (ELF_TEXT_BASE + (uint64_t)lea_start + 7));
                uint8_t *p = cg->code + lea_start + 3;
                uint8_t *d = (uint8_t *)&disp;
                p[0] = d[0]; p[1] = d[1]; p[2] = d[2]; p[3] = d[3];
                cg->patch_count++;
            }
            if (temp_get_reg(cg, inst->temp_id) < 0) {
                emit_mov_mem_rbp_r(cg, dst_reg, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        case IR_ADDR_LOCAL: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;
            emit_lea_r_rbp(cg, dst_reg, (int32_t)inst->ival);
            if (temp_get_reg(cg, inst->temp_id) < 0) {
                emit_mov_mem_rbp_r(cg, dst_reg, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        case IR_LOAD: {
            if (inst->temp_id < 0) break;
            int addr_reg = cg_load_temp(cg, inst->src_id);
            if (addr_reg != R_RAX)
                emit_mov_rr(cg, R_RAX, addr_reg);
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;

            if (inst->type_size == 8) {
                emit_mov_r_m(cg, dst_reg);
            } else if (inst->type_size == 4) {
                if (inst->is_signed) {
                    uint8_t db = (uint8_t)((dst_reg >= 8) ? 1 : 0);
                    cg_emit_byte(cg, (uint8_t)(0x40 | (db << 2)));
                    cg_emit_byte(cg, 0x63);
                    cg_emit_byte(cg, (uint8_t)(0x00 | ((dst_reg & 7) << 3)));
                } else {
                    emit_mov_r32_mem(cg, dst_reg, R_RAX);
                }
            } else {
                if (inst->is_signed) {
                    emit_movsx_r64_byte(cg, dst_reg, R_RAX);
                } else {
                    emit_movzx_r64_byte(cg, dst_reg, R_RAX);
                }
            }
            if (temp_get_reg(cg, inst->temp_id) < 0) {
                emit_mov_mem_rbp_r(cg, dst_reg, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        case IR_STORE: {
            int addr_reg = cg_load_temp(cg, inst->src_id);
            if (addr_reg != R_RAX)
                emit_mov_rr(cg, R_RAX, addr_reg);
            int val_reg = cg_load_temp(cg, inst->src2_id);

            if (inst->type_size == 8) {
                emit_mov_m_r(cg, val_reg);
            } else if (inst->type_size == 4) {
                emit_mov_mem_r32(cg, val_reg, R_RAX);
            } else {
                emit_mov_r8_mem(cg, val_reg, R_RAX);
            }
            break;
        }

        case IR_ADDR_PARAM: {
            if (inst->temp_id < 0) break;
            int disp = 16 + (int)inst->ival * 8;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;
            emit_lea_r_rbp(cg, dst_reg, (int32_t)disp);
            if (temp_get_reg(cg, inst->temp_id) < 0) {
                emit_mov_mem_rbp_r(cg, dst_reg, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        /* Arithmetic */
        case IR_ADD: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg)
                    emit_mov_rr(cg, dst_reg, src_reg);
                if (dst_reg != src2_reg)
                    emit_add_rr(cg, dst_reg, src2_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                if (R_RAX != src2_reg) emit_add_rr(cg, R_RAX, src2_reg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_SUB: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg)
                    emit_mov_rr(cg, dst_reg, src_reg);
                if (dst_reg != src2_reg)
                    emit_sub_rr(cg, dst_reg, src2_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                if (R_RAX != src2_reg) emit_sub_rr(cg, R_RAX, src2_reg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_MUL: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg)
                    emit_mov_rr(cg, dst_reg, src_reg);
                if (dst_reg != src2_reg)
                    emit_imul_rr(cg, dst_reg, src2_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                if (R_RAX != src2_reg) emit_imul_rr(cg, R_RAX, src2_reg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_DIV: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
            emit_cqo(cg);
            if (src2_reg == R_RAX || src2_reg == R_RDX) {
                emit_mov_rr(cg, R_RCX, src2_reg);
                src2_reg = R_RCX;
            }
            emit_idiv_r(cg, src2_reg);
            if (dst_reg >= 0 && dst_reg != R_RAX) {
                emit_mov_rr(cg, dst_reg, R_RAX);
            } else if (dst_reg < 0) {
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_MOD: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
            emit_cqo(cg);
            if (src2_reg == R_RAX || src2_reg == R_RDX) {
                emit_mov_rr(cg, R_RCX, src2_reg);
                src2_reg = R_RCX;
            }
            emit_idiv_r(cg, src2_reg);
            if (dst_reg >= 0 && dst_reg != R_RDX) {
                emit_mov_rr(cg, dst_reg, R_RDX);
            } else if (dst_reg < 0) {
                emit_mov_mem_rbp_r(cg, R_RDX, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        case IR_BAND: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg) emit_mov_rr(cg, dst_reg, src_reg);
                if (dst_reg != src2_reg) emit_and_rr(cg, dst_reg, src2_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                if (R_RAX != src2_reg) emit_and_rr(cg, R_RAX, src2_reg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_BOR: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg) emit_mov_rr(cg, dst_reg, src_reg);
                if (dst_reg != src2_reg) emit_or_rr(cg, dst_reg, src2_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                if (R_RAX != src2_reg) emit_or_rr(cg, R_RAX, src2_reg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_XOR: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg) emit_mov_rr(cg, dst_reg, src_reg);
                if (dst_reg != src2_reg) emit_xor_rr(cg, dst_reg, src2_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                if (R_RAX != src2_reg) emit_xor_rr(cg, R_RAX, src2_reg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_SHL: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (src2_reg != R_RCX)
                emit_mov_rr(cg, R_RCX, src2_reg);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg) emit_mov_rr(cg, dst_reg, src_reg);
                emit_shl_rcl(cg, dst_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                emit_shl_rcl(cg, R_RAX);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_SHR: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            if (src2_reg != R_RCX)
                emit_mov_rr(cg, R_RCX, src2_reg);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg) emit_mov_rr(cg, dst_reg, src_reg);
                if (inst->is_signed)
                    emit_sar_rcl(cg, dst_reg);
                else
                    emit_shr_rcl(cg, dst_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                if (inst->is_signed)
                    emit_sar_rcl(cg, R_RAX);
                else
                    emit_shr_rcl(cg, R_RAX);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        /* Unary */
        case IR_NEG: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg) emit_mov_rr(cg, dst_reg, src_reg);
                emit_neg_r(cg, dst_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                emit_neg_r(cg, R_RAX);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_NOT: {
            if (inst->temp_id < 0) break;
            int src_reg = cg_load_temp(cg, inst->src_id);
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            emit_test_rr(cg, src_reg, src_reg);
            emit_setcc(cg, 5);
            if (dst_reg >= 0) {
                emit_movzx_r_al(cg, dst_reg);
            } else {
                emit_movzx_rax_al(cg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_BITNOT: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            int src_reg = cg_load_temp(cg, inst->src_id);
            if (dst_reg >= 0) {
                if (dst_reg != src_reg) emit_mov_rr(cg, dst_reg, src_reg);
                emit_not_r(cg, dst_reg);
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                emit_not_r(cg, R_RAX);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        /* Comparisons */
        case IR_CMP_EQ:
        case IR_CMP_NEQ:
        case IR_CMP_LT:
        case IR_CMP_GT:
        case IR_CMP_LEQ:
        case IR_CMP_GEQ: {
            if (inst->temp_id < 0) break;
            int src_reg = cg_load_temp(cg, inst->src_id);
            int src2_reg = cg_load_temp(cg, inst->src2_id);
            int dst_reg = temp_get_reg(cg, inst->temp_id);

            if (src_reg != src2_reg) {
                emit_cmp_rr(cg, src_reg, src2_reg);
            } else {
                emit_xor_rr(cg, src_reg, src_reg);
            }

            int cc = 0;
            switch (inst->kind) {
                case IR_CMP_EQ:  cc = 0x4; break;
                case IR_CMP_NEQ: cc = 0x5; break;
                case IR_CMP_LT:  cc = inst->is_signed ? 0xC : 0x3; break;
                case IR_CMP_GT:  cc = inst->is_signed ? 0xF : 0x7; break;
                case IR_CMP_LEQ: cc = inst->is_signed ? 0xE : 0x2; break;
                case IR_CMP_GEQ: cc = inst->is_signed ? 0xD : 0x6; break;
                default: cc = 0x4; break;
            }

            emit_setcc(cg, cc);
            if (dst_reg >= 0) {
                emit_movzx_r_al(cg, dst_reg);
            } else {
                emit_movzx_rax_al(cg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        /* Control flow */
        case IR_BR: {
            emit_jmp_rel32(cg, 0);
            if (cg->patch_count < MAX_PATCHES) {
                int lbl = cg_find_label(cg, inst->label);
                if (lbl < 0)
                    lbl = cg_add_label(cg, inst->label, -1);
                cg->patches[cg->patch_count].code_offset = (int)cg->code_size - 4;
                cg->patches[cg->patch_count].target_label_idx = lbl;
                cg->patches[cg->patch_count].patch_size = 4;
                cg->patch_count++;
            }
            break;
        }

        case IR_BR_IF: {
            int src_reg = cg_load_temp(cg, inst->src_id);
            emit_test_rr(cg, src_reg, src_reg);
            emit_jcc_rel32(cg, 5, 0);
            if (cg->patch_count < MAX_PATCHES) {
                int lbl = cg_find_label(cg, inst->label);
                if (lbl < 0)
                    lbl = cg_add_label(cg, inst->label, -1);
                cg->patches[cg->patch_count].code_offset = (int)cg->code_size - 4;
                cg->patches[cg->patch_count].target_label_idx = lbl;
                cg->patches[cg->patch_count].patch_size = 4;
                cg->patch_count++;
            }
            break;
        }

        case IR_BR_IF_NOT: {
            int src_reg = cg_load_temp(cg, inst->src_id);
            emit_test_rr(cg, src_reg, src_reg);
            emit_jcc_rel32(cg, 4, 0);
            if (cg->patch_count < MAX_PATCHES) {
                int lbl = cg_find_label(cg, inst->label);
                if (lbl < 0)
                    lbl = cg_add_label(cg, inst->label, -1);
                cg->patches[cg->patch_count].code_offset = (int)cg->code_size - 4;
                cg->patches[cg->patch_count].target_label_idx = lbl;
                cg->patches[cg->patch_count].patch_size = 4;
                cg->patch_count++;
            }
            break;
        }

        case IR_LABEL: {
            int lbl = cg_find_label(cg, inst->label);
            if (lbl < 0) {
                cg_add_label(cg, inst->label, (int)cg->code_size);
            } else if (cg->labels[lbl].code_offset < 0) {
                cg->labels[lbl].code_offset = (int)cg->code_size;
            }
            /* Do NOT overwrite labels that were pre-set (e.g., "main" by _start).
             * Pre-set labels have code_offset >= 0 and were set before prologue. */
            break;
        }

        case IR_CALL: {
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            cg_save_caller_saved(cg);

            /* Load args into ABI registers (rdi, rsi, rdx, rcx, r8, r9) */
            for (int a = 0; a < inst->arg_count && a < ABI_PARAM_REG_COUNT; a++) {
                int arg_tid = inst->arg_ids[a];
                int arg_reg = cg_load_temp(cg, arg_tid);
                int abi_reg = ABI_PARAM_REGS[a];
                if (arg_reg != abi_reg)
                    emit_mov_rr(cg, abi_reg, arg_reg);
            }

            /* call target */
            emit_call_rel32(cg, 0);
            if (cg->patch_count < MAX_PATCHES) {
                int lbl = cg_find_label(cg, inst->label);
                if (lbl < 0)
                    lbl = cg_add_label(cg, inst->label, 0);
                cg->patches[cg->patch_count].code_offset = (int)cg->code_size - 4;
                cg->patches[cg->patch_count].target_label_idx = lbl;
                cg->patches[cg->patch_count].patch_size = 4;
                cg->patch_count++;
            }

            cg_restore_caller_saved(cg);

            if (dst_reg >= 0 && dst_reg != R_RAX) {
                emit_mov_rr(cg, dst_reg, R_RAX);
            } else if (dst_reg < 0) {
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_CALL_IND: {
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            cg_save_caller_saved(cg);
            cg_emit_call_align(cg);

            for (int a = inst->arg_count - 1; a >= 0; a--) {
                int arg_tid = inst->arg_ids[a];
                int arg_reg = cg_load_temp(cg, arg_tid);
                emit_push_r(cg, arg_reg);
            }

            int fn_reg = cg_load_temp(cg, inst->src_id);
            if (fn_reg != R_RAX)
                emit_mov_rr(cg, R_RAX, fn_reg);
            emit_call_rax(cg);

            cg_emit_call_unalign(cg);
            cg_restore_caller_saved(cg);

            if (dst_reg >= 0 && dst_reg != R_RAX) {
                emit_mov_rr(cg, dst_reg, R_RAX);
            } else if (dst_reg < 0) {
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_RET: {
            if (inst->src_id >= 0) {
                int src_reg = cg_load_temp(cg, inst->src_id);
                if (src_reg != R_RAX)
                    emit_mov_rr(cg, R_RAX, src_reg);
            }
            cg_emit_epilogue(cg);
            break;
        }

        case IR_GLOBAL_STR: {
            if (inst->label) {
                int gidx = cg_find_global(cg, inst->label);
                if (gidx < 0) {
                    if (cg->global_count < MAX_GLOBALS) {
                        size_t len = strlen(inst->label) + 1;
                        cg_grow_rodata(cg, len);
                        memcpy(cg->rodata + cg->rodata_size, inst->label, len);
                        cg->globals[cg->global_count].rodata_offset =
                            (int)cg->rodata_size;
                        snprintf(cg->globals[cg->global_count].name,
                                 sizeof(cg->globals[cg->global_count].name),
                                 "%s", inst->label);
                        cg->rodata_size += len;
                        cg->global_count++;
                    }
                }
            }
            break;
        }

        case IR_SYSCALL: {
            emit_syscall(cg);
            break;
        }

        case IR_ARG:
        case IR_NOP:
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Patch branches                                                     */
/* ------------------------------------------------------------------ */

void cg_patch_branches(CodeGen *cg)
{
    for (int i = 0; i < cg->patch_count; i++) {
        BranchPatch *bp = &cg->patches[i];
        if (bp->target_label_idx < 0) continue;
        int lbl = bp->target_label_idx;
        if (lbl < 0 || lbl >= cg->label_count) continue;
        int target = cg->labels[lbl].code_offset;
        if (target < 0) continue;
        int32_t rel = (int32_t)(target - (bp->code_offset + bp->patch_size));
        uint8_t *p = cg->code + bp->code_offset;
        uint8_t *d = (uint8_t *)&rel;
        p[0] = d[0]; p[1] = d[1]; p[2] = d[2]; p[3] = d[3];
    }
}
