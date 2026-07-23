/*
 * tc/src/ir.c  --  Three-address code IR with named temporaries.
 *
 * This is the bridge between the AST (high-level) and x86-64 codegen
 * (low-level).  The IR is a flat list of instructions; each instruction
 * operates on named temporaries (sequential integers starting at 0).
 */

#define _POSIX_C_SOURCE 200809L

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* ==================================================================
 *  IRProgram helpers
 * ================================================================== */

IRProgram *ir_create(void)
{
    IRProgram *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->cap = 256;
    p->instrs = calloc((size_t)p->cap, sizeof(IRInstr));
    return p;
}

void ir_track_label(IRProgram *prog, const char *label)
{
    if (!prog || !label) return;
    if (prog->owned_label_count >= prog->owned_label_cap) {
        int new_cap = prog->owned_label_cap == 0 ? 32 : prog->owned_label_cap * 2;
        prog->owned_labels = realloc(prog->owned_labels, (size_t)new_cap * sizeof(char *));
        prog->owned_label_cap = new_cap;
    }
    prog->owned_labels[prog->owned_label_count++] = (char *)label;
}

void ir_destroy(IRProgram *prog)
{
    if (!prog) return;
    /* Only free labels we explicitly tracked as owned */
    for (int i = 0; i < prog->owned_label_count; i++) {
        free(prog->owned_labels[i]);
    }
    free(prog->owned_labels);
    /* Free arg_ids for each instruction */
    for (int i = 0; i < prog->count; i++) {
        free(prog->instrs[i].arg_ids);
    }
    free(prog->instrs);
    free(prog);
}

static int ir_grow(IRProgram *prog)
{
    if (prog->count >= prog->cap) {
        prog->cap *= 2;
        prog->instrs = realloc(prog->instrs,
                               (size_t)prog->cap * sizeof(IRInstr));
    }
    int idx = prog->count++;
    memset(&prog->instrs[idx], 0, sizeof(IRInstr));
    return idx;
}

int ir_emit(IRProgram *prog, IRKind kind, int dest, int src1, int src2)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = kind;
    i->temp_id = dest;
    i->src_id = src1;
    i->src2_id = src2;
    return dest;
}

static char *safe_strdup(const char *s)
{
    return s ? strdup(s) : NULL;
}

int ir_emit_const(IRProgram *prog, int64_t value)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_CONST;
    i->ival = value;
    i->temp_id = idx;
    i->type_size = 4;
    return i->temp_id;
}

int ir_emit_label(IRProgram *prog, const char *label)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_LABEL;
    i->label = safe_strdup(label);
    ir_track_label(prog, i->label);
    return idx;
}

int ir_emit_call(IRProgram *prog, int dest, const char *name,
                 int nargs, int *args)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_CALL;
    i->label = safe_strdup(name);
    ir_track_label(prog, i->label);
    i->temp_id = dest;
    i->arg_count = nargs;
    if (nargs > 0 && args) {
        i->arg_ids = malloc((size_t)nargs * sizeof(int));
        memcpy(i->arg_ids, args, (size_t)nargs * sizeof(int));
    }
    return i->temp_id;
}

int ir_emit_alloc(IRProgram *prog, int size)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_ALLOC;
    i->ival = (int64_t)size;
    return idx;
}

int ir_emit_global_str(IRProgram *prog, const char *name)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_GLOBAL_STR;
    i->label = safe_strdup(name);
    ir_track_label(prog, i->label);
    return idx;
}

int ir_emit_addr_global(IRProgram *prog, const char *name)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_ADDR_GLOBAL;
    i->label = safe_strdup(name);
    ir_track_label(prog, i->label);
    i->temp_id = idx;
    return i->temp_id;
}

int ir_emit_addr_local(IRProgram *prog, int offset)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_ADDR_LOCAL;
    i->ival = (int64_t)offset;
    i->temp_id = idx;
    return i->temp_id;
}

int ir_emit_addr_param(IRProgram *prog, int param_n)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_ADDR_PARAM;
    i->ival = (int64_t)param_n;
    i->temp_id = idx;
    return i->temp_id;
}

int ir_emit_load(IRProgram *prog, int src, int type_size, int is_signed)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_LOAD;
    i->src_id = src;
    i->temp_id = idx;
    i->type_size = type_size;
    i->is_signed = is_signed;
    return i->temp_id;
}

int ir_emit_store(IRProgram *prog, int addr, int value, int type_size, int is_signed)
{
    int idx = ir_grow(prog);
    IRInstr *i = &prog->instrs[idx];
    i->kind = IR_STORE;
    i->src_id = addr;
    i->src2_id = value;
    i->type_size = type_size;
    i->is_signed = is_signed;
    return idx;
}

/* ==================================================================
 *  Debug printer
 * ================================================================== */

static const char *ir_kind_name(IRKind k)
{
    switch (k) {
#define X(k) case k: return #k
        X(IR_NOP);       X(IR_MOV);       X(IR_CONST);
        X(IR_ADDR_GLOBAL); X(IR_ADDR_LOCAL); X(IR_ADDR_PARAM);
        X(IR_LOAD);      X(IR_STORE);
        X(IR_ADD);       X(IR_SUB);       X(IR_MUL);
        X(IR_DIV);       X(IR_MOD);
        X(IR_BAND);      X(IR_BOR);       X(IR_XOR);
        X(IR_NEG);       X(IR_NOT);       X(IR_BITNOT);
        X(IR_SHL);       X(IR_SHR);
        X(IR_CMP_EQ);    X(IR_CMP_NEQ);   X(IR_CMP_LT);
        X(IR_CMP_GT);    X(IR_CMP_LEQ);   X(IR_CMP_GEQ);
        X(IR_BR);        X(IR_BR_IF);     X(IR_BR_IF_NOT);
        X(IR_LABEL);
        X(IR_CALL);      X(IR_CALL_IND);  X(IR_PARAM);
        X(IR_RET);       X(IR_ALLOC);     X(IR_ARG);
        X(IR_GLOBAL_STR); X(IR_GLOBAL_DATA);
        X(IR_SYSCALL);
#undef X
        default: return "IR_UNKNOWN";
    }
}

