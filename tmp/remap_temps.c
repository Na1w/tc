/* Fix: Remap temp_ids per-function so register allocator doesn't collide
   across functions. Each function gets its own t0, t1, t2, ... range. */

/* Build remap table: for each function, map global temp_ids to local 0..N */
static void cg_remap_temps(IRProgram *prog, int func_start, int func_end)
{
    /* First pass: find all temp_ids used in this function */
    int seen[MAX_TEMP];
    memset(seen, 0, sizeof(seen));
    int max_seen = 0;
    
    for (int i = func_start; i < func_end; i++) {
        IRInstr *inst = &prog->instrs[i];
        int ids[3] = { inst->temp_id, inst->src_id, inst->src2_id };
        for (int j = 0; j < 3; j++) {
            if (ids[j] >= 0) {
                if (!seen[ids[j]]) {
                    seen[ids[j]] = 1;
                    if (ids[j] > max_seen) max_seen = ids[j];
                }
            }
        }
        /* Also check call arg_ids */
        if (inst->kind == IR_CALL || inst->kind == IR_CALL_IND) {
            for (int a = 0; a < inst->arg_count; a++) {
                if (inst->arg_ids[a] >= 0) {
                    if (!seen[inst->arg_ids[a]]) {
                        seen[inst->arg_ids[a]] = 1;
                        if (inst->arg_ids[a] > max_seen) max_seen = inst->arg_ids[a];
                    }
                }
            }
        }
    }
    
    /* Second pass: assign local indices */
    int mapping[MAX_TEMP];
    int next_local = 0;
    for (int i = 0; i <= max_seen; i++) {
        if (seen[i]) {
            mapping[i] = next_local++;
        } else {
            mapping[i] = -1;
        }
    }
    
    /* Third pass: remap instructions in-place */
    for (int i = func_start; i < func_end; i++) {
        IRInstr *inst = &prog->instrs[i];
        if (inst->temp_id >= 0 && seen[inst->temp_id])
            inst->temp_id = mapping[inst->temp_id];
        if (inst->src_id >= 0 && seen[inst->src_id])
            inst->src_id = mapping[inst->src_id];
        if (inst->src2_id >= 0 && seen[inst->src2_id])
            inst->src2_id = mapping[inst->src2_id];
        /* Remap call arg_ids */
        if (inst->kind == IR_CALL || inst->kind == IR_CALL_IND) {
            for (int a = 0; a < inst->arg_count; a++) {
                if (inst->arg_ids[a] >= 0 && seen[inst->arg_ids[a]])
                    inst->arg_ids[a] = mapping[inst->arg_ids[a]];
            }
        }
    }
}
