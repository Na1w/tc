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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
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
            int64_t global_addr = 0;
            if (gidx >= 0) {
                /* Check data_offset first (global variables), then rodata_offset (string literals) */
                if (cg->globals[gidx].data_offset >= 0) {
                    global_addr = ELF_DATA_BASE + (uint64_t)cg->globals[gidx].data_offset;
                } else {
                    global_addr = ELF_RODATA_BASE + (uint64_t)cg->globals[gidx].rodata_offset;
                }
            }
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;
            int lea_start = (int)cg->code_size;
            emit_lea_r_rip(cg, dst_reg, 0);
            if (cg->patch_count < MAX_PATCHES) {
                int32_t disp = (int32_t)(global_addr - (ELF_TEXT_BASE + (uint64_t)lea_start + 7));
                uint8_t *p = cg->code + lea_start + 3;
                uint8_t *d = (uint8_t *)&disp;
                p[0] = d[0]; p[1] = d[1]; p[2] = d[2]; p[3] = d[3];
            }
            if (temp_get_reg(cg, inst->temp_id) < 0) {
                emit_mov_mem_rbp_r(cg, dst_reg, temp_get_stack_off(cg, inst->temp_id));
            }
            break;
        }

        case IR_ADDR_LOCAL: {
            if (inst->temp_id < 0) break;
            /* Skip if this temp was pre-initialized after the prologue */
            if (cg->temps[inst->temp_id].pre_init) break;
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
            int addr_reg = cg_load_temp(cg, inst->src_id, -1);
            if (addr_reg != R_RAX)
                emit_mov_rr(cg, R_RAX, addr_reg);
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;

            int ts = inst->type_size;
            if (ts == 0) ts = 4;  /* default to dword if type_size not set */

            if (ts == 8) {
                emit_mov_r_m(cg, dst_reg);
            } else if (ts == 4) {
                if (inst->is_signed) {
                    /* movsxd r64, r/m32  -- requires REX.W=1 */
                    uint8_t db = (uint8_t)((dst_reg >= 8) ? 1 : 0);
                    cg_emit_byte(cg, (uint8_t)(0x48 | (db << 2)));
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
            int addr_reg = cg_load_temp(cg, inst->src_id, -1);
            if (addr_reg != R_RAX)
                emit_mov_rr(cg, R_RAX, addr_reg);
            int val_reg = cg_load_temp(cg, inst->src2_id, -1);

            int ts = inst->type_size;
            if (ts == 0) ts = 4;  /* default to dword if type_size not set */

            if (ts == 8) {
                emit_mov_m_r(cg, val_reg);
            } else if (ts == 4) {
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
            if (dst_reg >= 0) {
                /* If dst_reg == src2_reg, we must ADD before MOV to avoid
                 * overwriting src2's value.  Otherwise MOV first. */
                if (dst_reg == src2_reg) {
                    /* src2 is already in dst_reg; add src_reg */
                    if (dst_reg != src_reg)
                        emit_add_rr(cg, dst_reg, src_reg);
                } else {
                    if (dst_reg != src_reg)
                        emit_mov_rr(cg, dst_reg, src_reg);
                    emit_add_rr(cg, dst_reg, src2_reg);
                }
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
            if (dst_reg >= 0) {
                /* Handle overlap: if dst == src2, MUL first then MOV src */
                if (dst_reg == src2_reg) {
                    emit_imul_rr(cg, dst_reg, src_reg);
                } else {
                    if (dst_reg != src_reg)
                        emit_mov_rr(cg, dst_reg, src_reg);
                    emit_imul_rr(cg, dst_reg, src2_reg);
                }
            } else {
                if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
                emit_imul_rr(cg, R_RAX, src2_reg);
                cg_store_temp(cg, inst->temp_id);
            }
            break;
        }

        case IR_DIV: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);

            // --- Load divisor (src2) FIRST into a safe register (not RAX/RDX) ---
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
            if (src2_reg == R_RAX || src2_reg == R_RDX) {
                emit_mov_rr(cg, R_RCX, src2_reg);
                src2_reg = R_RCX;
            }
            // Now src2 is in a register that won't be clobbered by loading src into RAX.

            // --- Load dividend (src) into RAX for CQO/IDIV ---
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            if (src_reg != R_RAX) {
                emit_mov_rr(cg, R_RAX, src_reg);
            }

            emit_cqo(cg);
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

            // --- Load divisor (src2) FIRST into a safe register (not RAX/RDX) ---
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
            if (src2_reg == R_RAX || src2_reg == R_RDX) {
                emit_mov_rr(cg, R_RCX, src2_reg);
                src2_reg = R_RCX;
            }
            // Now src2 is in a register that won't be clobbered by loading src into RAX.

            // --- Load dividend (src) into RAX for CQO/IDIV ---
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            if (src_reg != R_RAX) {
                emit_mov_rr(cg, R_RAX, src_reg);
            }

            emit_cqo(cg);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
            int src2_reg = cg_load_temp(cg, inst->src2_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
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
            int src_reg = cg_load_temp(cg, inst->src_id, -1);
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
                int arg_reg = cg_load_temp(cg, arg_tid, -1);
                int abi_reg = ABI_PARAM_REGS[a];
                if (arg_reg != abi_reg)
                    emit_mov_rr(cg, abi_reg, arg_reg);
            }

            /* call target */
            emit_call_rel32(cg, 0);
            if (cg->patch_count < MAX_PATCHES) {
                int lbl = cg_find_label(cg, inst->label);
                if (lbl < 0) {
                    /* New label - mark as unresolved for potential relocation */
                    lbl = cg_add_label(cg, inst->label, -1);
                }
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
                int arg_reg = cg_load_temp(cg, arg_tid, -1);
                emit_push_r(cg, arg_reg);
            }

            int fn_reg = cg_load_temp(cg, inst->src_id, -1);
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
                int src_reg = cg_load_temp(cg, inst->src_id, -1);
                if (src_reg >= 0) {
                    // Value is in a register; move to RAX if not already there
                    if (src_reg != R_RAX)
                        emit_mov_rr(cg, R_RAX, src_reg);
                } else {
                    // cg_load_temp returned -1 (temp unassigned).
                    // Safety net: if inst->ival holds a constant value, use it directly
                    if (inst->ival != 0) {
                        emit_mov_rimm(cg, R_RAX, inst->ival);
                    }
                    // If ival is also 0, RAX will be zeroed below
                }
            } else {
                // No return value (void function) - zero RAX as safety
                emit_mov_rimm(cg, R_RAX, 0);
            }
            cg_emit_epilogue(cg);
            break;
        }

        case IR_GLOBAL_STR: {
            if (inst->label) {
                int gidx = cg_find_global(cg, inst->label);
                if (gidx < 0) {
                    if (cg->global_count < MAX_GLOBALS) {
                        /* String content is stored in ival (cast from char*) */
                        const char *str_content = (const char *)(intptr_t)inst->ival;
                        if (!str_content) str_content = "";
                        size_t len = strlen(str_content) + 1;
                        cg_grow_rodata(cg, len);
                        memcpy(cg->rodata + cg->rodata_size, str_content, len);
                        cg->globals[cg->global_count].rodata_offset =
                            (int)cg->rodata_size;
                        cg->globals[cg->global_count].data_offset = -1;
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

        case IR_GLOBAL_DATA: {
            /* Emit a global variable into .data section */
            if (inst->label) {
                int gidx = cg_find_global(cg, inst->label);
                if (gidx < 0) {
                    if (cg->global_count < MAX_GLOBALS) {
                        uint64_t init_val = 0;
                        int has_init = 0;
                        int64_t raw_ival = inst->ival;

                        /* Check if ival is a pointer to a string content (cast from char*) */
                        char *str_label = (char *)(intptr_t)raw_ival;
                        int is_str_ptr = 0;
                        if (str_label && strlen(str_label) > 0) {
                            /* Try to resolve as a string global (registered by IR_GLOBAL_STR) */
                            int sgidx = cg_find_global(cg, str_label);
                            if (sgidx >= 0) {
                                is_str_ptr = 1;
                            }
                        }

                        if (is_str_ptr) {
                            /* Resolve the string label to its rodata offset */
                            int sgidx = cg_find_global(cg, str_label);
                            if (sgidx >= 0) {
                                init_val = ELF_RODATA_BASE + (uint64_t)cg->globals[sgidx].rodata_offset;
                                has_init = 1;
                            }
                        } else if (raw_ival != 0) {
                            /* Integer initializer */
                            init_val = (uint64_t)raw_ival;
                            has_init = 1;
                        }
                        // else: zero-initialized (BSS)

                        if (has_init) {
                            /* Store as 8-byte value in .data section */
                            cg_grow_data(cg, 8);
                            memcpy(cg->data + cg->data_size, &init_val, 8);
                            cg->globals[cg->global_count].data_offset =
                                (int)cg->data_size;
                            cg->globals[cg->global_count].rodata_offset = -1;
                            snprintf(cg->globals[cg->global_count].name,
                                     sizeof(cg->globals[cg->global_count].name),
                                     "%s", inst->label);
                            cg->data_size += 8;
                            cg->global_count++;
                        } else {
                            /* Zero-initialized (BSS) - still need to register the symbol */
                            cg->globals[cg->global_count].data_offset = -1;
                            cg->globals[cg->global_count].rodata_offset = -1;
                            snprintf(cg->globals[cg->global_count].name,
                                     sizeof(cg->globals[cg->global_count].name),
                                     "%s", inst->label);
                            cg->global_count++;
                        }
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
    fprintf(stderr, "[PATCH] patch_count=%d, label_count=%d\n", cg->patch_count, cg->label_count);
    for (int i = 0; i < cg->patch_count; i++) {
        BranchPatch *bp = &cg->patches[i];
        if (bp->target_label_idx < 0) continue;
        int lbl = bp->target_label_idx;
        if (lbl < 0 || lbl >= cg->label_count) continue;
        int target = cg->labels[lbl].code_offset;
        fprintf(stderr, "[PATCH] patch[%d]: offset=%d label='%s' target=%d\n",
                i, bp->code_offset, cg->labels[lbl].name, target);
        if (target < 0) continue;
        /* code_offset points to the start of the 4-byte displacement field.
         * x86 relative displacement is from the NEXT instruction, which is
         * exactly code_offset + 4 (right after the displacement field).
         * This formula works for ALL instruction types (E9, E8, 0F 8x). */
        int32_t rel = (int32_t)(target - (bp->code_offset + 4));
        fprintf(stderr, "[PATCH]   rel=%d (target=%d - (code_offset=%d + 4))\n",
                rel, target, bp->code_offset);
        uint8_t *p = cg->code + bp->code_offset;
        uint8_t *d = (uint8_t *)&rel;
        p[0] = d[0]; p[1] = d[1]; p[2] = d[2]; p[3] = d[3];
    }
}