void ir_print(const IRProgram *prog)
{
    if (!prog) {
        printf("(null IR)\n");
        return;
    }
    for (int i = 0; i < prog->count; i++) {
        const IRInstr *inst = &prog->instrs[i];
        printf("  [%03d] %-16s", i, ir_kind_name(inst->kind));
        switch (inst->kind) {
        case IR_CONST:
            printf("t%d = %lld\n", inst->temp_id, (long long)inst->ival);
            break;
        case IR_ADDR_GLOBAL:
            printf("t%d = &%s\n", inst->temp_id, inst->label);
            break;
        case IR_ADDR_LOCAL:
            printf("t%d = &local[%lld]\n", inst->temp_id, (long long)inst->ival);
            break;
        case IR_ADDR_PARAM:
            printf("t%d = &param[%lld]\n", inst->temp_id, (long long)inst->ival);
            break;
        case IR_LOAD:
            printf("t%d = *(%s%d)t%d\n",
                   inst->temp_id,
                   inst->is_signed ? "" : "u",
                   inst->type_size,
                   inst->src_id);
            break;
        case IR_STORE:
            printf("*(%s%d)t%d = t%d\n",
                   inst->is_signed ? "" : "u",
                   inst->type_size,
                   inst->src_id,
                   inst->src2_id);
            break;
        case IR_MOV:
            printf("t%d = t%d\n", inst->temp_id, inst->src_id);
            break;
        case IR_ADD: case IR_SUB: case IR_MUL:
        case IR_DIV: case IR_MOD:
        case IR_BAND: case IR_BOR: case IR_XOR:
        case IR_SHL: case IR_SHR:
            printf("t%d = t%d %s t%d\n",
                   inst->temp_id, inst->src_id,
                   ir_kind_name(inst->kind) + 3,
                   inst->src2_id);
            break;
        case IR_NEG:
            printf("t%d = -t%d\n", inst->temp_id, inst->src_id);
            break;
        case IR_NOT:
            printf("t%d = !t%d\n", inst->temp_id, inst->src_id);
            break;
        case IR_BITNOT:
            printf("t%d = ~t%d\n", inst->temp_id, inst->src_id);
            break;
        case IR_CMP_EQ: case IR_CMP_NEQ:
        case IR_CMP_LT: case IR_CMP_GT:
        case IR_CMP_LEQ: case IR_CMP_GEQ:
            printf("t%d = t%d %s t%d\n",
                   inst->temp_id, inst->src_id,
                   ir_kind_name(inst->kind) + 3,
                   inst->src2_id);
            break;
        case IR_BR:
            printf("goto %s\n", inst->label);
            break;
        case IR_BR_IF:
            printf("if (t%d != 0) goto %s\n", inst->src_id, inst->label);
            break;
        case IR_BR_IF_NOT:
            printf("if (t%d == 0) goto %s\n", inst->src_id, inst->label);
            break;
        case IR_LABEL:
            printf("%s:\n", inst->label);
            break;
        case IR_CALL:
            printf("t%d = call %s(", inst->temp_id, inst->label);
            for (int a = 0; a < inst->arg_count; a++) {
                if (a) printf(", ");
                printf("t%d", inst->arg_ids[a]);
            }
            printf(")\n");
            break;
        case IR_CALL_IND:
            printf("t%d = call_indirect t%d(", inst->temp_id, inst->src_id);
            for (int a = 0; a < inst->arg_count; a++) {
                if (a) printf(", ");
                printf("t%d", inst->arg_ids[a]);
            }
            printf(")\n");
            break;
        case IR_RET:
            if (inst->src_id >= 0)
                printf("return t%d\n", inst->src_id);
            else
                printf("return\n");
            break;
        case IR_ALLOC:
            printf("alloc %lld bytes\n", (long long)inst->ival);
            break;
        case IR_PARAM:
            printf("param %lld\n", (long long)inst->ival);
            break;
        case IR_GLOBAL_STR:
            printf("global_str %s\n", inst->label);
            break;
        case IR_GLOBAL_DATA:
            printf("global_data %lld bytes\n", (long long)inst->ival);
            break;
        case IR_SYSCALL:
            printf("syscall num=%lld args=t%d t%d\n",
                   (long long)inst->ival, inst->src_id, inst->src2_id);
            break;
        case IR_ARG:
            printf("arg t%d\n", inst->src_id);
            break;
        case IR_NOP:
            printf("nop\n");
            break;
        default:
            printf("(unknown kind %d)\n", (int)inst->kind);
            break;
        }
    }
}

/* ==================================================================
 *  AST -> IR lowering
 * ================================================================== */

/* ---- internal helpers ---- */

static int ir_gen_new_temp(IRGen *gen)
{
    return gen->temp_counter++;
}

static char *ir_gen_new_label(IRGen *gen, const char *prefix)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s_%d", prefix, gen->label_counter++);
    return strdup(buf);
}

static IRKind ir_binop_kind(BinaryOp op)
{
    switch (op) {
        case BIN_ADD:    return IR_ADD;
        case BIN_SUB:    return IR_SUB;
        case BIN_MUL:    return IR_MUL;
        case BIN_DIV:    return IR_DIV;
        case BIN_MOD:    return IR_MOD;
        case BIN_BAND:   return IR_BAND;
        case BIN_BXOR:   return IR_XOR;
        case BIN_BOR:    return IR_BOR;
        case BIN_LSHL:   return IR_SHL;
        case BIN_LSHR:   return IR_SHR;
        case BIN_EQ:     return IR_CMP_EQ;
        case BIN_NEQ:    return IR_CMP_NEQ;
        case BIN_LT:     return IR_CMP_LT;
        case BIN_GT:     return IR_CMP_GT;
        case BIN_LEQ:    return IR_CMP_LEQ;
        case BIN_GEQ:    return IR_CMP_GEQ;
        default:         return IR_NOP;
    }
}

