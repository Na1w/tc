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

    /* Collect function boundaries: each IR_LABEL marks start of a function */
    typedef struct {
        int start;
        int end;
        char name[256];
    } FuncRange;

    FuncRange funcs[MAX_LABELS];
    int func_count = 0;

    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == IR_LABEL) {
            if (func_count < MAX_LABELS) {
                funcs[func_count].start = i;
                snprintf(funcs[func_count].name, sizeof(funcs[func_count].name),
                         "%s", prog->instrs[i].label);
                func_count++;
            }
        }
    }

    for (int f = 0; f < func_count; f++) {
        if (f + 1 < func_count)
            funcs[f].end = funcs[f + 1].start;
        else
            funcs[f].end = prog->count;
    }

    /* Emit _start entry point */
    cg_emit_start(cg);

    /* Process each function independently */
    for (int f = 0; f < func_count; f++) {
        /* Reset per-function state */
        cg->frame_size = 0;
        cg->callee_saved_mask = 0;
        cg->pushed_bytes = 0;
        cg->param_count = 0;
        temp_init(cg);

        /* Remap temp_ids to be unique per-function (0,1,2,...) */
        cg_remap_temps_func(prog, funcs[f].start, funcs[f].end);

        /* Compute live intervals for this function only */
        IRProgram sub;
        sub.count = funcs[f].end - funcs[f].start;
        sub.instrs = &prog->instrs[funcs[f].start];
        cg_compute_intervals(cg, &sub);
        cg_linear_scan(cg);

        /* Pre-pass: collect IR_ALLOC sizes and IR_PARAM counts */
        for (int i = funcs[f].start; i < funcs[f].end; i++) {
            if (prog->instrs[i].kind == IR_ALLOC) {
                int needed = (int)prog->instrs[i].ival;
                if (needed > cg->frame_size)
                    cg->frame_size = needed;
            }
            if (prog->instrs[i].kind == IR_PARAM) {
                int param_n = (int)prog->instrs[i].ival;
                if (param_n + 1 > cg->param_count)
                    cg->param_count = param_n + 1;
            }
        }
        cg->frame_size = (cg->frame_size + 15) & ~15;

        /* Record label and emit prologue */
        int lbl = cg_find_label(cg, funcs[f].name);
        if (lbl < 0) {
            cg_add_label(cg, funcs[f].name, (int)cg->code_size);
        } else if (cg->labels[lbl].code_offset < 0) {
            cg->labels[lbl].code_offset = (int)cg->code_size;
        }

        cg_emit_prologue(cg);

        /* Emit instructions for this function */
        for (int i = funcs[f].start; i < funcs[f].end; i++) {
            IRInstr *inst = &prog->instrs[i];
            if (inst->kind == IR_LABEL) continue;
            cg_emit_instr(cg, inst);
        }

        /* If no RET was emitted, add default return */
        int has_ret = 0;
        for (int i = funcs[f].start; i < funcs[f].end; i++) {
            if (prog->instrs[i].kind == IR_RET) { has_ret = 1; break; }
        }
        if (!has_ret) {
            emit_mov_rimm(cg, R_RAX, 0);
            cg_emit_epilogue(cg);
        }
    }

    /* Patch branches */
    for (int i = 0; i < cg->label_count; i++) {
        if (strcmp(cg->labels[i].name, "main") == 0 && cg->labels[i].code_offset < 0) {
            cg->labels[i].code_offset = (int)cg->code_size;
        }
    }
    cg_patch_branches(cg);

    /* Write ELF */
    {
        ElfWriter *elf = elf_create();
        if (elf) {
            elf_add_text(elf, cg->code, cg->code_size);
            elf_add_rodata(elf, cg->rodata, cg->rodata_size);
            elf->entry = ELF_TEXT_BASE;
            elf_define_symbol(elf, "_start", ELF_TEXT_BASE);
            elf_write(elf, cg->output_path);
            elf_destroy(elf);
        }
    }

    return 0;
}
