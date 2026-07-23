#ifndef TC_X86_64_H
#define TC_X86_64_H

#include "backend.h"
#include "ir.h"

#include <stddef.h>
#include <stdint.h>

#define MAX_INTERVALS 4096
#define MAX_LABELS    1024
#define MAX_PATCHES   2048
#define MAX_GLOBALS   512
#define MAX_TEMP      4096

typedef struct {
    int assigned;
    int is_reg;
    int reg_code;
    int stack_off;      // spill slot offset from rbp
    int local_off;      // if this temp holds addr of a local var, offset from rbp (INT_MIN=none)
    int pre_init;       // 1 if this temp's addr was pre-initialized after prologue
} TempLoc;

typedef struct {
    int temp_id;
    int start;
    int end;
} LiveInterval;

typedef struct {
    int code_offset;
    int target_label_idx;
    int patch_size;
} BranchPatch;

typedef struct {
    char name[256];
    int code_offset;
} LabelEntry;

typedef struct {
    char name[256];
    int rodata_offset;
    int data_offset;    /* offset in .data section (for IR_GLOBAL_DATA) */
} GlobalEntry;

struct CodeGen {
    uint8_t *code;
    size_t code_size;
    size_t code_cap;

    uint8_t *rodata;
    size_t rodata_size;
    size_t rodata_cap;

    uint8_t *data;
    size_t data_size;
    size_t data_cap;

    char output_path[512];

    TempLoc temps[MAX_TEMP];
    int max_temp;

    LiveInterval intervals[MAX_INTERVALS];
    int interval_count;

    LabelEntry labels[MAX_LABELS];
    int label_count;

    BranchPatch patches[MAX_PATCHES];
    int patch_count;

    GlobalEntry globals[MAX_GLOBALS];
    int global_count;

    int frame_size;
    int callee_saved_mask;
    int param_count;

    int free_regs[14];
    int free_reg_count;
    int reg_owner[16];

    int pushed_bytes;

    int object_mode;    /* 1 = ET_REL output, 0 = ET_EXEC */
};

typedef struct CodeGen CodeGen;

CodeGen* cg_create(void);
void cg_destroy(CodeGen *cg);
int cg_generate(CodeGen *cg, IRProgram *prog, const char *output_path);
void x86_64_init(Backend *be, const char *output);

/* Temp helpers (used by instruction module) */
int temp_is_assigned(CodeGen *cg, int tid);
int cg_load_temp(CodeGen *cg, int tid, int avoid_reg);
void cg_store_temp(CodeGen *cg, int tid);
int temp_get_reg(CodeGen *cg, int tid);
int temp_get_stack_off(CodeGen *cg, int tid);

/* Call convention helpers (used by instruction module) */
void cg_save_caller_saved(CodeGen *cg);
void cg_restore_caller_saved(CodeGen *cg);
void cg_emit_call_align(CodeGen *cg);
void cg_emit_call_unalign(CodeGen *cg);
void cg_emit_epilogue(CodeGen *cg);

/* Helpers called by instruction module */
int cg_load_temp(CodeGen *cg, int tid, int avoid_reg);
void cg_store_temp(CodeGen *cg, int tid);
int temp_get_reg(CodeGen *cg, int tid);
int temp_get_stack_off(CodeGen *cg, int tid);
void cg_save_caller_saved(CodeGen *cg);
void cg_restore_caller_saved(CodeGen *cg);
void cg_emit_call_align(CodeGen *cg);
void cg_emit_call_unalign(CodeGen *cg);
void cg_emit_epilogue(CodeGen *cg);

/* ABI parameter registers (System V AMD64) - shared with instruction module */
#define ABI_PARAM_REG_COUNT 6
extern const int ABI_PARAM_REGS[];

#endif
