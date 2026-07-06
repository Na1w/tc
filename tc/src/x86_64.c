/*
 * tc/src/x86_64.c  --  x86-64 code generator for tc compiler
 *
 * Implements a complete linear-scan register allocator and
 * ELF64 binary emitter targeting the Linux x86-64 System V ABI.
 */

#include "x86_64.h"
#include "elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Register constants                                                */
/* ------------------------------------------------------------------ */

#define R_RAX   0
#define R_RCX   1
#define R_RDX   2
#define R_RBX   3
#define R_RSP   4
#define R_RBP   5
#define R_RSI   6
#define R_RDI   7
#define R_R8    8
#define R_R9    9
#define R_R10   10
#define R_R11   11
#define R_R12   12
#define R_R13   13
#define R_R14   14
#define R_R15   15

/* Callee-saved registers we might allocate (bit positions in mask) */
#define CS_RBX   0   /* bit 0 */
#define CS_R12   1   /* bit 1 */
#define CS_R13   2   /* bit 2 */
#define CS_R14   3   /* bit 3 */
#define CS_R15   4   /* bit 4 */
#define CS_COUNT 5

/* Argument registers for calls */
static const int ARG_REGS[6] = { R_RDI, R_RSI, R_RDX, R_RCX, R_R8, R_R9 };

/* Caller-saved registers */
static const int CALLER_SAVED[] = {
    R_RAX, R_RCX, R_RDX, R_RSI, R_RDI, R_R8, R_R9, R_R10, R_R11
};
#define CALLER_SAVED_COUNT 9

/* Allocatable registers (rsp and rbp reserved) */
static const int ALLOC_REGS[] = {
    R_RAX, R_RCX, R_RDX, R_RBX, R_RSI, R_RDI,
    R_R8, R_R9, R_R10, R_R11, R_R12, R_R13, R_R14, R_R15
};
#define ALLOC_REG_COUNT 14

/* ------------------------------------------------------------------ */
/*  Data structures                                                    */
/* ------------------------------------------------------------------ */

#define MAX_INTERVALS 4096
#define MAX_LABELS    1024
#define MAX_PATCHES   2048
#define MAX_GLOBALS   512
#define MAX_TEMP      4096

typedef struct {
    int assigned;       /* has a location */
    int is_reg;         /* 1 = register, 0 = stack slot */
    int reg_code;       /* register number if is_reg */
    int stack_off;      /* offset from rbp (negative) if !is_reg */
} TempLoc;

typedef struct {
    int temp_id;
    int start;
    int end;
} LiveInterval;

typedef struct {
    int code_offset;
    int target_label_idx;   /* index into label table */
    int patch_size;         /* 4 (jmp/call) or 4 (jcc rel32) */
} BranchPatch;

typedef struct {
    char name[256];
    int code_offset;
} LabelEntry;

typedef struct {
    char name[256];
    int rodata_offset;
} GlobalEntry;

struct CodeGen {
    /* Dynamic buffers */
    uint8_t *code;
    size_t code_size;
    size_t code_cap;

    uint8_t *rodata;
    size_t rodata_size;
    size_t rodata_cap;

    char output_path[512];

    /* Temp locations */
    TempLoc temps[MAX_TEMP];
    int max_temp;

    /* Live intervals */
    LiveInterval intervals[MAX_INTERVALS];
    int interval_count;

    /* Label table */
    LabelEntry labels[MAX_LABELS];
    int label_count;

    /* Branch patches to resolve after code emission */
    BranchPatch patches[MAX_PATCHES];
    int patch_count;

    /* Global string table */
    GlobalEntry globals[MAX_GLOBALS];
    int global_count;

    /* Frame info */
    int frame_size;       /* bytes for locals/spills (negative from rbp) */
    int callee_saved_mask; /* bits: 0=rbx, 1=r12, 2=r13, 3=r14, 4=r15 */
    int param_count;

    /* Register allocator state */
    int free_regs[ALLOC_REG_COUNT];
    int free_reg_count;
    int reg_owner[16];    /* which interval owns each reg, -1 if free */

    /* Stack alignment tracking */
    int pushed_bytes;     /* bytes pushed since function entry */
};

/* ------------------------------------------------------------------ */
/*  Utility helpers                                                    */
/* ------------------------------------------------------------------ */

static void cg_emit_byte(CodeGen *cg, uint8_t b)
{
    if (cg->code_size < cg->code_cap)
        cg->code[cg->code_size++] = b;
}

/* cg_emit_bytes removed (unused) */

static void cg_emit_int32(CodeGen *cg, int32_t v)
{
    uint8_t *p = (uint8_t *)&v;
    cg_emit_byte(cg, p[0]);
    cg_emit_byte(cg, p[1]);
    cg_emit_byte(cg, p[2]);
    cg_emit_byte(cg, p[3]);
}

static void cg_emit_int64(CodeGen *cg, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        cg_emit_byte(cg, (uint8_t)((v >> ((size_t)i * 8)) & 0xFF));
}

/* cg_grow_code removed (unused, buffers grow via cg_grow_rodata pattern) */

