#ifndef TC_X86_64_H
#define TC_X86_64_H

#include "backend.h"
#include "ir.h"

typedef struct CodeGen CodeGen;

CodeGen* cg_create(void);
void cg_destroy(CodeGen *cg);
/* Generate machine code from IR program, write ELF binary to file.
   Returns 0 on success, -1 on error. */
int cg_generate(CodeGen *cg, IRProgram *prog, const char *output_path);

/* Compatibility with backend interface */
void x86_64_init(Backend *be, const char *output);

#endif
