#!/usr/bin/env python3
"""Surgical patch for tc/src/x86_64.c to fix printf extern stub crash."""

with open('tc/src/x86_64.c', 'r') as f:
    code = f.read()

# ============================================================
# PART 1: Fix function discovery - add second pass for IR_CALL targets
# ============================================================

old_block1 = '''    /* Emit _start entry point (calls main) */
    cg_emit_start(cg);'''

new_block1 = '''    /* Second pass: discover labels referenced by IR_CALL (extern stubs)
     * These are labels like "printf" that have IR_LABEL + IR_RET but no params.
     * Without this, calls to them resolve to offset 0 (crash). */
    for (int i = 0; i < prog->count; i++) {
        if (prog->instrs[i].kind == IR_CALL) {
            int already_found = 0;
            for (int f = 0; f < func_count; f++) {
                if (strcmp(prog->instrs[func_starts[f]].label, prog->instrs[i].label) == 0) {
                    already_found = 1;
                    break;
                }
            }
            if (!already_found && func_count < MAX_LABELS) {
                for (int j = 0; j < prog->count; j++) {
                    if (prog->instrs[j].kind == IR_LABEL &&
                        strcmp(prog->instrs[j].label, prog->instrs[i].label) == 0) {
                        func_starts[func_count] = j;
                        func_param_counts[func_count] = 0;
                        func_count++;
                        break;
                    }
                }
            }
        }
    }

    /* Sort func_starts by instruction index (second pass may have added out of order) */
    for (int i = 0; i < func_count - 1; i++) {
        for (int j = i + 1; j < func_count; j++) {
            if (func_starts[j] < func_starts[i]) {
                int tmp = func_starts[i];
                func_starts[i] = func_starts[j];
                func_starts[j] = tmp;
                tmp = func_param_counts[i];
                func_param_counts[i] = func_param_counts[j];
                func_param_counts[j] = tmp;
            }
        }
    }

    /* Emit _start entry point (calls main) */
    cg_emit_start(cg);'''

if old_block1 not in code:
    print("ERROR: Could not find cg_emit_start block")
    exit(1)

code = code.replace(old_block1, new_block1)
print("[OK] Part 1: Added second pass for IR_CALL targets + sort")

# ============================================================
# PART 2: Add runtime helper functions before cg_emit_function
# ============================================================

old_block2 = '''static void cg_emit_function(CodeGen *cg, IRProgram *prog, int start, int end, int param_count)
{'''

