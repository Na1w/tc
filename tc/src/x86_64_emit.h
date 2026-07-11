#ifndef TC_X86_64_EMIT_H
#define TC_X86_64_EMIT_H

#include "x86_64.h"

void cg_emit_byte(CodeGen *cg, uint8_t b);
void cg_emit_int32(CodeGen *cg, int32_t v);
void cg_emit_int64(CodeGen *cg, uint64_t v);
void cg_grow_rodata(CodeGen *cg, size_t needed);

uint8_t rex_w(uint8_t reg_bit, uint8_t rm_bit);

void emit_mov_rr(CodeGen *cg, int dst, int src);
void emit_mov_rimm32(CodeGen *cg, int dst, int64_t val);
void emit_mov_rimm64(CodeGen *cg, int dst, int64_t val);
void emit_mov_rimm(CodeGen *cg, int dst, int64_t val);
void emit_mov_rmem_rbp(CodeGen *cg, int dst, int32_t disp);
void emit_mov_mem_rbp_r(CodeGen *cg, int src, int32_t disp);
void emit_lea_r_rbp(CodeGen *cg, int dst, int32_t disp);
void emit_lea_r_rip(CodeGen *cg, int dst, int32_t disp);
void emit_add_rr(CodeGen *cg, int dst, int src);
void emit_sub_rr(CodeGen *cg, int dst, int src);
void emit_imul_rr(CodeGen *cg, int dst, int src);
void emit_and_rr(CodeGen *cg, int dst, int src);
void emit_or_rr(CodeGen *cg, int dst, int src);
void emit_xor_rr(CodeGen *cg, int dst, int src);
void emit_cmp_rr(CodeGen *cg, int dst, int src);
void emit_test_rr(CodeGen *cg, int dst, int src);
void emit_neg_r(CodeGen *cg, int reg);
void emit_not_r(CodeGen *cg, int reg);
void emit_idiv_r(CodeGen *cg, int reg);
void emit_shl_rcl(CodeGen *cg, int dst);
void emit_shr_rcl(CodeGen *cg, int dst);
void emit_sar_rcl(CodeGen *cg, int dst);
void emit_cqo(CodeGen *cg);
void emit_setcc(CodeGen *cg, int cc);
void emit_movzx_rax_al(CodeGen *cg);
void emit_movzx_r_al(CodeGen *cg, int dst);
void emit_mov_r32_mem(CodeGen *cg, int dst, int base);
void emit_mov_mem_r32(CodeGen *cg, int src, int base);
void emit_mov_r8_mem(CodeGen *cg, int src, int base);
void emit_movzx_r64_byte(CodeGen *cg, int dst, int base);
void emit_movsx_r64_byte(CodeGen *cg, int dst, int base);
void emit_push_r(CodeGen *cg, int reg);
void emit_pop_r(CodeGen *cg, int reg);
void emit_jmp_rel32(CodeGen *cg, int32_t rel);
void emit_jcc_rel32(CodeGen *cg, int cc, int32_t rel);
void emit_call_rel32(CodeGen *cg, int32_t rel);
void emit_call_rax(CodeGen *cg);
void emit_ret(CodeGen *cg);
void emit_syscall(CodeGen *cg);
void emit_sub_rsp_imm32(CodeGen *cg, int32_t val);
void emit_mov_m_r(CodeGen *cg, int src);
void emit_mov_r_m(CodeGen *cg, int dst);

#endif
