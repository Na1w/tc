/*
 * main.c -- tc C compiler, entry point
 *
 * Pipeline:
 *   source → lexer → parser (AST) → sema (type-check)
 *          → IR lowering → optimization → x86-64 codegen → ELF binary
 *
 * Usage:
 *   tc [-o output] input.c    Compile to executable
 *   tc -S input.c             Generate assembly listing only
 *   tc -c input.c             Generate object file only (ELF)
 *   tc -d <phase> input.c     Dump IR/AST after phase (ast, sema, ir, opt)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "ir.h"
#include "opt.h"
#include "linker.h"
#include "x86_64.h"
#include "disasm.h"
#include "elf.h"

/* ==================================================================
 * File I/O
 * ================================================================== */

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "tc: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "tc: out of memory\n");
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, f);
    buf[nread] = '\0';
    fclose(f);

    if (out_size) *out_size = (size_t)fsize;
    return buf;
}

/* ==================================================================
 * CLI / Usage
 * ================================================================== */

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <file.c>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>    Output file (default: a.out)\n");
    fprintf(stderr, "  -S           Emit assembly listing only\n");
    fprintf(stderr, "  -c           Compile only, no link (ELF object)\n");
    fprintf(stderr, "  -d <phase>   Dump after phase (ast, sema, ir, opt, disasm)\n");
    fprintf(stderr, "  -v           Verbose\n");
    fprintf(stderr, "  -h           Show this help\n");
}

/* ==================================================================
 * Assembly text emitter (for -S)
 * ================================================================== */

static const char *ir_kind_name(IRKind kind) {
    switch (kind) {
        case IR_MOV:           return "mov";
        case IR_CONST:         return "const";
        case IR_ADDR_GLOBAL:   return "lea_global";
        case IR_ADDR_LOCAL:    return "lea_local";
        case IR_LOAD:          return "load";
        case IR_STORE:         return "store";
        case IR_ADDR_PARAM:    return "lea_param";
        case IR_ADD:           return "add";
        case IR_SUB:           return "sub";
        case IR_MUL:           return "imul";
        case IR_DIV:           return "idiv";
        case IR_MOD:           return "imod";
        case IR_BAND:          return "and";
        case IR_BOR:           return "or";
        case IR_XOR:           return "xor";
        case IR_SHL:           return "shl";
        case IR_SHR:           return "shr";
        case IR_NEG:           return "neg";
        case IR_NOT:           return "not";
        case IR_BITNOT:        return "bnot";
        case IR_CMP_EQ:        return "cmpeq";
        case IR_CMP_NEQ:       return "cmpneq";
        case IR_CMP_LT:        return "cmplt";
        case IR_CMP_GT:        return "cmpgt";
        case IR_CMP_LEQ:       return "cmpleq";
        case IR_CMP_GEQ:       return "cmpgeq";
        case IR_BR:            return "br";
        case IR_BR_IF:         return "br_if";
        case IR_BR_IF_NOT:     return "br_if_not";
        case IR_LABEL:         return "label";
        case IR_CALL:          return "call";
        case IR_CALL_IND:      return "call_ind";
        case IR_PARAM:         return "param";
        case IR_RET:           return "ret";
        case IR_ALLOC:         return "alloc";
        case IR_ARG:           return "arg";
        case IR_GLOBAL_STR:    return "global_str";
        case IR_GLOBAL_DATA:   return "global_data";
        case IR_NOP:           return "nop";
        case IR_SYSCALL:       return "syscall";
        default:               return "???";
    }
}

