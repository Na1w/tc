#ifndef TC_OPT_H
#define TC_OPT_H

#include "ir.h"

// Run all optimizer passes in sequence
void opt_run_all(IRProgram *prog);

// Individual passes (also callable separately)
void opt_constant_fold(IRProgram *prog);
void opt_dead_code_elim(IRProgram *prog);
void opt_copy_propagation(IRProgram *prog);
void opt_cse_basic_block(IRProgram *prog);

#endif /* TC_OPT_H */