static void cg_grow_rodata(CodeGen *cg, size_t needed)
{
    if (cg->rodata_size + needed <= cg->rodata_cap)
        return;
    size_t new_cap = cg->rodata_cap * 2;
    while (new_cap < cg->rodata_size + needed)
        new_cap *= 2;
    uint8_t *tmp = realloc(cg->rodata, new_cap);
    if (!tmp) {
        fprintf(stderr, "cg_grow_rodata: realloc failed\n");
        exit(1);
    }
    cg->rodata = tmp;
    cg->rodata_cap = new_cap;
}

/* ------------------------------------------------------------------ */
/*  Encoding helpers                                                   */
/* ------------------------------------------------------------------ */

/* Emit REX prefix for 64-bit ops.
   W=1 (64-bit), R/B/X bits based on register operands.
   reg_bit: bit for register field (0 or 1 if >= 8)
   rm_bit:  bit for r/m field (0 or 1 if >= 8)
   Returns the REX byte. */
static uint8_t rex_w(uint8_t reg_bit, uint8_t rm_bit)
{
    return (uint8_t)(0x48 | (reg_bit << 2) | rm_bit);
}

/* mov r64, r64  :  REX 89 /r */
static void emit_mov_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x89);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* mov r64, imm32 (sign-extended) : REX C7 C0|dst imm32 */
static void emit_mov_rimm32(CodeGen *cg, int dst, int64_t val)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0xC7);
    cg_emit_byte(cg, (uint8_t)(0xC0 | (dst & 15)));
    cg_emit_int32(cg, (int32_t)val);
}

/* mov r64, imm64 : REX B8+(reg) imm64 */
static void emit_mov_rimm64(CodeGen *cg, int dst, int64_t val)
{
    uint8_t d = (uint8_t)(dst & 15);
    if (d < 8) {
        cg_emit_byte(cg, 0x48);
        cg_emit_byte(cg, 0xB8 + d);
    } else {
        cg_emit_byte(cg, 0x49);
        cg_emit_byte(cg, 0xB8 + (d - 8));
    }
    cg_emit_int64(cg, (uint64_t)val);
}

/* mov r64, imm (choose 32 or 64 bit encoding) */
static void emit_mov_rimm(CodeGen *cg, int dst, int64_t val)
{
    int32_t v32 = (int32_t)val;
    if ((int64_t)v32 == val) {
        emit_mov_rimm32(cg, dst, val);
    } else {
        emit_mov_rimm64(cg, dst, val);
    }
}

/* mov r64, [rbp+disp32] : REX 8B 85|dst disp32 */
static void emit_mov_rmem_rbp(CodeGen *cg, int dst, int32_t disp)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0x8B);
    cg_emit_byte(cg, (uint8_t)(0x85 | ((dst & 15) << 3)));
    cg_emit_int32(cg, disp);
}

/* mov [rbp+disp32], r64 : REX 89 85|src disp32 */
static void emit_mov_mem_rbp_r(CodeGen *cg, int src, int32_t disp)
{
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(0, sb));
    cg_emit_byte(cg, 0x89);
    cg_emit_byte(cg, (uint8_t)(0x85 | ((src & 15) << 3)));
    cg_emit_int32(cg, disp);
}

/* lea r64, [rbp+disp32] : REX 8D 85|dst disp32 */
static void emit_lea_r_rbp(CodeGen *cg, int dst, int32_t disp)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0x8D);
    cg_emit_byte(cg, (uint8_t)(0x85 | ((dst & 15) << 3)));
    cg_emit_int32(cg, disp);
}

/* lea r64, [rip+disp32] : REX 8D 05|dst disp32 */
static void emit_lea_r_rip(CodeGen *cg, int dst, int32_t disp)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0x8D);
    cg_emit_byte(cg, (uint8_t)(0x05 | ((dst & 15) << 3)));
    cg_emit_int32(cg, disp);
}

/* add r64, r64 : REX 01 /r */
static void emit_add_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x01);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* sub r64, r64 : REX 29 /r */
static void emit_sub_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x29);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* imul r64, r64 : REX 0F AF /r */
static void emit_imul_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, 0xAF);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* and r64, r64 : REX 21 /r */
static void emit_and_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x21);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* or r64, r64 : REX 09 /r */
static void emit_or_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x09);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* xor r64, r64 : REX 31 /r */
static void emit_xor_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x31);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* cmp r64, r64 : REX 39 /r  (cmp r/m64, r64) */
static void emit_cmp_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x39);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* test r64, r64 : REX 85 /r */
static void emit_test_rr(CodeGen *cg, int dst, int src)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, db));
    cg_emit_byte(cg, 0x85);
    cg_emit_byte(cg, (uint8_t)(0xC0 | ((src & 15) << 3) | (dst & 15)));
}

