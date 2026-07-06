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
#include "x86_64.h"

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
    fprintf(stderr, "  -d <phase>   Dump after phase (ast, sema, ir, opt)\n");
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
    int verbose = 0;

    /* ---- Parse CLI arguments ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-S") == 0) {
            emit_asm = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            /* -c: compile only (ELF object) — same as normal for now */
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dump_phase = argv[++i];
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

    /* Default: Generate ELF executable */
    CodeGen *cg = cg_create();
    if (!cg) {
        fprintf(stderr, "tc: codegen creation failed\n");
        cleanup(source, lexer, parser, sema, ir);
        return 1;
    }

    int cg_result = cg_generate(cg, ir, output_file);
    if (cg_result != 0) {
        fprintf(stderr, "tc: code generation failed\n");
        cg_destroy(cg);
        cleanup(source, lexer, parser, sema, ir);
        return 1;
    }
    if (verbose) fprintf(stderr, "tc: codegen done, wrote '%s'\n", output_file);

    /* ================================================================
     *  Cleanup
     * ================================================================ */
    cg_destroy(cg);
    cleanup(source, lexer, parser, sema, ir);

    if (verbose) fprintf(stderr, "tc: compilation successful\n");
    return 0;
}