static IRKind ir_unop_kind(UnaryOp op)
{
    switch (op) {
        case U_NEG:    return IR_NEG;
        case U_NOT:    return IR_NOT;
        case U_BITNOT: return IR_BITNOT;
        default:       return IR_NOP;
    }
}

static int ir_type_size(SemaContext *sema, int type_id)
{
    if (type_id < 0) return 4;
    Type *t = sema_get_type(sema, type_id);
    if (!t) return 4;
    if (t->size > 0) return t->size;
    switch (t->kind) {
        case TY_VOID:   return 0;
        case TY_CHAR:   return 1;
        case TY_SHORT:  case TY_USHORT:  return 2;
        case TY_INT:    case TY_UINT:    return 4;
        case TY_LONG:   case TY_ULONG:   return 8;
        case TY_PTR:    return 8;
        default:        return 4;
    }
}

static int ir_type_signed(SemaContext *sema, int type_id)
{
    if (type_id < 0) return 1;
    Type *t = sema_get_type(sema, type_id);
    if (!t) return 1;
    return t->sign == 0;
}

/* ---- recursive lowering ---- */

static int ir_lower_expr(IRGen *gen, Node *node);
static void ir_lower_stmt(IRGen *gen, Node *node);

static void ir_lower_block(IRGen *gen, Node *node)
{
    if (!node) return;
    if (node->kind == NODE_BLOCK) {
        for (int i = 0; i < node->u.block.nstmts; i++) {
            ir_lower_stmt(gen, node->u.block.stmts[i]);
        }
    } else {
        ir_lower_stmt(gen, node);
    }
}

/* Helper: directly search the global scope for a symbol (bypasses scope chain) */
static Symbol *sema_lookup_global(SemaContext *sema, uint32_t hash, const char *name)
{
    if (!sema || !name) return NULL;
    Scope *gs = &sema->global_scope;
    int idx = (int)(hash % 256);
    for (int i = 0; i < 256; i++) {
        int check = (idx + i) % 256;
        Symbol *s = gs->symbols[check];
        if (s) {
            if (s->hash == hash && strcmp(s->name, name) == 0) {
                return s;
            }
        } else {
            break;
        }
    }
    return NULL;
}

/* Helper: get the address temp for an lvalue identifier */
static int ir_lower_lvalue_addr(IRGen *gen, Node *node)
{
    if (!node || node->kind != NODE_IDENT) return -1;

    const char *name = node->u.ident.name;
    if (!name) return -1;

    /* First check global scope explicitly (bypasses broken scope chain) */
    Symbol *global_sym = sema_lookup_global(gen->sema, node->u.ident.hash, name);
    if (global_sym && global_sym->scope_kind == SCOPE_GLOBAL) {
        return ir_emit_addr_global(gen->prog, name);
    }

    Symbol *sym = sema_lookup(gen->sema, node->u.ident.hash, name);
    if (sym && sym->kind == SYM_PARAM) {
        return ir_emit_addr_param(gen->prog, (int)(intptr_t)sym->data);
    } else {
        int off = 0;
        if (sym && sym->data) off = (int)(intptr_t)sym->data;
        return ir_emit_addr_local(gen->prog, off);
    }
}

/* Helper: load value from an identifier */
static int ir_lower_ident_load(IRGen *gen, Node *node)
{
    if (!node || node->kind != NODE_IDENT) return -1;
    const char *name = node->u.ident.name;
    if (!name) return -1;

    /* First check global scope explicitly (bypasses broken scope chain) */
    Symbol *global_sym = sema_lookup_global(gen->sema, node->u.ident.hash, name);

    int addr_t;
    if (global_sym && global_sym->scope_kind == SCOPE_GLOBAL) {
        /* Global variable found - use it */
        addr_t = ir_emit_addr_global(gen->prog, name);
    } else {
        /* Fall back to regular lookup for locals/params */
        Symbol *sym = sema_lookup(gen->sema, node->u.ident.hash, name);
        if (sym && sym->kind == SYM_PARAM) {
            addr_t = ir_emit_addr_param(gen->prog, (int)(intptr_t)sym->data);
        } else {
            int off = gen->frame_offset;
            if (sym) off = (int)(intptr_t)sym->data;
            addr_t = ir_emit_addr_local(gen->prog, off);
        }
    }

    int t = ir_gen_new_temp(gen);
    int idx = ir_grow(gen->prog);
    IRInstr *i = &gen->prog->instrs[idx];
    i->kind = IR_LOAD;
    i->src_id = addr_t;
    i->temp_id = t;
    i->type_size = ir_type_size(gen->sema, node->type_id);
    i->is_signed = ir_type_signed(gen->sema, node->type_id);
    return t;
}

/* ---- expression lowering ---- */