static void emit_asm_program(FILE *f, const IRProgram *prog) {
    fprintf(f, ".intel_syntax noprefix\n");
    fprintf(f, ".text\n");
    fprintf(f, ".globl main\n");

    for (int i = 0; i < prog->count; i++) {
        const IRInstr *inst = &prog->instrs[i];

        switch (inst->kind) {
        case IR_LABEL:
            fprintf(f, "%s:\n", inst->label ? inst->label : "L???");
            break;

        case IR_CONST:
            fprintf(f, "    mov rax, %ld\n", (long)inst->ival);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_MOV:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_ADD:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    add rax, [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_SUB:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    sub rax, [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_MUL:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    imul rax, [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_DIV:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cqo\n");
            fprintf(f, "    idiv [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_MOD:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cqo\n");
            fprintf(f, "    idiv [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rdx\n", inst->temp_id + 1);
            break;

        case IR_BAND:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    and rax, [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_BOR:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    or rax, [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_XOR:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    xor rax, [rbp-8*%d]\n", inst->src2_id + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_SHL:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    mov rcx, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    shl rax, cl\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_SHR:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    mov rcx, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    shr rax, cl\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_NEG:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    neg rax\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_NOT:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    test rax, rax\n");
            fprintf(f, "    setne al\n");
            fprintf(f, "    movzx rax, al\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_BITNOT:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    not rax\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_CMP_EQ:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cmp rax, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    sete al\n");
            fprintf(f, "    movzx rax, al\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_CMP_NEQ:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cmp rax, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    setne al\n");
            fprintf(f, "    movzx rax, al\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_CMP_LT:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cmp rax, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    setl al\n");
            fprintf(f, "    movzx rax, al\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_CMP_GT:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cmp rax, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    setg al\n");
            fprintf(f, "    movzx rax, al\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_CMP_LEQ:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cmp rax, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    setle al\n");
            fprintf(f, "    movzx rax, al\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_CMP_GEQ:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    cmp rax, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    setge al\n");
            fprintf(f, "    movzx rax, al\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_BR:
            fprintf(f, "    jmp %s\n", inst->label ? inst->label : "L???");
            break;

        case IR_BR_IF:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    test rax, rax\n");
            fprintf(f, "    jne %s\n", inst->label ? inst->label : "L???");
            break;

        case IR_BR_IF_NOT:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    test rax, rax\n");
            fprintf(f, "    je %s\n", inst->label ? inst->label : "L???");
            break;

        case IR_CALL: {
            const char *fname = inst->label ? inst->label : "L???";
            int nargs = inst->arg_count;
            for (int a = nargs - 1; a >= 0; a--) {
                int arg_id = inst->arg_ids[a];
                fprintf(f, "    mov rax, [rbp-8*%d]\n", arg_id + 1);
                fprintf(f, "    push rax\n");
            }
            fprintf(f, "    call %s\n", fname);
            fprintf(f, "    add rsp, %d\n", nargs * 8);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;
        }

        case IR_CALL_IND:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    call rax\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_RET:
            if (inst->src_id >= 0) {
                fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            }
            fprintf(f, "    leave\n");
            fprintf(f, "    ret\n");
            break;

        case IR_ALLOC:
            fprintf(f, "    ; alloc %ld bytes on stack\n", (long)inst->ival);
            break;

        case IR_ADDR_GLOBAL:
            fprintf(f, "    lea rax, [%s]\n", inst->label ? inst->label : "global");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_ADDR_LOCAL:
            fprintf(f, "    lea rax, [rbp+%ld]\n", (long)inst->ival);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_ADDR_PARAM:
            fprintf(f, "    lea rax, [rbp+8*%ld]\n", (long)inst->ival + 1);
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_LOAD:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    mov rax, [rax]\n");
            if (inst->temp_id >= 0)
                fprintf(f, "    mov [rbp-8*%d], rax\n", inst->temp_id + 1);
            break;

        case IR_STORE:
            fprintf(f, "    mov rax, [rbp-8*%d]\n", inst->src2_id + 1);
            fprintf(f, "    mov rcx, [rbp-8*%d]\n", inst->src_id + 1);
            fprintf(f, "    mov [rcx], rax\n");
            break;

        case IR_PARAM:
            fprintf(f, "    ; param %ld\n", (long)inst->ival);
            break;

        case IR_ARG:
            fprintf(f, "    push [rbp-8*%d]\n", inst->src_id + 1);
            break;

        case IR_GLOBAL_STR:
            fprintf(f, "    ; global string: %s\n", inst->label ? inst->label : "str");
            break;

        case IR_GLOBAL_DATA:
            fprintf(f, "    ; global data: %ld bytes\n", (long)inst->ival);
            break;

        case IR_NOP:
            fprintf(f, "    nop\n");
            break;

        case IR_SYSCALL:
            fprintf(f, "    mov rax, %ld\n", (long)inst->ival);
            fprintf(f, "    syscall\n");
            break;

        default:
            fprintf(f, "    ; unknown IR instruction: %d (%s)\n",
                    inst->kind, ir_kind_name(inst->kind));
            break;
        }
    }
}

/* ==================================================================
 * Semantic dump helpers
 * ================================================================== */

static void dump_symbol_table(FILE *f, const SemaContext *sema) {
    fprintf(f, "=== Symbol Table ===\n");
    fprintf(f, "Types: %d\n", sema->type_count);
    fprintf(f, "Symbols resolved: %d\n", sema->sym_count);
    fprintf(f, "Errors: %d\n", sema->error_count);

    const Scope *scope = &sema->global_scope;
    fprintf(f, "\n--- Global Scope ---\n");
    for (int i = 0; i < 256; i++) {
        if (scope->symbols[i]) {
            const Symbol *sym = scope->symbols[i];
            fprintf(f, "  [%d] %-20s kind=%d hash=0x%08x",
                    i, sym->name ? sym->name : "(null)",
                    sym->kind, sym->hash);
            if (sym->type) {
                fprintf(f, " type_id=%d", sym->type->kind);
            }
            if (sym->is_extern) {
                fprintf(f, " extern");
            }
            fprintf(f, "\n");
        }
    }

    if (sema->resolved_symbols && sema->sym_count > 0) {
        fprintf(f, "\n--- Resolved Symbols ---\n");
        for (int i = 0; i < sema->sym_count; i++) {
            const Symbol *sym = sema->resolved_symbols[i];
            if (sym) {
                fprintf(f, "  %-20s kind=%d hash=0x%08x",
                        sym->name ? sym->name : "(null)",
                        sym->kind, sym->hash);
                if (sym->type) {
                    fprintf(f, " type_kind=%d", sym->type->kind);
                }
                fprintf(f, "\n");
            }
        }
    }
}

/* ==================================================================
 * Minimal Preprocessor
 * ==================================================================
 *
 * Text-based preprocessing pass that handles:
 *   #include <file.h>   - Include from tc/include/ directory
 *   #include "file.h"   - Include from source file's directory
 *   #define MACRO val   - Simple macro definition
 *   #ifdef MACRO        - Conditional inclusion
 *   #ifndef MACRO       - Conditional inclusion
 *   #endif              - End conditional
 *
 * All other # directives are silently skipped.
 * ================================================================== */

/* Simple macro definition for preprocessor */
typedef struct MacroDef {
    char name[64];
    char value[256];
    struct MacroDef *next;
} MacroDef;

static MacroDef *g_macros = NULL;

static void preprocessor_add_macro(const char *name, const char *value) {
    MacroDef *m = (MacroDef *)malloc(sizeof(MacroDef));
    strncpy(m->name, name, 63);
    m->name[63] = '\0';
    strncpy(m->value, value, 255);
    m->value[255] = '\0';
    m->next = g_macros;
    g_macros = m;
}

static int preprocessor_is_defined(const char *name) {
    for (MacroDef *m = g_macros; m; m = m->next) {
        if (strcmp(m->name, name) == 0) return 1;
    }
    return 0;
}

static void preprocessor_free_macros(void) {
    MacroDef *m = g_macros;
    while (m) {
        MacroDef *next = m->next;
        free(m);
        m = next;
    }
    g_macros = NULL;
}

/* Expand macros in a line of text. Returns newly allocated string. */
static char *preprocessor_expand_macros(const char *line, size_t len) {
    if (!g_macros) {
        /* No macros defined, return copy */
        char *result = (char *)malloc(len + 1);
        if (result) { memcpy(result, line, len); result[len] = '\0'; }
        return result;
    }

    /* Allocate output buffer - could be larger due to macro expansion */
    size_t cap = len * 4 + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t out = 0;

    const char *p = line;
    while (p < line + len) {
        /* Check if we're at the start of an identifier */
        if ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z') || p[0] == '_') {
            /* Read full identifier */
            const char *id_start = p;
            while (p < line + len &&
                   ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z') ||
                    (p[0] >= '0' && p[0] <= '9') || p[0] == '_')) {
                p++;
            }
            size_t id_len = (size_t)(p - id_start);

            /* Look up in macro table */
            int found = 0;
            for (MacroDef *m = g_macros; m; m = m->next) {
                if (strlen(m->name) == id_len &&
                    strncmp(m->name, id_start, id_len) == 0) {
                    /* Expand macro */
                    size_t val_len = strlen(m->value);
                    if (out + val_len + 1 >= cap) {
                        cap = (out + val_len + 1) * 2;
                        char *new_buf = (char *)realloc(buf, cap);
                        if (!new_buf) { free(buf); return NULL; }
                        buf = new_buf;
                    }
                    memcpy(buf + out, m->value, val_len);
                    out += val_len;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                /* Not a macro, copy identifier as-is */
                if (out + id_len + 1 >= cap) {
                    cap = (out + id_len + 1) * 2;
                    char *new_buf = (char *)realloc(buf, cap);
                    if (!new_buf) { free(buf); return NULL; }
                    buf = new_buf;
                }
                memcpy(buf + out, id_start, id_len);
                out += id_len;
            }
        } else {
            /* Not identifier start, copy char */
            if (out + 2 >= cap) {
                cap = (out + 2) * 2;
                char *new_buf = (char *)realloc(buf, cap);
                if (!new_buf) { free(buf); return NULL; }
                buf = new_buf;
            }
            buf[out++] = *p++;
        }
    }
    buf[out] = '\0';
    return buf;
}

/*
 * preprocess_source - Perform text-based preprocessing on source code.
 *
 * Returns a newly allocated string with preprocessing done, or NULL on error.
 * The original source is NOT freed.
 * depth: recursion depth limit (max 10)
 */
static char *preprocess_source(const char *source, size_t source_len,
                                const char *input_file, int verbose,
                                int depth) {
    /* Prevent infinite recursion */
    if (depth > 10) {
        fprintf(stderr, "preprocessor: include depth exceeded (max 10)\n");
        return NULL;
    }

    /* Build include search path: directory of input file + tc/include/ */
    char include_local[512] = ".";
    char include_std[512] = "include";

    /* Determine local include directory from input file path */
    const char *slash = strrchr(input_file, '/');
    if (slash && slash != input_file) {
        size_t dir_len = (size_t)(slash - input_file);
        if (dir_len < sizeof(include_local) - 1) {
            memcpy(include_local, input_file, dir_len);
            include_local[dir_len] = '\0';
        }
    }

    /* Allocate output buffer - start with 4x source size for includes */
    size_t cap = source_len * 4 + 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t out_len = 0;

    /* Process line by line */
    const char *p = source;
    const char *end = source + source_len;
    int skip_block = 0; /* for #ifdef/#ifndef blocks */
    int skip_depth = 0; /* nested skip tracking */

    while (p < end) {
        /* Find start of next line */
        const char *line_start = p;

        /* Skip leading whitespace */
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        /* Find end of line (the \n character) */
        const char *line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;

        /* Check if this is a preprocessor directive */
        if (p < end && *p == '#') {
            /* Extract directive text (skip # and whitespace) */
            const char *dir = p + 1;
            while (dir < line_end && (*dir == ' ' || *dir == '\t')) dir++;

            /* Parse directive keyword */
            char directive[64];
            int di = 0;
            while (dir < line_end && di < 63 &&
                   (*dir != ' ' && *dir != '\t' && *dir != '\n')) {
                directive[di++] = *dir++;
            }
            directive[di] = '\0';

            /* Skip rest of line whitespace */
            while (dir < line_end && (*dir == ' ' || *dir == '\t')) dir++;

            /* Process directive */
            if (strcmp(directive, "include") == 0) {
                /* #include directive */
                if (skip_block) {
                    /* Skip this line */
                    p = line_end + 1;
                    continue;
                }

                /* Extract filename */
                char filename[256];
                int is_std = 0; /* 1 for <>, 0 for "" */

                if (*dir == '<') {
                    is_std = 1;
                    dir++;
                    int fi = 0;
                    while (dir < line_end && *dir != '>' && fi < 255) {
                        filename[fi++] = *dir++;
                    }
                    filename[fi] = '\0';
                } else if (*dir == '"') {
                    dir++;
                    int fi = 0;
                    while (dir < line_end && *dir != '"' && fi < 255) {
                        filename[fi++] = *dir++;
                    }
                    filename[fi] = '\0';
                } else {
                    fprintf(stderr, "preprocessor: bad #include syntax\n");
                    p = line_end + 1;
                    continue;
                }

                /* Build include path */
                char include_path[512];
                if (is_std) {
                    snprintf(include_path, sizeof(include_path),
                             "%s/%s", include_std, filename);
                } else {
                    snprintf(include_path, sizeof(include_path),
                             "%s/%s", include_local, filename);
                }

                /* Read included file */
                FILE *f = fopen(include_path, "r");
                if (!f) {
                    /* Try alternate path: ./include/ */
                    snprintf(include_path, sizeof(include_path),
                             "./include/%s", filename);
                    f = fopen(include_path, "r");
                }

                if (!f) {
                    fprintf(stderr, "preprocessor: cannot open '%s' "
                            "(tried '%s')\n", filename, include_path);
                    p = line_end + 1;
                    continue;
                }

                fseek(f, 0, SEEK_END);
                long inc_size = ftell(f);
                fseek(f, 0, SEEK_SET);

                char *inc_buf = (char *)malloc((size_t)inc_size + 1);
                if (!inc_buf) {
                    fclose(f);
                    p = line_end + 1;
                    continue;
                }
                fread(inc_buf, 1, (size_t)inc_size, f);
                inc_buf[inc_size] = '\0';
                fclose(f);

                /* Recursively preprocess the included file */
                char *processed = preprocess_source(inc_buf, (size_t)inc_size,
                                                     include_path, verbose,
                                                     depth + 1);
                free(inc_buf);

                if (!processed) {
                    p = line_end + 1;
                    continue;
                }

                /* Append to output buffer */
                size_t proc_len = strlen(processed);
                if (out_len + proc_len + 1 >= cap) {
                    cap = (out_len + proc_len + 1) * 2;
                    char *new_buf = (char *)realloc(buf, cap);
                    if (!new_buf) {
                        free(processed);
                        free(buf);
                        return NULL;
                    }
                    buf = new_buf;
                }
                memcpy(buf + out_len, processed, proc_len);
                out_len += proc_len;
                free(processed);

                if (verbose) {
                    fprintf(stderr, "preprocessor: included '%s'\n", filename);
                }

                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "define") == 0) {
                /* #define NAME [value] */
                if (skip_block) {
                    p = line_end + 1;
                    continue;
                }
                char name[64], value[256];
                int ni = 0;
                while (dir < line_end && ni < 63 &&
                       (*dir != ' ' && *dir != '\t')) {
                    name[ni++] = *dir++;
                }
                name[ni] = '\0';

                while (dir < line_end && (*dir == ' ' || *dir == '\t')) dir++;

                int vi = 0;
                while (dir < line_end && vi < 255 && *dir != '\n') {
                    value[vi++] = *dir++;
                }
                value[vi] = '\0';

                preprocessor_add_macro(name, value);
                if (verbose) {
                    fprintf(stderr, "preprocessor: defined %s = %s\n",
                            name, value);
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "ifdef") == 0) {
                if (skip_block) {
                    skip_depth++;
                    p = line_end + 1;
                    continue;
                }
                char name[64];
                int ni = 0;
                while (dir < line_end && ni < 63 &&
                       (*dir != ' ' && *dir != '\t')) {
                    name[ni++] = *dir++;
                }
                name[ni] = '\0';

                if (!preprocessor_is_defined(name)) {
                    skip_block = 1;
                    skip_depth = 0;
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "ifndef") == 0) {
                if (skip_block) {
                    skip_depth++;
                    p = line_end + 1;
                    continue;
                }
                char name[64];
                int ni = 0;
                while (dir < line_end && ni < 63 &&
                       (*dir != ' ' && *dir != '\t')) {
                    name[ni++] = *dir++;
                }
                name[ni] = '\0';

                if (preprocessor_is_defined(name)) {
                    skip_block = 1;
                    skip_depth = 0;
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "endif") == 0) {
                if (skip_block) {
                    if (skip_depth > 0) {
                        skip_depth--;
                    } else {
                        skip_block = 0;
                    }
                }
                p = line_end + 1;
                continue;

            } else if (strcmp(directive, "undef") == 0 ||
                       strcmp(directive, "pragma") == 0 ||
                       strcmp(directive, "error") == 0 ||
                       strcmp(directive, "warning") == 0 ||
                       strcmp(directive, "line") == 0 ||
                       strcmp(directive, "elif") == 0 ||
                       strcmp(directive, "else") == 0) {
                /* Skip these directives */
                p = line_end + 1;
                continue;
            }

            /* Unknown directive - skip the line */
            p = line_end + 1;
            continue;
        }

        /* Not a preprocessor directive - copy line as-is INCLUDING the newline */
        if (!skip_block) {
            /* Strip typedef lines - tc parser doesn't support typedef */
            const char *line_trimmed = p;
            while (line_trimmed < line_end && (*line_trimmed == ' ' || *line_trimmed == '\t')) line_trimmed++;
            if (line_trimmed + 7 <= line_end &&
                strncmp(line_trimmed, "typedef", 7) == 0) {
                /* Skip this typedef line entirely */
                p = line_end + 1;
                continue;
            }

            /* Expand macros in this line */
            size_t raw_line_len = (size_t)(line_end - p);
            char *expanded = preprocessor_expand_macros(p, raw_line_len);
            if (!expanded) {
                p = line_end + 1;
                continue;
            }

            /* Append expanded content + newline */
            size_t exp_len = strlen(expanded);
            if (out_len + exp_len + 2 >= cap) {
                cap = (out_len + exp_len + 2) * 2;
                char *new_buf = (char *)realloc(buf, cap);
                if (!new_buf) {
                    free(expanded);
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
            }
            memcpy(buf + out_len, expanded, exp_len);
            out_len += exp_len;
            free(expanded);

            /* Include the newline character if present */
            if (line_end < end && *line_end == '\n') {
                if (out_len + 1 >= cap) {
                    cap = (out_len + 1) * 2;
                    char *new_buf = (char *)realloc(buf, cap);
                    if (!new_buf) { free(buf); return NULL; }
                    buf = new_buf;
                }
                buf[out_len++] = '\n';
            }
        }

        p = line_end + 1;
    }

    buf[out_len] = '\0';
    return buf;
}


/* ==================================================================
 * Cleanup helper
 * ================================================================== */

static void cleanup(char *source, Lexer *lexer, Parser *parser,
                    SemaContext *sema, IRProgram *ir) {
    if (ir)     ir_destroy(ir);
    if (sema)   sema_destroy(sema);
    if (parser) parser_destroy(parser);
    if (lexer)  lexer_destroy(lexer);
    if (source) free(source);
}

/* ==================================================================
 * main
 * ================================================================== */

int main(int argc, char *argv[]) {
    const char *input_file = NULL;
    const char *output_file = "a.out";
    const char *dump_phase = NULL;
    int emit_asm = 0;       /* -S: assembly only */
    int compile_only = 0;   /* -c: object file */
    int verbose = 0;
    int disasm_mode = 0;  /* -d disasm: disassemble generated code */

    /* ---- Parse CLI arguments ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-S") == 0) {
            emit_asm = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            compile_only = 1;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dump_phase = argv[++i];
            if (strcmp(dump_phase, "disasm") == 0) {
                disasm_mode = 1;
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "tc: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!input_file) {
        fprintf(stderr, "tc: error: no input file\n");
        print_usage(argv[0]);
        return 1;
    }

    /* ================================================================
     *  Phase 1: Read source file
     * ================================================================ */
    size_t source_len = 0;
    char *source = read_file(input_file, &source_len);
    if (!source) {
        return 1;
    }
    if (verbose) {
        fprintf(stderr, "tc: read %zu bytes from '%s'\n", source_len, input_file);
    }

    /* ================================================================
     *  Phase 1.5: Preprocessing  (#include, #define, etc.)
     * ================================================================ */
    char *preprocessed = preprocess_source(source, source_len, input_file, verbose, 0);
    if (preprocessed) {
        free(source);
        source = preprocessed;
        source_len = strlen(source);
        if (verbose) {
            fprintf(stderr, "tc: preprocessed %zu bytes\n", source_len);
        }
        preprocessor_free_macros();
    }

    /* ================================================================
     *  Phase 2: Lexer  (source → tokens)
     * ================================================================ */
    Lexer *lexer = lexer_create(source, source_len);
    if (!lexer) {
        fprintf(stderr, "tc: lexer creation failed\n");
        cleanup(source, NULL, NULL, NULL, NULL);
        return 1;
    }
    if (verbose) fprintf(stderr, "tc: lexer done\n");

    /* ================================================================
     *  Phase 3: Parser  (tokens → AST)
     * ================================================================ */
    Parser *parser = parser_create(lexer);
    if (!parser) {
        fprintf(stderr, "tc: parser creation failed\n");
        cleanup(source, lexer, NULL, NULL, NULL);
        return 1;
    }

    Node *ast = parse_program(parser);
    if (parser->error) {
        fprintf(stderr, "tc: parse errors encountered\n");
        cleanup(source, lexer, parser, NULL, NULL);
        return 1;
    }
    if (!ast) {
        fprintf(stderr, "tc: parser returned empty AST\n");
        cleanup(source, lexer, parser, NULL, NULL);
        return 1;
    }
    if (verbose) fprintf(stderr, "tc: parse done\n");

    /* ---- Optional: dump AST ---- */
    if (dump_phase && strcmp(dump_phase, "ast") == 0) {
        node_dump(ast, 0);
        cleanup(source, lexer, parser, NULL, NULL);
        return 0;
    }

    /* ================================================================
     *  Phase 4: Semantic analysis  (type-checking, symbol resolution)
     * ================================================================ */
    SemaContext *sema = sema_create();
    if (!sema) {
        fprintf(stderr, "tc: sema context creation failed\n");
        cleanup(source, lexer, parser, NULL, NULL);
        return 1;
    }

    int sema_result = sema_analyze(sema, ast);
    if (sema_result < 0) {
        fprintf(stderr, "tc: semantic analysis failed (%d errors)\n",
                sema->error_count);
        cleanup(source, lexer, parser, sema, NULL);
        return 1;
    }
    if (verbose) fprintf(stderr, "tc: sema done\n");

    /* ---- Optional: dump symbol table ---- */
    if (dump_phase && strcmp(dump_phase, "sema") == 0) {
        dump_symbol_table(stdout, sema);
        cleanup(source, lexer, parser, sema, NULL);
        return 0;
    }

    /* ================================================================
     *  Phase 5: IR lowering  (AST → three-address code)
     * ================================================================ */
    IRProgram *ir = ir_lower(ast, sema);
    if (!ir) {
        fprintf(stderr, "tc: IR lowering failed\n");
        cleanup(source, lexer, parser, sema, NULL);
        return 1;
    }
    if (verbose) {
        fprintf(stderr, "tc: IR lowering done (%d instructions)\n", ir->count);
    }

    /* ---- Optional: dump IR (pre-optimization) ---- */
    if (dump_phase && strcmp(dump_phase, "ir") == 0) {
        ir_print(ir);
        cleanup(source, lexer, parser, sema, ir);
        return 0;
    }

    /* ================================================================
     *  Phase 6: Optimization passes
     * ================================================================ */
    opt_run_all(ir);
    if (verbose) {
        fprintf(stderr, "tc: optimization done (%d instructions)\n", ir->count);
    }

    /* ---- Optional: dump IR (post-optimization) ---- */
    if (dump_phase && strcmp(dump_phase, "opt") == 0) {
        ir_print(ir);
        cleanup(source, lexer, parser, sema, ir);
        return 0;
    }

    /* ================================================================
     *  Phase 7: Output
     * ================================================================ */

    /* -S: Emit assembly listing */
    if (emit_asm) {
        const char *asm_out = output_file;
        if (strcmp(output_file, "a.out") == 0) {
            /* Default: derive .s filename from input */
            const char *dot = strrchr(input_file, '.');
            if (dot) {
                size_t base = (size_t)(dot - input_file);
                char *sname = (char *)malloc(base + 3);
                memcpy(sname, input_file, base + 1);  /* copy including the dot */
                sname[base + 1] = 's';
                sname[base + 2] = '\0';
                asm_out = sname;
                FILE *f = fopen(asm_out, "w");
                if (f) {
                    emit_asm_program(f, ir);
                    fclose(f);
                    if (verbose)
                        fprintf(stderr, "tc: wrote assembly to '%s'\n", asm_out);
                } else {
                    fprintf(stderr, "tc: cannot open '%s' for writing\n", asm_out);
                    cleanup(source, lexer, parser, sema, ir);
                    free(sname);
                    return 1;
                }
                free(sname);
            } else {
                emit_asm_program(stdout, ir);
            }
        } else {
            FILE *f = fopen(asm_out, "w");
            if (!f) {
                fprintf(stderr, "tc: cannot open '%s' for writing\n", asm_out);
                cleanup(source, lexer, parser, sema, ir);
                return 1;
            }
            emit_asm_program(f, ir);
            fclose(f);
            if (verbose)
                fprintf(stderr, "tc: wrote assembly to '%s'\n", asm_out);
        }
        cleanup(source, lexer, parser, sema, ir);
        return 0;
    }

    /* Default: Generate ELF executable (or object file with -c) */
    CodeGen *cg = cg_create();
    if (!cg) {
        fprintf(stderr, "tc: codegen creation failed\n");
        cleanup(source, lexer, parser, sema, ir);
        return 1;
    }

    cg->object_mode = 1;  /* Always generate object file first */

    char obj_path[512];
    if (compile_only) {
        snprintf(obj_path, sizeof(obj_path), "%s", output_file);
    } else {
        /* Generate object to a temp file, then link to final output */
        snprintf(obj_path, sizeof(obj_path), "%s.o.tmp", output_file);
    }

    int cg_result = cg_generate(cg, ir, obj_path);
    if (cg_result != 0) {
        fprintf(stderr, "tc: code generation failed\n");
        cg_destroy(cg);
        cleanup(source, lexer, parser, sema, ir);
        return 1;
    }

    if (verbose) fprintf(stderr, "tc: codegen done, wrote '%s'\n", obj_path);

    /* ================================================================
     *  Link phase (only when not compile-only)
     * ================================================================ */
    if (!compile_only) {
        /* Use our linker to link the object file with the runtime library */
        Linker *linker = linker_create();
        if (!linker) {
            fprintf(stderr, "tc: linker creation failed\n");
            cg_destroy(cg);
            cleanup(source, lexer, parser, sema, ir);
            return 1;
        }

        /* Add the object file we just generated */
        if (linker_add_object(linker, obj_path) != 0) {
            fprintf(stderr, "tc: linker error: %s\n", linker_error(linker));
            linker_destroy(linker);
            cg_destroy(cg);
            cleanup(source, lexer, parser, sema, ir);
            return 1;
        }

        /* Add the runtime library */
        /* Try to find libruntime.a */
        int runtime_linked = 0;
        char runtime_path[512];
        
        /* Get directory of the tc executable from argv[0] */
        char tc_dir[512] = ".";
        const char *slash = strrchr(argv[0], '/');
        if (slash != NULL) {
            size_t dir_len = (size_t)(slash - argv[0]);
            if (dir_len < sizeof(tc_dir)) {
                memcpy(tc_dir, argv[0], dir_len);
                tc_dir[dir_len] = '\0';
            }
        }
        fprintf(stderr, "tc: debug tc_dir='%s'\n", tc_dir);
        
        /* Try multiple paths for libruntime.a */
        const char *runtime_suffixes[] = {
            "lib/libruntime.a",
            "../lib/libruntime.a",
            "../../lib/libruntime.a",
            NULL
        };
        for (int i = 0; runtime_suffixes[i] != NULL; i++) {
            snprintf(runtime_path, sizeof(runtime_path), "%s/%s", 
                     (strlen(tc_dir) == 0) ? "." : tc_dir, runtime_suffixes[i]);
            fprintf(stderr, "tc: debug trying runtime path '%s'\n", runtime_path);
            if (linker_add_archive(linker, runtime_path) == 0) {
                runtime_linked = 1;
                break;
            }
            fprintf(stderr, "tc: debug failed: %s\n", linker_error(linker));
        }
        
        /* Also try absolute/common paths */
        if (!runtime_linked) {
            const char *common_paths[] = {
                "lib/libruntime.a",
                "./lib/libruntime.a",
                NULL
            };
            for (int i = 0; common_paths[i] != NULL; i++) {
                fprintf(stderr, "tc: debug trying common path '%s'\n", common_paths[i]);
                if (linker_add_archive(linker, common_paths[i]) == 0) {
                    runtime_linked = 1;
                    break;
                }
                fprintf(stderr, "tc: debug failed: %s\n", linker_error(linker));
            }
        }

        if (!runtime_linked) {
            fprintf(stderr, "tc: warning: could not find libruntime.a, "
                    "external symbols may be unresolved\n");
        }

        /* Perform the link */
        if (linker_link(linker, output_file) != 0) {
            fprintf(stderr, "tc: linker error: %s\n", linker_error(linker));
            linker_destroy(linker);
            cg_destroy(cg);
            cleanup(source, lexer, parser, sema, ir);
            return 1;
        }

        linker_destroy(linker);
    }

    /* ================================================================
     *  Disassembly output (if -d disasm)
     * ================================================================ */
    if (disasm_mode) {
        fprintf(stderr, "=== Disassembly of .text section (%zu bytes) ===\n", cg->code_size);
        disasm_x86_64(cg->code, cg->code_size, ELF_TEXT_BASE, stderr);
        fprintf(stderr, "=== End of disassembly ===\n");
    }

    /* ================================================================
     *  Cleanup
     * ================================================================ */
    cg_destroy(cg);
    cleanup(source, lexer, parser, sema, ir);

    if (verbose) fprintf(stderr, "tc: compilation successful\n");
    return 0;
}