/* neg r64 : REX F7 D0|reg (opcode ext 3) */
static void emit_neg_r(CodeGen *cg, int reg)
{
    uint8_t rb = (uint8_t)((reg >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(0, rb));
    cg_emit_byte(cg, 0xF7);
    cg_emit_byte(cg, (uint8_t)(0xD0 | (reg & 15)));
}

/* not r64 : REX F7 D8|reg (opcode ext 2) */
static void emit_not_r(CodeGen *cg, int reg)
{
    uint8_t rb = (uint8_t)((reg >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(0, rb));
    cg_emit_byte(cg, 0xF7);
    cg_emit_byte(cg, (uint8_t)(0xD8 | (reg & 15)));
}

/* idiv r64 : REX F7 F8|reg (opcode ext 7) */
static void emit_idiv_r(CodeGen *cg, int reg)
{
    uint8_t rb = (uint8_t)((reg >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(0, rb));
    cg_emit_byte(cg, 0xF7);
    cg_emit_byte(cg, (uint8_t)(0xF8 | (reg & 15)));
}

/* shl r64, cl : REX D3 E0|dst (opcode ext 4) */
static void emit_shl_rcl(CodeGen *cg, int dst)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0xD3);
    cg_emit_byte(cg, (uint8_t)(0xE0 | (dst & 15)));
}

/* shr r64, cl : REX D3 E8|dst (opcode ext 5) */
static void emit_shr_rcl(CodeGen *cg, int dst)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0xD3);
    cg_emit_byte(cg, (uint8_t)(0xE8 | (dst & 15)));
}

/* sar r64, cl : REX D3 F0|dst (opcode ext 7) */
static void emit_sar_rcl(CodeGen *cg, int dst)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0xD3);
    cg_emit_byte(cg, (uint8_t)(0xF0 | (dst & 15)));
}

/* cqo (cdq for 64-bit) : 48 99 */
static void emit_cqo(CodeGen *cg)
{
    cg_emit_byte(cg, 0x48);
    cg_emit_byte(cg, 0x99);
}

/* setcc al : 0F 90+cc C0 */
static void emit_setcc(CodeGen *cg, int cc)
{
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, (uint8_t)(0x90 + cc));
    cg_emit_byte(cg, 0xC0);
}

/* movzx rax, al : 48 0F B6 C0 */
static void emit_movzx_rax_al(CodeGen *cg)
{
    cg_emit_byte(cg, 0x48);
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, 0xB6);
    cg_emit_byte(cg, 0xC0);
}

/* movzx r64, al : REX 0F B6 C0|dst */
static void emit_movzx_r_al(CodeGen *cg, int dst)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, 0xB6);
    cg_emit_byte(cg, (uint8_t)(0xC0 | (dst & 15)));
}

/* emit_movsxd_rr removed (unused) */

/* mov r32, [r/m32] : 8B /r (32-bit load) */
static void emit_mov_r32_mem(CodeGen *cg, int dst, int base)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t bb = (uint8_t)((base >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, bb));
    cg_emit_byte(cg, 0x8B);
    cg_emit_byte(cg, (uint8_t)(0x00 | ((dst & 15) << 3) | (base & 7)));
    /* SIB byte if base in [4..11] */
    if (base >= 4 && base <= 11) {
        cg_emit_byte(cg, (uint8_t)(0x24 | ((base & 7) << 3)));
    }
}

/* mov r/m32, r32 : 89 /r (32-bit store) */
static void emit_mov_mem_r32(CodeGen *cg, int src, int base)
{
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    uint8_t bb = (uint8_t)((base >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, bb));
    cg_emit_byte(cg, 0x89);
    cg_emit_byte(cg, (uint8_t)(0x00 | ((src & 15) << 3) | (base & 7)));
    if (base >= 4 && base <= 11) {
        cg_emit_byte(cg, (uint8_t)(0x24 | ((base & 7) << 3)));
    }
}

/* mov r/m8, r8 : 88 /r (8-bit store) */
static void emit_mov_r8_mem(CodeGen *cg, int src, int base)
{
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    uint8_t bb = (uint8_t)((base >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(sb, bb));
    cg_emit_byte(cg, 0x88);
    cg_emit_byte(cg, (uint8_t)(0x00 | ((src & 15) << 3) | (base & 7)));
    if (base >= 4 && base <= 11) {
        cg_emit_byte(cg, (uint8_t)(0x24 | ((base & 7) << 3)));
    }
}

/* movzx r64, byte [r/m] : REX 0F B6 /r */
static void emit_movzx_r64_byte(CodeGen *cg, int dst, int base)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t bb = (uint8_t)((base >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, bb));
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, 0xB6);
    cg_emit_byte(cg, (uint8_t)(0x00 | ((dst & 15) << 3) | (base & 7)));
    if (base >= 4 && base <= 11) {
        cg_emit_byte(cg, (uint8_t)(0x24 | ((base & 7) << 3)));
    }
}

/* movsx r64, byte [r/m] : REX 0F BE /r */
static void emit_movsx_r64_byte(CodeGen *cg, int dst, int base)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    uint8_t bb = (uint8_t)((base >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, bb));
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, 0xBE);
    cg_emit_byte(cg, (uint8_t)(0x00 | ((dst & 15) << 3) | (base & 7)));
    if (base >= 4 && base <= 11) {
        cg_emit_byte(cg, (uint8_t)(0x24 | ((base & 7) << 3)));
    }
}

/* emit_mov_r64_mem removed (unused) */

/* emit_mov_mem_r64 removed (unused) */

/* push r64 */
static void emit_push_r(CodeGen *cg, int reg)
{
    if (reg < 8) {
        cg_emit_byte(cg, (uint8_t)(0x50 + reg));
    } else {
        cg_emit_byte(cg, 0x41);
        cg_emit_byte(cg, (uint8_t)(0x50 + (reg - 8)));
    }
}