new_block2 = '''/* ------------------------------------------------------------------ */
/*  Runtime function emitters (printf, exit, putstr)                  */
/*  Provide implementations for extern stubs.                         */
/* ------------------------------------------------------------------ */

static int is_extern_stub(IRProgram *prog, int start, int end, int param_count)
{
    if (param_count != 0) return 0;
    for (int i = start + 1; i < end; i++) {
        if (prog->instrs[i].kind != IR_RET) return 0;
    }
    return 1;
}

static void cg_emit_runtime_exit(CodeGen *cg)
{
    emit_mov_rimm(cg, R_RAX, 231);
    emit_syscall(cg);
    cg_emit_byte(cg, 0xEB);
    cg_emit_byte(cg, 0xFE);
}

static void cg_emit_runtime_putstr(CodeGen *cg)
{
    emit_push_r(cg, R_RBX);
    emit_mov_rr(cg, R_RBX, R_RDI);
    emit_mov_rimm32(cg, R_RAX, 0);
    emit_mov_rimm64(cg, R_RCX, -1);
    emit_mov_rr(cg, R_RDI, R_RBX);
    cg_emit_byte(cg, 0xF3);
    cg_emit_byte(cg, 0xAE);
    emit_not_r(cg, R_RCX);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0x83); cg_emit_byte(cg, 0xE9); cg_emit_byte(cg, 0x02);
    emit_test_rr(cg, R_RCX, R_RCX);
    int jz_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x74);
    cg_emit_byte(cg, 0x00);
    emit_mov_rimm32(cg, R_RAX, 1);
    emit_mov_rimm32(cg, R_RDI, 1);
    emit_mov_rr(cg, R_RSI, R_RBX);
    emit_syscall(cg);
    emit_pop_r(cg, R_RBX);
    emit_ret(cg);
    int target = (int)cg->code_size - 2;
    int rel = target - (jz_pos + 2);
    cg->code[jz_pos + 1] = (uint8_t)rel;
}

static void cg_emit_runtime_printf(CodeGen *cg)
{
    emit_push_r(cg, R_RBX);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0x81); cg_emit_byte(cg, 0xEC);
    cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x01); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0x89); cg_emit_byte(cg, 0x74); cg_emit_byte(cg, 0x24); cg_emit_byte(cg, 0x10);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0x89); cg_emit_byte(cg, 0x54); cg_emit_byte(cg, 0x24); cg_emit_byte(cg, 0x18);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0x89); cg_emit_byte(cg, 0x4c); cg_emit_byte(cg, 0x24); cg_emit_byte(cg, 0x20);
    cg_emit_byte(cg, 0x4c); cg_emit_byte(cg, 0x89); cg_emit_byte(cg, 0x44); cg_emit_byte(cg, 0x24); cg_emit_byte(cg, 0x28);
    cg_emit_byte(cg, 0x4c); cg_emit_byte(cg, 0x89); cg_emit_byte(cg, 0x4c); cg_emit_byte(cg, 0x24); cg_emit_byte(cg, 0x30);
    emit_mov_rr(cg, R_RSI, R_RSP);
    cg_emit_byte(cg, 0x31); cg_emit_byte(cg, 0xd2);
    int loop_start = (int)cg->code_size;
    cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0xb6); cg_emit_byte(cg, 0x07);
    cg_emit_byte(cg, 0x84); cg_emit_byte(cg, 0xc0);
    int done_off_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x74); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x3c); cg_emit_byte(cg, 0x25);
    int copy_char_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x75); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc7);
    cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0xb6); cg_emit_byte(cg, 0x0f);
    cg_emit_byte(cg, 0x80); cg_emit_byte(cg, 0xfa); cg_emit_byte(cg, 0x69);
    int print_int_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0x84); cg_emit_int32(cg, 0);
    cg_emit_byte(cg, 0x80); cg_emit_byte(cg, 0xfa); cg_emit_byte(cg, 0x64);
    int print_int_pos2 = (int)cg->code_size;
    cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0x84); cg_emit_int32(cg, 0);
    cg_emit_byte(cg, 0x80); cg_emit_byte(cg, 0xfa); cg_emit_byte(cg, 0x73);
    int print_str_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0x84); cg_emit_int32(cg, 0);
    cg_emit_byte(cg, 0x80); cg_emit_byte(cg, 0xfa); cg_emit_byte(cg, 0x63);
    int print_char_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0x84); cg_emit_int32(cg, 0);
    cg_emit_byte(cg, 0x80); cg_emit_byte(cg, 0xfa); cg_emit_byte(cg, 0x25);
    int print_pct_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0x84); cg_emit_int32(cg, 0);
    cg_emit_byte(cg, 0xc6); cg_emit_byte(cg, 0x06); cg_emit_byte(cg, 0x25);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    cg_emit_byte(cg, 0x88); cg_emit_byte(cg, 0x5f); cg_emit_byte(cg, 0x01);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc7);
    int def_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0xeb); cg_emit_byte(cg, 0x00);
    int print_char_target = (int)cg->code_size;
    cg_emit_byte(cg, 0x41); cg_emit_byte(cg, 0x8b); cg_emit_byte(cg, 0x14); cg_emit_byte(cg, 0x94);
    cg_emit_byte(cg, 0x10); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x88); cg_emit_byte(cg, 0x06);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc7);
    cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc2);
    int pchar_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0xeb); cg_emit_byte(cg, 0x00);
    int print_str_target = (int)cg->code_size;
    cg_emit_byte(cg, 0x41); cg_emit_byte(cg, 0x8b); cg_emit_byte(cg, 0x14); cg_emit_byte(cg, 0x94);
    cg_emit_byte(cg, 0x10); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00);
    int str_loop = (int)cg->code_size;
    cg_emit_byte(cg, 0x44); cg_emit_byte(cg, 0x0f); cg_emit_byte(cg, 0xb6); cg_emit_byte(cg, 0x02);
    cg_emit_byte(cg, 0x44); cg_emit_byte(cg, 0x84); cg_emit_byte(cg, 0x00);
    int str_done_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x74); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x44); cg_emit_byte(cg, 0x88); cg_emit_byte(cg, 0x06);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc2);
    int str_loop_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0xeb); cg_emit_byte(cg, 0x00);
    int str_done_target = (int)cg->code_size;
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc7);
    cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc2);
    int pstr_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0xeb); cg_emit_byte(cg, 0x00);
    int print_int_target = (int)cg->code_size;
    cg_emit_byte(cg, 0x41); cg_emit_byte(cg, 0x8b); cg_emit_byte(cg, 0x04); cg_emit_byte(cg, 0x94);
    cg_emit_byte(cg, 0x10); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x49); cg_emit_byte(cg, 0x89); cg_emit_byte(cg, 0xc2);
    emit_test_rr(cg, R_RAX, R_RAX);
    int int_pos_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x79); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xf7); cg_emit_byte(cg, 0xd8);
    cg_emit_byte(cg, 0xc6); cg_emit_byte(cg, 0x06); cg_emit_byte(cg, 0x2d);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    int int_pos_target = (int)cg->code_size;
    cg_emit_byte(cg, 0x41); cg_emit_byte(cg, 0xb8); cg_emit_byte(cg, 0x0a);
    cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00);
    int int_div_loop = (int)cg->code_size;
    cg_emit_byte(cg, 0x41); cg_emit_byte(cg, 0x31); cg_emit_byte(cg, 0xd2);
    cg_emit_byte(cg, 0x49); cg_emit_byte(cg, 0xf7); cg_emit_byte(cg, 0xf0);
    cg_emit_byte(cg, 0x62); cg_emit_byte(cg, 0xd2); cg_emit_byte(cg, 0x30);
    cg_emit_byte(cg, 0x52);
    emit_test_rr(cg, R_RAX, R_RAX);
    int int_div_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x75); cg_emit_byte(cg, 0x00);
    int int_print = (int)cg->code_size;
    cg_emit_byte(cg, 0x5a);
    cg_emit_byte(cg, 0x88); cg_emit_byte(cg, 0x16);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc9);
    emit_test_rr(cg, R_RCX, R_RCX);
    int int_print_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x75); cg_emit_byte(cg, 0x00);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc7);
    cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc2);
    int pint_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0xeb); cg_emit_byte(cg, 0x00);
    int print_pct_target = (int)cg->code_size;
    cg_emit_byte(cg, 0xc6); cg_emit_byte(cg, 0x06); cg_emit_byte(cg, 0x25);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc7);
    int ppct_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0xeb); cg_emit_byte(cg, 0x00);
    int copy_char_target = (int)cg->code_size;
    cg_emit_byte(cg, 0x88); cg_emit_byte(cg, 0x06);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc6);
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0xff); cg_emit_byte(cg, 0xc7);
    int copy_jmp_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0xeb); cg_emit_byte(cg, 0x00);
    int done_target = (int)cg->code_size;
    emit_sub_rr(cg, R_RSI, R_RSP);
    emit_mov_rr(cg, R_RDX, R_RSI);
    emit_test_rr(cg, R_RDX, R_RDX);
    int skip_write_pos = (int)cg->code_size;
    cg_emit_byte(cg, 0x74); cg_emit_byte(cg, 0x00);
    emit_mov_rimm32(cg, R_RAX, 1);
    emit_mov_rimm32(cg, R_RDI, 1);
    emit_mov_rr(cg, R_RSI, R_RSP);
    emit_syscall(cg);
    int skip_write_target = (int)cg->code_size;
    cg_emit_byte(cg, 0x48); cg_emit_byte(cg, 0x81); cg_emit_byte(cg, 0xc4);
    cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x01); cg_emit_byte(cg, 0x00); cg_emit_byte(cg, 0x00);
    emit_pop_r(cg, R_RBX);
    emit_mov_rr(cg, R_RAX, R_RDX);
    emit_ret(cg);
    int rel;
    int32_t rel32;
    rel = done_target - (done_off_pos + 2);
    cg->code[done_off_pos + 1] = (uint8_t)rel;
    rel = copy_char_target - (copy_char_pos + 2);
    cg->code[copy_char_pos + 1] = (uint8_t)rel;
    rel = print_int_target - (print_int_pos + 6);
    rel32 = (int32_t)rel;
    memcpy(cg->code + print_int_pos + 2, &rel32, 4);
    rel = print_int_target - (print_int_pos2 + 6);
    rel32 = (int32_t)rel;
    memcpy(cg->code + print_int_pos2 + 2, &rel32, 4);
    rel = print_str_target - (print_str_pos + 6);
    rel32 = (int32_t)rel;
    memcpy(cg->code + print_str_pos + 2, &rel32, 4);
    rel = print_char_target - (print_char_pos + 6);
    rel32 = (int32_t)rel;
    memcpy(cg->code + print_char_pos + 2, &rel32, 4);
    rel = print_pct_target - (print_pct_pos + 6);
    rel32 = (int32_t)rel;
    memcpy(cg->code + print_pct_pos + 2, &rel32, 4);
    rel = loop_start - (def_jmp_pos + 2);
    cg->code[def_jmp_pos + 1] = (uint8_t)rel;
    rel = loop_start - (pchar_jmp_pos + 2);
    cg->code[pchar_jmp_pos + 1] = (uint8_t)rel;
    rel = str_done_target - (str_done_pos + 2);
    cg->code[str_done_pos + 1] = (uint8_t)rel;
    rel = str_loop - (str_loop_jmp_pos + 2);
    cg->code[str_loop_jmp_pos + 1] = (uint8_t)rel;
    rel = loop_start - (pstr_jmp_pos + 2);
    cg->code[pstr_jmp_pos + 1] = (uint8_t)rel;
    rel = int_pos_target - (int_pos_pos + 2);
    cg->code[int_pos_pos + 1] = (uint8_t)rel;
    rel = int_div_loop - (int_div_jmp_pos + 2);
    cg->code[int_div_jmp_pos + 1] = (uint8_t)rel;
    rel = int_print - (int_print_jmp_pos + 2);
    cg->code[int_print_jmp_pos + 1] = (uint8_t)rel;
    rel = loop_start - (pint_jmp_pos + 2);
    cg->code[pint_jmp_pos + 1] = (uint8_t)rel;
    rel = loop_start - (ppct_jmp_pos + 2);
    cg->code[ppct_jmp_pos + 1] = (uint8_t)rel;
    rel = loop_start - (copy_jmp_pos + 2);
    cg->code[copy_jmp_pos + 1] = (uint8_t)rel;
    rel = skip_write_target - (skip_write_pos + 2);
    cg->code[skip_write_pos + 1] = (uint8_t)rel;
}

static void cg_emit_function(CodeGen *cg, IRProgram *prog, int start, int end, int param_count)
{'''