static int ir_lower_expr(IRGen *gen, Node *node)
{
    if (!node) return -1;

    switch (node->kind) {

    case NODE_INT_LIT: {
        int t = ir_gen_new_temp(gen);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_CONST;
        i->ival = node->const_val;
        i->temp_id = t;
        i->type_size = ir_type_size(gen->sema, node->type_id);
        return t;
    }

    case NODE_CHAR_LIT: {
        int t = ir_gen_new_temp(gen);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_CONST;
        i->ival = node->const_val;
        i->temp_id = t;
        i->type_size = 1;
        return t;
    }

    case NODE_STR_LIT: {
        char *label = ir_gen_new_label(gen, "str");
        ir_track_label(gen->prog, label);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_GLOBAL_STR;
        i->label = label;  // Use generated label name as the symbol name
        i->ival = (int64_t)(intptr_t)safe_strdup(node->u.str_lit.str);  // Store string content in ival

        int t = ir_gen_new_temp(gen);
        int idx2 = ir_grow(gen->prog);
        IRInstr *i2 = &gen->prog->instrs[idx2];
        i2->kind = IR_ADDR_GLOBAL;
        i2->label = safe_strdup(label);
        i2->temp_id = t;
        i2->type_size = 8;
        return t;
    }

    case NODE_IDENT:
        return ir_lower_ident_load(gen, node);

    case NODE_BINARY: {
        BinaryOp op = node->u.binary.op;

        /* Logical AND / OR with short-circuit */
        if (op == BIN_ANDAND || op == BIN_OROR) {
            char *label_false = ir_gen_new_label(gen, "andor");
            ir_track_label(gen->prog, label_false);
            char *label_end = ir_gen_new_label(gen, "andor");
            ir_track_label(gen->prog, label_end);

            int left_val = ir_lower_expr(gen, node->u.binary.left);

            if (op == BIN_ANDAND) {
                int idx = ir_grow(gen->prog);
                IRInstr *i = &gen->prog->instrs[idx];
                i->kind = IR_BR_IF_NOT;
                i->src_id = left_val;
                i->label = label_false;

                int right_val = ir_lower_expr(gen, node->u.binary.right);

                int idx2 = ir_grow(gen->prog);
                IRInstr *i2 = &gen->prog->instrs[idx2];
                i2->kind = IR_BR;
                i2->label = label_end;

                int idx3 = ir_grow(gen->prog);
                IRInstr *i3 = &gen->prog->instrs[idx3];
                i3->kind = IR_LABEL;
                i3->label = label_false;

                int t0 = ir_gen_new_temp(gen);
                int idx4 = ir_grow(gen->prog);
                IRInstr *i4 = &gen->prog->instrs[idx4];
                i4->kind = IR_CONST;
                i4->ival = 0;
                i4->temp_id = t0;

                i->temp_id = t0;

                int idx5 = ir_grow(gen->prog);
                IRInstr *i5 = &gen->prog->instrs[idx5];
                i5->kind = IR_LABEL;
                i5->label = label_end;

                i2->temp_id = right_val;
                return right_val;
            } else {
                /* BIN_OROR */
                int idx = ir_grow(gen->prog);
                IRInstr *i = &gen->prog->instrs[idx];
                i->kind = IR_BR_IF;
                i->src_id = left_val;
                i->label = label_end;

                int right_val = ir_lower_expr(gen, node->u.binary.right);

                int idx2 = ir_grow(gen->prog);
                IRInstr *i2 = &gen->prog->instrs[idx2];
                i2->kind = IR_BR;
                i2->label = label_false;

                int idx3 = ir_grow(gen->prog);
                IRInstr *i3 = &gen->prog->instrs[idx3];
                i3->kind = IR_LABEL;
                i3->label = label_end;

                int t1 = ir_gen_new_temp(gen);
                int idx4 = ir_grow(gen->prog);
                IRInstr *i4 = &gen->prog->instrs[idx4];
                i4->kind = IR_CONST;
                i4->ival = 1;
                i4->temp_id = t1;

                int idx5 = ir_grow(gen->prog);
                IRInstr *i5 = &gen->prog->instrs[idx5];
                i5->kind = IR_LABEL;
                i5->label = label_false;

                return right_val;
            }
        }

        /* Compound assignments: x += y => x = x + y */
        if (op >= BIN_PLUSEQ && op <= BIN_LSHREQ) {
            Node *left = node->u.binary.left;
            Node *right = node->u.binary.right;

            BinaryOp real_op = BIN_ADD;
            switch (op) {
                case BIN_PLUSEQ:   real_op = BIN_ADD; break;
                case BIN_MINUSEQ:  real_op = BIN_SUB; break;
                case BIN_STAREQ:   real_op = BIN_MUL; break;
                case BIN_SLASHEQ:  real_op = BIN_DIV; break;
                case BIN_PERCENTEQ: real_op = BIN_MOD; break;
                case BIN_AMPREQ:   real_op = BIN_BAND; break;
                case BIN_PIPEEQ:   real_op = BIN_BOR; break;
                case BIN_CARETEQ:  real_op = BIN_BXOR; break;
                case BIN_LSHLEQ:   real_op = BIN_LSHL; break;
                case BIN_LSHREQ:   real_op = BIN_LSHR; break;
                default: break;
            }

            int lv = ir_lower_expr(gen, left);
            int rv = ir_lower_expr(gen, right);

            int t = ir_gen_new_temp(gen);
            int idx = ir_grow(gen->prog);
            IRInstr *i = &gen->prog->instrs[idx];
            i->kind = ir_binop_kind(real_op);
            i->src_id = lv;
            i->src2_id = rv;
            i->temp_id = t;
            i->type_size = ir_type_size(gen->sema, left->type_id);

            /* Store back to lvalue address */
            if (left->kind == NODE_IDENT) {
                int addr_t = ir_lower_lvalue_addr(gen, left);
                if (addr_t >= 0) {
                    ir_emit_store(gen->prog, addr_t, t,
                                  ir_type_size(gen->sema, left->type_id),
                                  ir_type_signed(gen->sema, left->type_id));
                }
            }
            return t;
        }

        /* Regular binary op */
        int lv = ir_lower_expr(gen, node->u.binary.left);
        int rv = ir_lower_expr(gen, node->u.binary.right);

        int t = ir_gen_new_temp(gen);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = ir_binop_kind(op);
        i->src_id = lv;
        i->src2_id = rv;
        i->temp_id = t;
        i->type_size = ir_type_size(gen->sema, node->type_id);
        i->is_signed = ir_type_signed(gen->sema, node->type_id);
        return t;
    }

    case NODE_UNARY: {
        UnaryOp op = node->u.unary.op;

        /* Handle ++ and -- */
        if (op == U_PREINC || op == U_POSTINC || op == U_PREDEC || op == U_POSTDEC) {
            Node *operand = node->u.unary.operand;
            int is_pre = (op == U_PREINC || op == U_PREDEC);

            int val = ir_lower_expr(gen, operand);

            int one = ir_emit_const(gen->prog, 1);

            int t = ir_gen_new_temp(gen);
            int idx = ir_grow(gen->prog);
            IRInstr *i = &gen->prog->instrs[idx];
            i->kind = IR_ADD;
            i->src_id = val;
            i->src2_id = one;
            i->temp_id = t;
            i->type_size = ir_type_size(gen->sema, node->type_id);

            if (op == U_PREDEC || op == U_POSTDEC) {
                int neg_one = ir_emit_const(gen->prog, -1);
                i->src2_id = neg_one;
            }

            /* Store back */
            if (operand->kind == NODE_IDENT) {
                int addr_t = ir_lower_lvalue_addr(gen, operand);
                if (addr_t >= 0) {
                    ir_emit_store(gen->prog, addr_t, t,
                                  ir_type_size(gen->sema, operand->type_id),
                                  ir_type_signed(gen->sema, operand->type_id));
                }
            }

            return is_pre ? t : val;
        }

        int operand_val = ir_lower_expr(gen, node->u.unary.operand);
        int t = ir_gen_new_temp(gen);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = ir_unop_kind(op);
        i->src_id = operand_val;
        i->temp_id = t;
        i->type_size = ir_type_size(gen->sema, node->type_id);
        return t;
    }

    case NODE_ASSIGN: {
        Node *lval_node = node->u.assign.lvalue;
        Node *rhs_node = node->u.assign.rhs;
        BinaryOp assign_op = node->u.assign.assign_op;

        if (assign_op != BIN_NONE && assign_op != BIN_ASSIGN) {
            BinaryOp real_op = BIN_ADD;
            switch (assign_op) {
                case BIN_PLUSEQ:   real_op = BIN_ADD; break;
                case BIN_MINUSEQ:  real_op = BIN_SUB; break;
                case BIN_STAREQ:   real_op = BIN_MUL; break;
                case BIN_SLASHEQ:  real_op = BIN_DIV; break;
                case BIN_PERCENTEQ: real_op = BIN_MOD; break;
                case BIN_AMPREQ:   real_op = BIN_BAND; break;
                case BIN_PIPEEQ:   real_op = BIN_BOR; break;
                case BIN_CARETEQ:  real_op = BIN_BXOR; break;
                case BIN_LSHLEQ:   real_op = BIN_LSHL; break;
                case BIN_LSHREQ:   real_op = BIN_LSHR; break;
                default: break;
            }

            int lv = ir_lower_expr(gen, lval_node);
            int rv = ir_lower_expr(gen, rhs_node);

            int t = ir_gen_new_temp(gen);
            int idx = ir_grow(gen->prog);
            IRInstr *i = &gen->prog->instrs[idx];
            i->kind = ir_binop_kind(real_op);
            i->src_id = lv;
            i->src2_id = rv;
            i->temp_id = t;
            i->type_size = ir_type_size(gen->sema, lval_node->type_id);

            /* Store result back to lvalue */
            int addr_t = -1;
            if (lval_node->kind == NODE_IDENT) {
                addr_t = ir_lower_lvalue_addr(gen, lval_node);
            } else if (lval_node->kind == NODE_INDEX) {
                int arr_addr = ir_lower_expr(gen, lval_node->u.index.array);
                int idx_val = ir_lower_expr(gen, lval_node->u.index.index);
                int elem_size = ir_type_size(gen->sema, lval_node->type_id);
                int scaled = ir_emit_const(gen->prog, (int64_t)elem_size);
                int off = ir_gen_new_temp(gen);
                int iidx = ir_grow(gen->prog);
                IRInstr *ii = &gen->prog->instrs[iidx];
                ii->kind = IR_MUL;
                ii->src_id = idx_val;
                ii->src2_id = scaled;
                ii->temp_id = off;
                ii->type_size = 4;
                addr_t = ir_gen_new_temp(gen);
                int aidx = ir_grow(gen->prog);
                IRInstr *ai = &gen->prog->instrs[aidx];
                ai->kind = IR_ADD;
                ai->src_id = arr_addr;
                ai->src2_id = off;
                ai->temp_id = addr_t;
                ai->type_size = 8;
            } else if (lval_node->kind == NODE_DEREF) {
                addr_t = ir_lower_expr(gen, lval_node->u.unary.operand);
            }
            if (addr_t >= 0) {
                ir_emit_store(gen->prog, addr_t, t,
                              ir_type_size(gen->sema, lval_node->type_id),
                              ir_type_signed(gen->sema, lval_node->type_id));
            }
            return t;
        }

        int rhs_val = ir_lower_expr(gen, rhs_node);

        int addr_t = -1;
        if (lval_node->kind == NODE_IDENT) {
            addr_t = ir_lower_lvalue_addr(gen, lval_node);
        } else if (lval_node->kind == NODE_INDEX) {
            int arr_addr = ir_lower_expr(gen, lval_node->u.index.array);
            int idx_val = ir_lower_expr(gen, lval_node->u.index.index);
            int elem_size = ir_type_size(gen->sema, lval_node->type_id);

            int scaled = ir_emit_const(gen->prog, (int64_t)elem_size);
            int off = ir_gen_new_temp(gen);
            int iidx = ir_grow(gen->prog);
            IRInstr *ii = &gen->prog->instrs[iidx];
            ii->kind = IR_MUL;
            ii->src_id = idx_val;
            ii->src2_id = scaled;
            ii->temp_id = off;
            ii->type_size = 4;

            addr_t = ir_gen_new_temp(gen);
            int aidx = ir_grow(gen->prog);
            IRInstr *ai = &gen->prog->instrs[aidx];
            ai->kind = IR_ADD;
            ai->src_id = arr_addr;
            ai->src2_id = off;
            ai->temp_id = addr_t;
            ai->type_size = 8;
        } else if (lval_node->kind == NODE_DEREF) {
            addr_t = ir_lower_expr(gen, lval_node->u.unary.operand);
        }

        if (addr_t >= 0) {
            ir_emit_store(gen->prog, addr_t, rhs_val,
                          ir_type_size(gen->sema, lval_node->type_id),
                          ir_type_signed(gen->sema, lval_node->type_id));
        }
        return rhs_val;
    }

    case NODE_INDEX: {
        int arr_addr = ir_lower_expr(gen, node->u.index.array);
        int idx_val = ir_lower_expr(gen, node->u.index.index);
        int elem_size = ir_type_size(gen->sema, node->type_id);

        int scaled_const = ir_emit_const(gen->prog, (int64_t)elem_size);
        int off = ir_gen_new_temp(gen);
        int iidx = ir_grow(gen->prog);
        IRInstr *ii = &gen->prog->instrs[iidx];
        ii->kind = IR_MUL;
        ii->src_id = idx_val;
        ii->src2_id = scaled_const;
        ii->temp_id = off;
        ii->type_size = 4;

        int ptr = ir_gen_new_temp(gen);
        int aidx = ir_grow(gen->prog);
        IRInstr *ai = &gen->prog->instrs[aidx];
        ai->kind = IR_ADD;
        ai->src_id = arr_addr;
        ai->src2_id = off;
        ai->temp_id = ptr;
        ai->type_size = 8;

        int t = ir_gen_new_temp(gen);
        int lidx = ir_grow(gen->prog);
        IRInstr *li = &gen->prog->instrs[lidx];
        li->kind = IR_LOAD;
        li->src_id = ptr;
        li->temp_id = t;
        li->type_size = ir_type_size(gen->sema, node->type_id);
        li->is_signed = ir_type_signed(gen->sema, node->type_id);
        return t;
    }

    case NODE_ADDR_OF: {
        Node *operand = node->u.unary.operand;
        if (operand->kind == NODE_IDENT) {
            return ir_lower_lvalue_addr(gen, operand);
        }
        int val = ir_lower_expr(gen, operand);
        int t = ir_gen_new_temp(gen);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_ADDR_LOCAL;
        i->ival = (int64_t)gen->frame_offset;
        i->temp_id = t;
        gen->frame_offset -= ir_type_size(gen->sema, operand->type_id);

        ir_emit_store(gen->prog, t, val,
                      ir_type_size(gen->sema, operand->type_id),
                      ir_type_signed(gen->sema, operand->type_id));
        return t;
    }

    case NODE_DEREF: {
        int ptr = ir_lower_expr(gen, node->u.unary.operand);
        int t = ir_gen_new_temp(gen);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_LOAD;
        i->src_id = ptr;
        i->temp_id = t;
        i->type_size = ir_type_size(gen->sema, node->type_id);
        i->is_signed = ir_type_signed(gen->sema, node->type_id);
        return t;
    }

    case NODE_CALL: {
        const char *func_name = NULL;
        int callee_addr = -1;

        if (node->u.call.callee->kind == NODE_IDENT) {
            func_name = node->u.call.callee->u.ident.name;
        } else {
            callee_addr = ir_lower_expr(gen, node->u.call.callee);
        }

        int nargs = node->u.call.nargs;
        int *arg_ids = NULL;
        if (nargs > 0) {
            arg_ids = malloc((size_t)nargs * sizeof(int));
            for (int a = 0; a < nargs; a++) {
                arg_ids[a] = ir_lower_expr(gen, node->u.call.args[a]);
            }
        }

        int t = ir_gen_new_temp(gen);

        if (func_name) {
            ir_emit_call(gen->prog, t, func_name, nargs, arg_ids);
        } else {
            int idx = ir_grow(gen->prog);
            IRInstr *i = &gen->prog->instrs[idx];
            i->kind = IR_CALL_IND;
            i->src_id = callee_addr;
            i->temp_id = t;
            i->arg_count = nargs;
            if (nargs > 0 && arg_ids) {
                i->arg_ids = arg_ids;
                arg_ids = NULL;
            }
        }
        free(arg_ids);
        return t;
    }

    case NODE_TERNARY: {
        char *label_else = ir_gen_new_label(gen, "tern");
        ir_track_label(gen->prog, label_else);
        char *label_end = ir_gen_new_label(gen, "tern");
        ir_track_label(gen->prog, label_end);

        int cond_val = ir_lower_expr(gen, node->u.ternary.cond);

        int idx1 = ir_grow(gen->prog);
        IRInstr *i1 = &gen->prog->instrs[idx1];
        i1->kind = IR_BR_IF_NOT;
        i1->src_id = cond_val;
        i1->label = label_else;

        int then_val = ir_lower_expr(gen, node->u.ternary.then_e);

        int idx2 = ir_grow(gen->prog);
        IRInstr *i2 = &gen->prog->instrs[idx2];
        i2->kind = IR_BR;
        i2->label = label_end;

        int idx3 = ir_grow(gen->prog);
        IRInstr *i3 = &gen->prog->instrs[idx3];
        i3->kind = IR_LABEL;
        i3->label = label_else;

        (void)ir_lower_expr(gen, node->u.ternary.else_e);

        int idx4 = ir_grow(gen->prog);
        IRInstr *i4 = &gen->prog->instrs[idx4];
        i4->kind = IR_BR;
        i4->label = label_end;

        int idx5 = ir_grow(gen->prog);
        IRInstr *i5 = &gen->prog->instrs[idx5];
        i5->kind = IR_LABEL;
        i5->label = label_end;

        return then_val;
    }

    case NODE_CAST:
        return ir_lower_expr(gen, node->u.cast.operand);

    case NODE_SIZEOF: {
        int t = ir_gen_new_temp(gen);
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_CONST;
        i->ival = (int64_t)ir_type_size(gen->sema, node->type_id);
        i->temp_id = t;
        i->type_size = 4;
        return t;
    }

    default:
        return -1;
    }
}

