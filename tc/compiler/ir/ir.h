#ifndef TC_IR_H
#define TC_IR_H

#include "ast.h"
#include "sema.h"
#include <stdint.h>

/* ==================================================================
 * IR Instruction Kinds  (Three-Address Code)
 * ================================================================== */

typedef enum {
    /* Data movement */
    IR_MOV,             /* t_dst = t_src1                     */
    IR_CONST,           /* t_dst = immediate (ival)           */
    IR_ADDR_GLOBAL,     /* t_dst = address of global "label"  */
    IR_ADDR_LOCAL,      /* t_dst = address of local (ival=off)*/
    IR_LOAD,            /* t_dst = *(type)t_src1              */
    IR_STORE,           /* *(type)t_src1 = t_src2             */
    IR_ADDR_PARAM,      /* t_dst = address of parameter N     */

    /* Arithmetic (binary: dst = src1 OP src2) */
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_MOD,
    IR_BAND,
    IR_BOR,
    IR_XOR,
    IR_SHL,
    IR_SHR,

    /* Unary (dst = OP src1) */
    IR_NEG,             /* dst = -src1                        */
    IR_NOT,             /* dst = !src1  (logical negation)    */
    IR_BITNOT,          /* dst = ~src1                        */

    /* Comparisons (result 0 or 1) */
    IR_CMP_EQ,
    IR_CMP_NEQ,
    IR_CMP_LT,
    IR_CMP_GT,
    IR_CMP_LEQ,
    IR_CMP_GEQ,

    /* Control flow */
    IR_BR,              /* goto label                         */
    IR_BR_IF,           /* if (t_src1 != 0) goto label        */
    IR_BR_IF_NOT,       /* if (t_src1 == 0) goto label        */
    IR_LABEL,           /* label:                             */

    /* Functions */
    IR_CALL,            /* t_dst = call func("label", args)   */
    IR_CALL_IND,        /* t_dst = call t_src1(t_src2, ...)   */
    IR_PARAM,           /* declare parameter N (ival)         */
    IR_RET,             /* return t_src1 (or void if src1<0)  */

    /* Memory / stack frame */
    IR_ALLOC,           /* allocate ival bytes on stack frame */
    IR_ARG,             /* argument in a call sequence        */

    /* Global data */
    IR_GLOBAL_STR,      /* emit global string "label"         */
    IR_GLOBAL_DATA,     /* emit global data (ival bytes)      */

    /* Special */
    IR_NOP,
    IR_SYSCALL,
} IRKind;

/* ==================================================================
 * IR Instruction
 * ================================================================== */

typedef struct {
    IRKind kind;

    int64_t ival;             /* immediate / offset / size / syscall num */
    const char *label;        /* label name / function name / global name */

    int temp_id;              /* destination temp (-1 = none)  */
    int src_id;               /* source temp / param id        */
    int src2_id;              /* second source temp (binary)   */

    int arg_count;            /* for CALL: number of arguments  */
    int *arg_ids;             /* for CALL: argument temp ids    */

    int type_size;            /* byte size of result type       */
    int is_signed;            /* signedness for LOAD/STORE/CMP  */
} IRInstr;

/* ==================================================================
 * IR Program  (flat list of instructions)
 * ================================================================== */

typedef struct {
    IRInstr *instrs;
    int count;
    int cap;
    char **owned_labels;
    int owned_label_count;
    int owned_label_cap;
} IRProgram;

/* Track dynamically allocated labels for safe destruction */
void ir_track_label(IRProgram *prog, const char *label);

/* ==================================================================
 * IR Generator context (per-function lowering state)
 * ================================================================== */

typedef struct {
    IRProgram *prog;       /* output program                  */
    SemaContext *sema;     /* semantic context for lookups    */
    int temp_counter;      /* next temp id                    */
    int label_counter;     /* next label number               */
    int frame_offset;      /* current stack frame offset      */
} IRGen;

/* ==================================================================
 * Public API
 * ================================================================== */

/* Construction / destruction */
IRProgram *ir_create(void);
void       ir_destroy(IRProgram *prog);

/* Emit helpers */
int  ir_emit(IRProgram *prog, IRKind kind, int dest, int src1, int src2);
int  ir_emit_const(IRProgram *prog, int64_t value);
int  ir_emit_label(IRProgram *prog, const char *label);
int  ir_emit_call(IRProgram *prog, int dest, const char *name,
                  int nargs, int *args);
int  ir_emit_alloc(IRProgram *prog, int size);
int  ir_emit_global_str(IRProgram *prog, const char *name);
int  ir_emit_addr_global(IRProgram *prog, const char *name);
int  ir_emit_addr_local(IRProgram *prog, int offset);
int  ir_emit_addr_param(IRProgram *prog, int param_n);
int  ir_emit_load(IRProgram *prog, int src, int type_size, int is_signed);
int  ir_emit_store(IRProgram *prog, int addr, int value, int type_size, int is_signed);

/* Debug */
void ir_print(const IRProgram *prog);

/* AST -> IR lowering */
IRProgram *ir_lower(Node *ast, SemaContext *sema);

#endif /* TC_IR_H */