if old_block2 not in code:
    print("ERROR: Could not find cg_emit_function declaration")
    exit(1)

code = code.replace(old_block2, new_block2)
print("[OK] Part 2: Added runtime helper functions")

# ============================================================
# PART 3: Add extern stub check at start of cg_emit_function
# ============================================================

old_block3 = '''static void cg_emit_function(CodeGen *cg, IRProgram *prog, int start, int end, int param_count)
{
    /* Remap temp_ids for this function to be unique */
    cg_remap_temps_func(prog->instrs, start, end, 0);'''

new_block3 = '''static void cg_emit_function(CodeGen *cg, IRProgram *prog, int start, int end, int param_count)
{
    /* Check if this is an extern stub (no params, just IR_RET body) */
    int is_stub = is_extern_stub(prog, start, end, param_count);
    if (is_stub) {
        const char *fname = prog->instrs[start].label;
        {
            int existing = cg_find_label(cg, fname);
            if (existing >= 0) {
                cg->labels[existing].code_offset = (int)cg->code_size;
            } else {
                cg_add_label(cg, fname, (int)cg->code_size);
            }
        }
        if (strcmp(fname, "exit") == 0) { cg_emit_runtime_exit(cg); return; }
        if (strcmp(fname, "putstr") == 0) { cg_emit_runtime_putstr(cg); return; }
        if (strcmp(fname, "printf") == 0) { cg_emit_runtime_printf(cg); return; }
        cg_emit_epilogue(cg);
        return;
    }
    /* Remap temp_ids for this function to be unique */
    cg_remap_temps_func(prog->instrs, start, end, 0);'''

if old_block3 not in code:
    print("ERROR: Could not find cg_emit_function body start")
    exit(1)

code = code.replace(old_block3, new_block3)
print("[OK] Part 3: Added extern stub check in cg_emit_function")

with open('tc/src/x86_64.c', 'w') as f:
    f.write(code)

print("Patch complete!")