/* ---- statement lowering ---- */

static void ir_lower_stmt(IRGen *gen, Node *node)
{
    if (!node) return;

    switch (node->kind) {

    case NODE_BLOCK:
        ir_lower_block(gen, node);
        break;

    case NODE_EXPR_STMT:
        ir_lower_expr(gen, node->u.ret.value);
        break;

    case NODE_RETURN: {
        if (node->u.ret.value) {
            int val = ir_lower_expr(gen, node->u.ret.value);
            int idx = ir_grow(gen->prog);
            IRInstr *i = &gen->prog->instrs[idx];
            i->kind = IR_RET;
            i->src_id = val;
        } else {
            int idx = ir_grow(gen->prog);
            IRInstr *i = &gen->prog->instrs[idx];
            i->kind = IR_RET;
            i->src_id = -1;
        }
        break;
    }

    case NODE_IF: {
        char *label_else = ir_gen_new_label(gen, "if");
        ir_track_label(gen->prog, label_else);
        char *label_end = ir_gen_new_label(gen, "if");
        ir_track_label(gen->prog, label_end);

        int cond_val = ir_lower_expr(gen, node->u.if_stmt.cond);

        int idx1 = ir_grow(gen->prog);
        IRInstr *i1 = &gen->prog->instrs[idx1];
        i1->kind = IR_BR_IF_NOT;
        i1->src_id = cond_val;
        i1->label = label_else;

        ir_lower_block(gen, node->u.if_stmt.then_body);

        int idx2 = ir_grow(gen->prog);
        IRInstr *i2 = &gen->prog->instrs[idx2];
        i2->kind = IR_BR;
        i2->label = label_end;

        int idx3 = ir_grow(gen->prog);
        IRInstr *i3 = &gen->prog->instrs[idx3];
        i3->kind = IR_LABEL;
        i3->label = label_else;

        if (node->u.if_stmt.else_body) {
            ir_lower_block(gen, node->u.if_stmt.else_body);
        }

        int idx4 = ir_grow(gen->prog);
        IRInstr *i4 = &gen->prog->instrs[idx4];
        i4->kind = IR_LABEL;
        i4->label = label_end;
        break;
    }

    case NODE_WHILE: {
        char *label_top = ir_gen_new_label(gen, "while");
        ir_track_label(gen->prog, label_top);
        char *label_end = ir_gen_new_label(gen, "while");
        ir_track_label(gen->prog, label_end);

        int idx0 = ir_grow(gen->prog);
        IRInstr *i0 = &gen->prog->instrs[idx0];
        i0->kind = IR_LABEL;
        i0->label = label_top;

        int cond_val = ir_lower_expr(gen, node->u.loop.cond);

        int idx1 = ir_grow(gen->prog);
        IRInstr *i1 = &gen->prog->instrs[idx1];
        i1->kind = IR_BR_IF_NOT;
        i1->src_id = cond_val;
        i1->label = label_end;

        ir_lower_block(gen, node->u.loop.body);

        int idx2 = ir_grow(gen->prog);
        IRInstr *i2 = &gen->prog->instrs[idx2];
        i2->kind = IR_BR;
        i2->label = label_top;

        int idx3 = ir_grow(gen->prog);
        IRInstr *i3 = &gen->prog->instrs[idx3];
        i3->kind = IR_LABEL;
        i3->label = label_end;
        break;
    }

    case NODE_DO_WHILE: {
        char *label_top = ir_gen_new_label(gen, "do");
        ir_track_label(gen->prog, label_top);
        char *label_end = ir_gen_new_label(gen, "do");
        ir_track_label(gen->prog, label_end);

        int idx0 = ir_grow(gen->prog);
        IRInstr *i0 = &gen->prog->instrs[idx0];
        i0->kind = IR_LABEL;
        i0->label = label_top;

        ir_lower_block(gen, node->u.loop.body);

        int cond_val = ir_lower_expr(gen, node->u.loop.cond);

        int idx1 = ir_grow(gen->prog);
        IRInstr *i1 = &gen->prog->instrs[idx1];
        i1->kind = IR_BR_IF;
        i1->src_id = cond_val;
        i1->label = label_top;

        int idx2 = ir_grow(gen->prog);
        IRInstr *i2 = &gen->prog->instrs[idx2];
        i2->kind = IR_LABEL;
        i2->label = label_end;
        break;
    }

    case NODE_FOR: {
        char *label_cond = ir_gen_new_label(gen, "for");
        ir_track_label(gen->prog, label_cond);
        char *label_end = ir_gen_new_label(gen, "for");
        ir_track_label(gen->prog, label_end);

        if (node->u.for_stmt.init) {
            ir_lower_stmt(gen, node->u.for_stmt.init);
        }

        int idx0 = ir_grow(gen->prog);
        IRInstr *i0 = &gen->prog->instrs[idx0];
        i0->kind = IR_LABEL;
        i0->label = label_cond;

        if (node->u.for_stmt.cond) {
            int cond_val = ir_lower_expr(gen, node->u.for_stmt.cond);

            int idx1 = ir_grow(gen->prog);
            IRInstr *i1 = &gen->prog->instrs[idx1];
            i1->kind = IR_BR_IF_NOT;
            i1->src_id = cond_val;
            i1->label = label_end;
        }

        ir_lower_block(gen, node->u.for_stmt.body);

        if (node->u.for_stmt.inc) {
            ir_lower_stmt(gen, node->u.for_stmt.inc);
        }

        int idx2 = ir_grow(gen->prog);
        IRInstr *i2 = &gen->prog->instrs[idx2];
        i2->kind = IR_BR;
        i2->label = label_cond;

        int idx3 = ir_grow(gen->prog);
        IRInstr *i3 = &gen->prog->instrs[idx3];
        i3->kind = IR_LABEL;
        i3->label = label_end;
        break;
    }

    case NODE_BREAK:
    case NODE_CONTINUE:
        break;

    case NODE_GOTO: {
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_BR;
        i->label = safe_strdup(node->u.goto_stmt.label);
        ir_track_label(gen->prog, i->label);
        break;
    }

    case NODE_LABEL: {
        int idx = ir_grow(gen->prog);
        IRInstr *i = &gen->prog->instrs[idx];
        i->kind = IR_LABEL;
        i->label = safe_strdup(node->u.label_stmt.name);
        ir_track_label(gen->prog, i->label);

        if (node->u.label_stmt.stmt) {
            ir_lower_stmt(gen, node->u.label_stmt.stmt);
        }
        break;
    }

    case NODE_VAR_DECL: {
        /* Look up the symbol in global scope explicitly (bypasses broken scope chain) */
        uint32_t vhash = hash_name(node->u.var_decl.name);
        Symbol *global_sym = sema_lookup_global(gen->sema, vhash, node->u.var_decl.name);

        /* Skip global variables - they are handled at the top level */
        if (global_sym && global_sym->scope_kind == SCOPE_GLOBAL) {
            break;
        }

        int type_sz = ir_type_size(gen->sema, node->u.var_decl.type_id);
        if (type_sz <= 0) type_sz = 4;

        ir_emit_alloc(gen->prog, type_sz);
        gen->frame_offset -= type_sz;


        /* Record stack offset in symbol for later loads/stores */
        {
            uint32_t vhash = hash_name(node->u.var_decl.name);
            Symbol *sym = sema_lookup(gen->sema, vhash, node->u.var_decl.name);
            if (sym) sym->data = (void *)(intptr_t)gen->frame_offset;
        }
        if (node->u.var_decl.init) {
            int val = ir_lower_expr(gen, node->u.var_decl.init);
            int addr = ir_emit_addr_local(gen->prog, gen->frame_offset);
            ir_emit_store(gen->prog, addr, val, type_sz,
                          ir_type_signed(gen->sema, node->u.var_decl.type_id));
        }
        break;
    }

    case NODE_FUNC_DECL:
        break;

    default:
        break;
    }
}

