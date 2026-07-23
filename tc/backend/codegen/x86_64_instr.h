#ifndef TC_X86_64_INSTR_H
#define TC_X86_64_INSTR_H

#include "x86_64.h"

/* Register constants */
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

/* Callee-saved bit positions */
#define CS_RBX   0
#define CS_R12   1
#define CS_R13   2
#define CS_R14   3
#define CS_R15   4
#define CS_COUNT 5

/* Label / global lookup */
int cg_find_label(CodeGen *cg, const char *name);
int cg_add_label(CodeGen *cg, const char *name, int offset);
int cg_find_global(CodeGen *cg, const char *name);

/* Instruction dispatch & branch patching */
void cg_emit_instr(CodeGen *cg, IRInstr *inst);
void cg_patch_branches(CodeGen *cg);

#endif
