#ifndef TC_BACKEND_H
#define TC_BACKEND_H

#include "ir.h"

typedef struct Backend {
    void (*emit_prologue)(IRProgram *prog);
    void (*emit_instruction)(IRInstr *inst);
    void (*emit_epilogue)(IRProgram *prog);
    void (*write_output)(const char *filename);
    void (*start_function)(IRProgram *prog);
    void (*end_function)(IRProgram *prog);
    void (*start_program)(IRProgram *prog);
    void (*end_program)(IRProgram *prog);
    void *context;
} Backend;

void backend_init_x86_64(Backend *be, const char *output);

#endif