/* pop r64 */
static void emit_pop_r(CodeGen *cg, int reg)
{
    if (reg < 8) {
        cg_emit_byte(cg, (uint8_t)(0x58 + reg));
    } else {
        cg_emit_byte(cg, 0x41);
        cg_emit_byte(cg, (uint8_t)(0x58 + (reg - 8)));
    }
}

/* jmp rel32 : E9 rel32 */
static void emit_jmp_rel32(CodeGen *cg, int32_t rel)
{
    cg_emit_byte(cg, 0xE9);
    cg_emit_int32(cg, rel);
}

/* jcc rel32 : 0F 80+cc rel32 */
static void emit_jcc_rel32(CodeGen *cg, int cc, int32_t rel)
{
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, (uint8_t)(0x80 + cc));
    cg_emit_int32(cg, rel);
}

/* call rel32 : E8 rel32 */
static void emit_call_rel32(CodeGen *cg, int32_t rel)
{
    cg_emit_byte(cg, 0xE8);
    cg_emit_int32(cg, rel);
}

/* call rax (FF D0) */
static void emit_call_rax(CodeGen *cg)
{
    cg_emit_byte(cg, 0xFF);
    cg_emit_byte(cg, 0xD0);
}

/* emit_call_r removed (unused, emit_call_rax suffices) */

/* ret : C3 */
static void emit_ret(CodeGen *cg)
{
    cg_emit_byte(cg, 0xC3);
}

/* syscall : 0F 05 */
static void emit_syscall(CodeGen *cg)
{
    cg_emit_byte(cg, 0x0F);
    cg_emit_byte(cg, 0x05);
}

/* sub rsp, imm32 : 48 81 EC imm32 */
static void emit_sub_rsp_imm32(CodeGen *cg, int32_t val)
{
    cg_emit_byte(cg, 0x48);
    cg_emit_byte(cg, 0x81);
    cg_emit_byte(cg, 0xEC);
    cg_emit_int32(cg, val);
}