/* ==================================================================
 *  Public: ir_lower  --  AST -> IR
 * ================================================================== */

IRProgram *ir_lower(Node *ast, SemaContext *sema)
{
    if (!ast) return NULL;

    IRProgram *prog = ir_create();
    if (!prog) return NULL;

    IRGen gen;
    gen.prog = prog;
    gen.sema = sema;
    gen.temp_counter = 0;
    gen.label_counter = 0;
    gen.frame_offset = 0;

    if (ast->kind == NODE_BLOCK) {
        for (int i = 0; i < ast->u.block.nstmts; i++) {
            Node *stmt = ast->u.block.stmts[i];
            if (!stmt) continue;

            if (stmt->kind == NODE_FUNC_DECL) {
                const char *fname = stmt->u.func_decl.name;

                ir_emit_label(prog, fname);

                for (int p = 0; p < stmt->u.func_decl.nparams; p++) {
                    int idx = ir_grow(prog);
                    IRInstr *i = &prog->instrs[idx];
                    i->kind = IR_PARAM;
                    i->ival = (int64_t)p;

                    /* Record parameter index for address lowering */
                    ParamDecl *pd = &stmt->u.func_decl.params[p];
                    if (pd->name) {
                        Symbol *psym = sema_lookup(sema, pd->hash, pd->name);
                        if (psym) psym->data = (void *)(intptr_t)p;
                    }
                }

                gen.frame_offset = 0;

                ir_lower_block(&gen, stmt->u.func_decl.body);

                if (prog->count == 0 ||
                    prog->instrs[prog->count - 1].kind != IR_RET) {
                    int idx = ir_grow(prog);
                    IRInstr *i = &prog->instrs[idx];
                    i->kind = IR_RET;
                    i->src_id = -1;
                }
            } else if (stmt->kind == NODE_VAR_DECL) {
                /* Build the actual type by applying ptr_stars (matching sema) */
                int type_id = (stmt->u.var_decl.type_id >= 0)
                                  ? stmt->u.var_decl.type_id
                                  : sema_int_type(sema);
                for (int ps = 0; ps < stmt->u.var_decl.ptr_stars; ps++) {
                    type_id = sema_ptr_type(sema, type_id);
                }
                int type_sz = ir_type_size(sema, type_id);
                if (type_sz <= 0) type_sz = 4;

                /* If initialized with a string literal, emit the string to .rodata first */
                char *str_label = NULL;
                char *str_content = NULL;
                if (stmt->u.var_decl.init &&
                    stmt->u.var_decl.init->kind == NODE_STR_LIT) {
                    str_label = ir_gen_new_label(&gen, "str");
                    ir_track_label(prog, str_label);
                    str_content = safe_strdup(stmt->u.var_decl.init->u.str_lit.str);
                    ir_track_label(prog, str_content);
                    int idx = ir_grow(prog);
                    IRInstr *i = &prog->instrs[idx];
                    i->kind = IR_GLOBAL_STR;
                    i->label = str_label;
                    i->ival = (int64_t)(intptr_t)str_content;
                }

                /* Emit global variable to .data */
                int idx2 = ir_grow(prog);
                IRInstr *i2 = &prog->instrs[idx2];
                i2->kind = IR_GLOBAL_DATA;
                i2->label = safe_strdup(stmt->u.var_decl.name);
                ir_track_label(prog, i2->label);

                /* Set initializer */
                if (str_content) {
                    /* Store the string label so codegen can look up the
                     * IR_GLOBAL_STR symbol by name to resolve the rodata address. */
                    i2->ival = (int64_t)(intptr_t)str_label;
                } else if (stmt->u.var_decl.init &&
                           stmt->u.var_decl.init->kind == NODE_INT_LIT) {
                    i2->ival = stmt->u.var_decl.init->const_val;
                } else {
                    /* Zero-initialized (BSS) */
                    i2->ival = 0;
                }
            } else {
                ir_lower_stmt(&gen, stmt);
            }
        }
    } else {
        ir_lower_stmt(&gen, ast);
    }

    return prog;
}