/* mov [rax], r64 : 89 00 (mod=00, r/m=00) */
static void emit_mov_m_r(CodeGen *cg, int src)
{
    uint8_t sb = (uint8_t)((src >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(0, sb));
    cg_emit_byte(cg, 0x89);
    cg_emit_byte(cg, 0x00);
}

/* mov r64, [rax] : 8B 00 */
static void emit_mov_r_m(CodeGen *cg, int dst)
{
    uint8_t db = (uint8_t)((dst >= 8) ? 1 : 0);
    cg_emit_byte(cg, rex_w(db, 0));
    cg_emit_byte(cg, 0x8B);
    cg_emit_byte(cg, 0x00);
}

/* ------------------------------------------------------------------ */
/*  Label lookup helpers                                               */
/* ------------------------------------------------------------------ */

static int cg_find_label(CodeGen *cg, const char *name)
{
    for (int i = 0; i < cg->label_count; i++) {
        if (strcmp(cg->labels[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int cg_add_label(CodeGen *cg, const char *name, int offset)
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

static int cg_find_global(CodeGen *cg, const char *name)
{
    for (int i = 0; i < cg->global_count; i++) {
        if (strcmp(cg->globals[i].name, name) == 0)
            return i;
    }
    return -1;
}

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

static int temp_is_assigned(CodeGen *cg, int tid)
{
    if (tid < 0 || tid >= MAX_TEMP) return 0;
    return cg->temps[tid].assigned;
}

static int temp_get_reg(CodeGen *cg, int tid)
{
    if (!temp_is_assigned(cg, tid)) return -1;
    if (cg->temps[tid].is_reg) return cg->temps[tid].reg_code;
    return -1;
}

static int temp_get_stack_off(CodeGen *cg, int tid)
{
    if (!temp_is_assigned(cg, tid)) return 0;
    return cg->temps[tid].stack_off;
}

/* ------------------------------------------------------------------ */
/*  Load temp into a register (spills from stack if needed)            */
/* ------------------------------------------------------------------ */

static int cg_load_temp(CodeGen *cg, int tid)
{
    if (!temp_is_assigned(cg, tid)) return R_RAX;
    int reg = temp_get_reg(cg, tid);
    if (reg >= 0) return reg;
    /* Spilled to stack: load into rax */
    emit_mov_rmem_rbp(cg, R_RAX, temp_get_stack_off(cg, tid));
    return R_RAX;
}

/* Store rax to temp's location */
static void cg_store_temp(CodeGen *cg, int tid)
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
static void cg_save_caller_saved(CodeGen *cg)
{
    /* Simple approach: push all caller-saved regs before call */
    for (int i = 0; i < CALLER_SAVED_COUNT; i++) {
        emit_push_r(cg, CALLER_SAVED[i]);
    }
    cg->pushed_bytes += 8 * CALLER_SAVED_COUNT;
}

/* After a call: restore caller-saved regs */
static void cg_restore_caller_saved(CodeGen *cg)
{
    /* Pop in reverse order */
    for (int i = CALLER_SAVED_COUNT - 1; i >= 0; i--) {
        emit_pop_r(cg, CALLER_SAVED[i]);
    }
    cg->pushed_bytes -= 8 * CALLER_SAVED_COUNT;
}

/* ------------------------------------------------------------------ */
/*  Stack alignment before CALL                                        */
/* ------------------------------------------------------------------ */

/* On x86-64 System V, rsp must be 16-byte aligned before CALL.
   At function entry: rsp%16 == 8 because the call instruction placed
   the return address.
   After push rbp: rsp%16 == 0.
   After push N callee-saved: rsp%16 == (8*N)%16 == 0 if N even, 8 if N odd.
   After sub rsp, frame_size (16-aligned): same as above.
   Before an outgoing CALL we push 9 caller-saved registers (72 bytes).
   With N even: (0 + 8)%16 == 8 -> need a dummy push.
   With N odd:  (8 + 8)%16 == 0 -> no dummy needed.
   A dummy is required when the number of callee-saved registers is EVEN,
   i.e. when 1 (rbp) + cs_count is ODD. */
static void cg_emit_call_align(CodeGen *cg)
{
    /* pushed_bytes includes: push rbp (8) + callee-saved pushes + frame_size
       At CALL site, rsp = rbp - frame_size - (pushed_bytes - 8 - callee_saved*8)
       Actually simpler: at function entry rsp%16==0.
       After push rbp: rsp%16==8.
       After push N callee-saved: rsp%16 == (8 + 8*N) % 16.
       After sub rsp, frame_size: rsp%16 == (8 + 8*N + frame_size) % 16.
       We need rsp%16==16 (==0) before call.
       So if (8 + 8*N + frame_size) % 16 != 0, push/pop dummy.
       But frame_size is 16-aligned, so we need (8 + 8*N) % 16 == 0,
       meaning N must be odd.
       Count callee-saved regs used: */
    int cs_count = 0;
    for (int b = 0; b < CS_COUNT; b++) {
        if (cg->callee_saved_mask & (1 << b))
            cs_count++;
    }
    /* Total pushes before sub rsp: 1 (rbp) + cs_count */
    int total_pushes = 1 + cs_count;
    if ((total_pushes & 1) == 1) {
        /* Odd number of pushes => rsp is 8 mod 16 before caller-saved pushes */
        emit_push_r(cg, R_RAX);
        cg->pushed_bytes += 8;
    }
}

static void cg_emit_call_unalign(CodeGen *cg)
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

static void cg_emit_prologue(CodeGen *cg)
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
}

/* ------------------------------------------------------------------ */
/*  Emit epilogue                                                      */
/* ------------------------------------------------------------------ */

static void cg_emit_epilogue(CodeGen *cg)
{
    /* mov rsp, rbp */
    emit_mov_rr(cg, R_RSP, R_RBP);

    /* Pop callee-saved regs in reverse order */
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
    /* _start:
       call main
       mov rdi, rax
       mov rax, 60
       syscall
    */
    /* Record _start label */
    cg_add_label(cg, "_start", (int)cg->code_size);

    /* call main (patch later) */
    cg_emit_byte(cg, 0xE8);
    int call_offset = (int)cg->code_size;
    cg_emit_int32(cg, 0); /* placeholder */
    /* Add patch -- look up "main" label; create placeholder if not found */
    if (cg->patch_count < MAX_PATCHES) {
        int lbl = cg_find_label(cg, "main");
        if (lbl < 0) {
            lbl = cg_add_label(cg, "main", -1);
        }
        cg->patches[cg->patch_count].code_offset = call_offset;
        cg->patches[cg->patch_count].target_label_idx = lbl;
        cg->patches[cg->patch_count].patch_size = 4;
        cg->patch_count++;
    }

    /* mov rdi, rax */
    emit_mov_rr(cg, R_RDI, R_RAX);
    /* mov rax, 60 */
    emit_mov_rimm(cg, R_RAX, 60);
    /* syscall */
    emit_syscall(cg);
}

/* ------------------------------------------------------------------ */
/*  Emit instruction                                                   */
/* ------------------------------------------------------------------ */

static void cg_emit_instr(CodeGen *cg, IRInstr *inst)
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
                /* dst is on stack */
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
            /* lea dst, [rip+disp] - disp to rodata offset */
            int gidx = cg_find_global(cg, inst->label);
            int rodata_off = 0;
            if (gidx >= 0)
                rodata_off = cg->globals[gidx].rodata_offset;
            /* disp = rodata_vaddr + rodata_off - (code_vaddr + current_ip + 7) */
            /* We'll patch this later */
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;
            /* Emit lea with placeholder disp */
            int lea_start = (int)cg->code_size;
            emit_lea_r_rip(cg, dst_reg, 0); /* placeholder */
            /* Record patch: disp = 0x401000 + rodata_off - (0x400000 + lea_start + 7) */
            if (cg->patch_count < MAX_PATCHES) {
                int32_t disp = (int32_t)(0x401000 + rodata_off - (0x400000 + lea_start + 7));
                /* Patch at lea_start + 3 (after REX 8D ModR/M) */
                cg->patches[cg->patch_count].code_offset = lea_start + 3;
                cg->patches[cg->patch_count].target_label_idx = -1; /* special: use patch_disp */
                cg->patches[cg->patch_count].patch_size = 4;
                /* Store disp in a special way - use code_offset to encode */
                /* Actually, let's just patch it directly */
                uint8_t *p = cg->code + lea_start + 3;
                uint8_t *d = (uint8_t *)&disp;
                p[0] = d[0]; p[1] = d[1]; p[2] = d[2]; p[3] = d[3];
                cg->patch_count++; /* dummy entry to not break counting */
            }
            break;
        }

        case IR_ADDR_LOCAL: {
            if (inst->temp_id < 0) break;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;
            emit_lea_r_rbp(cg, dst_reg, (int32_t)inst->ival);
            break;
        }

        case IR_LOAD: {
            if (inst->temp_id < 0) break;
            /* Load address into rax */
            int addr_reg = cg_load_temp(cg, inst->src_id);
            if (addr_reg != R_RAX)
                emit_mov_rr(cg, R_RAX, addr_reg);
            /* Load from [rax] */
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;

            if (inst->type_size == 8) {
                emit_mov_r_m(cg, dst_reg);
            } else if (inst->type_size == 4) {
                if (inst->is_signed) {
                    /* movsxd dst, [rax] : REX 63 00|dst */
                    uint8_t db = (uint8_t)((dst_reg >= 8) ? 1 : 0);
                    cg_emit_byte(cg, rex_w(db, 0));
                    cg_emit_byte(cg, 0x63);
                    cg_emit_byte(cg, (uint8_t)(0x00 | ((dst_reg & 15) << 3)));
                } else {
                    /* mov eax, [rax] then zero-extend */
                    emit_mov_r32_mem(cg, dst_reg, R_RAX);
                }
            } else {
                /* 1-byte */
                if (inst->is_signed) {
                    emit_movsx_r64_byte(cg, dst_reg, R_RAX);
                } else {
                    emit_movzx_r64_byte(cg, dst_reg, R_RAX);
                }
            }
            /* If dst was not the temp's register, move to temp location */
            if (dst_reg != R_RAX && !temp_get_reg(cg, inst->temp_id)) {
                /* Already handled above */
            }
            break;
        }

        case IR_STORE: {
            /* Store src2 to address in src_id */
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
            /* param at [rbp + 16 + ival*8] */
            int disp = 16 + (int)inst->ival * 8;
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            if (dst_reg < 0) dst_reg = R_RAX;
            emit_lea_r_rbp(cg, dst_reg, (int32_t)disp);
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
            /* dividend -> rax */
            if (src_reg != R_RAX) emit_mov_rr(cg, R_RAX, src_reg);
            emit_cqo(cg);
            /* divisor -> non-rax/rdx reg */
            if (src2_reg == R_RAX || src2_reg == R_RDX) {
                emit_mov_rr(cg, R_RCX, src2_reg);
                src2_reg = R_RCX;
            }
            emit_idiv_r(cg, src2_reg);
            /* result in rax */
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
            /* remainder in rdx */
            if (dst_reg >= 0 && dst_reg != R_RDX) {
                emit_mov_rr(cg, dst_reg, R_RDX);
            } else if (dst_reg < 0) {
                /* Store rdx to stack */
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
            /* count must be in cl */
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
            /* Logical not: test src, src; setne al; movzx rax, al */
            if (inst->temp_id < 0) break;
            int src_reg = cg_load_temp(cg, inst->src_id);
            int dst_reg = temp_get_reg(cg, inst->temp_id);
            emit_test_rr(cg, src_reg, src_reg);
            emit_setcc(cg, 5); /* setne al */
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

            /* cmp src2, src  => we want src OP src2 */
            /* cmp r/m, r: cmp src2_reg, src_reg means src_reg - src2_reg */
            if (src_reg != src2_reg) {
                emit_cmp_rr(cg, src2_reg, src_reg);
            } else {
                emit_xor_rr(cg, src_reg, src_reg); /* zero flags for == check */
            }

            /* setcc al */
            int cc = 0;
            switch (inst->kind) {
                case IR_CMP_EQ: cc = 0x4; break; /* sete */
                case IR_CMP_NEQ: cc = 0x5; break; /* setne */
                case IR_CMP_LT:
                    cc = inst->is_signed ? 0xC : 0x3; break; /* setl / setb */
                case IR_CMP_GT:
                    cc = inst->is_signed ? 0xF : 0x7; break; /* setg / seta */
                case IR_CMP_LEQ:
                    cc = inst->is_signed ? 0xE : 0x2; break; /* setle / setbe */
                case IR_CMP_GEQ:
                    cc = inst->is_signed ? 0xD : 0x6; break; /* setge / setae */
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
            /* jmp label */
            emit_jmp_rel32(cg, 0); /* placeholder */
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
            /* if src != 0 goto label */
            int src_reg = cg_load_temp(cg, inst->src_id);
            emit_test_rr(cg, src_reg, src_reg);
            emit_jcc_rel32(cg, 0x85, 0); /* jne placeholder */
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
            /* if src == 0 goto label */
            int src_reg = cg_load_temp(cg, inst->src_id);
            emit_test_rr(cg, src_reg, src_reg);
            emit_jcc_rel32(cg, 0x84, 0); /* je placeholder */
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
            /* Record label at current code offset */
            int lbl = cg_find_label(cg, inst->label);
            if (lbl >= 0) {
                /* Only update if not already set (e.g., _start pre-registered "main") */
                if (cg->labels[lbl].code_offset < 0) {
                    cg->labels[lbl].code_offset = (int)cg->code_size;
                }
            } else {
                cg_add_label(cg, inst->label, (int)cg->code_size);
            }
            break;
        }

        /* Functions */
        case IR_CALL: {
            /* Call function named in label */
            /* Save caller-saved registers */
            cg_save_caller_saved(cg);
            /* Align stack */
            cg_emit_call_align(cg);

            /* Place arguments */
            for (int a = 0; a < inst->arg_count && a < 6; a++) {
                int arg_reg = cg_load_temp(cg, inst->arg_ids[a]);
                if (arg_reg != ARG_REGS[a])
                    emit_mov_rr(cg, ARG_REGS[a], arg_reg);
            }

            /* call label */
            emit_call_rel32(cg, 0); /* placeholder */
            if (cg->patch_count < MAX_PATCHES) {
                int lbl = cg_find_label(cg, inst->label);
                if (lbl < 0)
                    lbl = cg_add_label(cg, inst->label, -1);
                cg->patches[cg->patch_count].code_offset = (int)cg->code_size - 4;
                cg->patches[cg->patch_count].target_label_idx = lbl;
                cg->patches[cg->patch_count].patch_size = 4;
                cg->patch_count++;
            }

            /* Unalign stack */
            cg_emit_call_unalign(cg);
            /* Restore caller-saved */
            cg_restore_caller_saved(cg);

            /* Result in rax -> temp */
            if (inst->temp_id >= 0) {
                int dst_reg = temp_get_reg(cg, inst->temp_id);
                if (dst_reg >= 0 && dst_reg != R_RAX) {
                    emit_mov_rr(cg, dst_reg, R_RAX);
                } else if (dst_reg < 0) {
                    cg_store_temp(cg, inst->temp_id);
                }
            }
            break;
        }

        case IR_CALL_IND: {
            /* Call function pointer in src_id */
            cg_save_caller_saved(cg);
            cg_emit_call_align(cg);

            /* Place arguments */
            for (int a = 0; a < inst->arg_count && a < 6; a++) {
                int arg_reg = cg_load_temp(cg, inst->arg_ids[a]);
                if (arg_reg != ARG_REGS[a])
                    emit_mov_rr(cg, ARG_REGS[a], arg_reg);
            }

            /* Move function pointer to rax, call rax */
            int fn_reg = cg_load_temp(cg, inst->src_id);
            if (fn_reg != R_RAX)
                emit_mov_rr(cg, R_RAX, fn_reg);
            emit_call_rax(cg);

            cg_emit_call_unalign(cg);
            cg_restore_caller_saved(cg);

            if (inst->temp_id >= 0) {
                int dst_reg = temp_get_reg(cg, inst->temp_id);
                if (dst_reg >= 0 && dst_reg != R_RAX) {
                    emit_mov_rr(cg, dst_reg, R_RAX);
                } else if (dst_reg < 0) {
                    cg_store_temp(cg, inst->temp_id);
                }
            }
            break;
        }

        case IR_PARAM: {
            /* Record parameter count */
            int param_n = (int)inst->ival;
            if (param_n + 1 > cg->param_count)
                cg->param_count = param_n + 1;
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

        /* Memory */
        case IR_ALLOC: {
            /* Increase frame size */
            int needed = (int)inst->ival;
            if (needed > cg->frame_size)
                cg->frame_size = needed;
            cg->frame_size = (cg->frame_size + 15) & ~15;
            break;
        }

        case IR_GLOBAL_STR: {
            /* Emit string to rodata */
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
            /* No code emitted */
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Patch branches                                                     */
/* ------------------------------------------------------------------ */

static void cg_patch_branches(CodeGen *cg)
{
    for (int i = 0; i < cg->patch_count; i++) {
        BranchPatch *bp = &cg->patches[i];
        if (bp->target_label_idx < 0) continue; /* skip special patches */
        int lbl = bp->target_label_idx;
        if (lbl < 0 || lbl >= cg->label_count) continue;
        int target = cg->labels[lbl].code_offset;
        if (target < 0) continue; /* forward ref not resolved yet */
        int32_t rel = (int32_t)(target - (bp->code_offset + bp->patch_size));
        uint8_t *p = cg->code + bp->code_offset;
        uint8_t *d = (uint8_t *)&rel;
        p[0] = d[0]; p[1] = d[1]; p[2] = d[2]; p[3] = d[3];
    }
}

/* ------------------------------------------------------------------ */
/*  Code generation entry point                                        */
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
    memset(cg->code, 0, cg->code_cap);
    memset(cg->rodata, 0, cg->rodata_cap);
    return cg;
}

void cg_destroy(CodeGen *cg)
{
    if (!cg) return;
    free(cg->code);
    free(cg->rodata);
    free(cg);
}

int cg_generate(CodeGen *cg, IRProgram *prog, const char *output_path)
{
    if (!cg || !prog) return -1;

    /* Store output path */
    snprintf(cg->output_path, sizeof(cg->output_path), "%s", output_path);

    /* Reset state */
    cg->code_size = 0;
    cg->rodata_size = 0;
    cg->label_count = 0;
    cg->patch_count = 0;
    cg->global_count = 0;
    cg->param_count = 0;
    cg->frame_size = 0;
    cg->callee_saved_mask = 0;
    cg->pushed_bytes = 0;
    temp_init(cg);

    /* Pass 1: Compute live intervals and allocate registers */
    cg_compute_intervals(cg, prog);
    cg_linear_scan(cg);

    /* Pass 2: Emit code */
    /* First emit _start */
    cg_emit_start(cg);

    /* Resolve the "main" label placeholder before emitting prologue */
    for (int i = 0; i < cg->label_count; i++) {
        if (strcmp(cg->labels[i].name, "main") == 0 && cg->labels[i].code_offset < 0) {
            cg->labels[i].code_offset = (int)cg->code_size;
            break;
        }
    }

    /* Emit prologue */
    cg_emit_prologue(cg);

    /* Emit all instructions */
    for (int i = 0; i < prog->count; i++) {
        IRInstr *inst = &prog->instrs[i];
        cg_emit_instr(cg, inst);
    }

    /* If no RET was emitted, add default return */
    /* Check if last instruction was a RET */
    int has_ret = 0;
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == IR_RET) {
            has_ret = 1;
            break;
        }
    }
    if (!has_ret) {
        emit_mov_rimm(cg, R_RAX, 0);
        cg_emit_epilogue(cg);
    }

    /* Patch branches */
    /* Ensure "main" label is resolved (may still be -1 if no IR_LABEL was emitted) */
    for (int i = 0; i < cg->label_count; i++) {
        if (strcmp(cg->labels[i].name, "main") == 0 && cg->labels[i].code_offset < 0) {
            cg->labels[i].code_offset = (int)cg->code_size;
        }
    }
    cg_patch_branches(cg);
    {
        ElfWriter *elf = elf_create();
        if (elf) {
            elf_add_text(elf, cg->code, cg->code_size);
            elf_add_rodata(elf, cg->rodata, cg->rodata_size);
            elf_define_symbol(elf, "_start", 0x400000);
            elf_write(elf, cg->output_path);
            elf_destroy(elf);
        }
    }

    return 0;
}

/* Backend wrappers below use global g_cg pointer */

static CodeGen *g_cg = NULL;
static char g_output[512] = "a.out";

static void be_start_program(IRProgram *prog)
{
    if (!g_cg) return;
    /* Reset state */
    g_cg->code_size = 0;
    g_cg->rodata_size = 0;
    g_cg->label_count = 0;
    g_cg->patch_count = 0;
    g_cg->global_count = 0;
    g_cg->param_count = 0;
    g_cg->frame_size = 0;
    g_cg->callee_saved_mask = 0;
    g_cg->pushed_bytes = 0;
    temp_init(g_cg);

    /* Pass 1: Compute live intervals and allocate registers */
    cg_compute_intervals(g_cg, prog);
    cg_linear_scan(g_cg);

    /* Pass 2: Emit code */
    cg_emit_start(g_cg);

    /* Update the "main" label placeholder created by cg_emit_start */
    for (int i = 0; i < g_cg->label_count; i++) {
        if (strcmp(g_cg->labels[i].name, "main") == 0 && g_cg->labels[i].code_offset < 0) {
            g_cg->labels[i].code_offset = (int)g_cg->code_size;
            break;
        }
    }

    /* Emit prologue */
    cg_emit_prologue(g_cg);

    /* Emit all instructions */
    for (int i = 0; i < prog->count; i++) {
        IRInstr *inst = &prog->instrs[i];
        cg_emit_instr(g_cg, inst);
    }

    /* If no RET was emitted, add default return */
    int has_ret = 0;
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == IR_RET) {
            has_ret = 1;
            break;
        }
    }
    if (!has_ret) {
        emit_mov_rimm(g_cg, R_RAX, 0);
        cg_emit_epilogue(g_cg);
    }

    /* Patch branches */
    /* Ensure "main" label is resolved (may still be -1 if no IR_LABEL was emitted) */
    for (int i = 0; i < g_cg->label_count; i++) {
        if (strcmp(g_cg->labels[i].name, "main") == 0 && g_cg->labels[i].code_offset < 0) {
            g_cg->labels[i].code_offset = (int)g_cg->code_size;
        }
    }
    cg_patch_branches(g_cg);
}

static void be_write_output(const char *filename)
{
    if (!g_cg) return;
    ElfWriter *elf = elf_create();
    if (!elf) return;
    elf_add_text(elf, g_cg->code, g_cg->code_size);
    elf_add_rodata(elf, g_cg->rodata, g_cg->rodata_size);
    elf_define_symbol(elf, "_start", 0x400000);
    elf_write(elf, filename);
    elf_destroy(elf);
}

void x86_64_init(Backend *be, const char *output)
{
    if (!be) return;

    g_cg = cg_create();
    if (!g_cg) return;

    snprintf(g_output, sizeof(g_output), "%s", output ? output : "a.out");
    be->context = g_cg;

    be->start_program = be_start_program;
    be->write_output = be_write_output;
    be->end_program = NULL;
    be->start_function = NULL;
    be->end_function = NULL;
    be->emit_prologue = NULL;
    be->emit_instruction = NULL;
    be->emit_epilogue = NULL;
}
