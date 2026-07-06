/*
 * sema.c -- Semantic analysis for the "tc" C compiler.
 *
 * Implements:
 *   1. Type table  (int=4B, char=1B, long=8B, ptr=8B, unsigned variants)
 *   2. Hash-based symbol table with scope nesting (global/param/local/loop)
 *   3. Identifier resolution, type checking for binary/unary ops, calls
 *   4. Constant expression evaluation (marks is_const_expr, sets const_val)
 */

#include "sema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ==================================================================
 * Internal helpers
 * ================================================================== */

/* Portable strdup for C99 */
static char *strdup_name(const char *s)
{
    size_t len = strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d)
        memcpy(d, s, len);
    return d;
}

/* djb2 hash */
static uint32_t hash_name(const char *name)
{
    uint32_t h = 5381;
    while (*name) {
        h = h * 33 + (uint32_t)(unsigned char)*name;
        name++;
    }
    return h;
}

/* Allocate a fresh Type on the heap. */
static Type *type_alloc(void)
{
    Type *t = (Type *)calloc(1, sizeof(Type));
    return t;
}

/* Shallow copy of a Type. */
static Type *type_copy(Type *src)
{
    if (!src)
        return NULL;
    Type *t = (Type *)calloc(1, sizeof(Type));
    t->kind        = src->kind;
    t->size        = src->size;
    t->sign        = src->sign;
    t->elem        = src->elem;
    t->array_count = src->array_count;
    t->ret_type    = src->ret_type;
    t->params      = src->params;
    t->param_count = src->param_count;
    return t;
}

/* Find the table index of a Type pointer (or -1). */
static int type_index_of(SemaContext *sema, Type *t)
{
    for (int i = 0; i < sema->type_count; i++) {
        if (sema->type_table[i] == t)
            return i;
    }
    return -1;
}

/* ==================================================================
 * Symbol allocation
 * ================================================================== */

static Symbol *sym_alloc(SemaContext *sema,
                         const char *name, uint32_t hash,
                         SymKind kind, Type *type,
                         int64_t init_value, void *data,
                         int is_extern)
{
    if (sema->sym_count >= sema->sym_cap) {
        int new_cap = sema->sym_cap == 0 ? 64 : sema->sym_cap * 2;
        sema->resolved_symbols = (Symbol **)realloc(
            (void *)sema->resolved_symbols,
            (size_t)new_cap * sizeof(Symbol *));
        sema->sym_cap = new_cap;
    }
    Symbol *sym = (Symbol *)calloc(1, sizeof(Symbol));
    sym->name       = strdup_name(name);
    sym->hash       = hash;
    sym->kind       = kind;
    sym->type       = type;
    sym->scope_kind = sema->current_scope
                        ? (int)sema->current_scope->kind
                        : SCOPE_GLOBAL;
    sym->init_value = init_value;
    sym->data       = data;
    sym->is_extern  = is_extern;
    sema->resolved_symbols[sema->sym_count++] = sym;
    return sym;
}

/* ==================================================================
 * Scope management
 * ================================================================== */

static void scope_init(Scope *scope, ScopeKind kind, Scope *parent)
{
    memset(scope->symbols, 0, sizeof(scope->symbols));
    memset(scope->hashes,  0, sizeof(scope->hashes));
    scope->parent = parent;
    scope->kind   = kind;
}

/* Open-addressing hash insert into a scope. */
static void scope_insert(Scope *scope, Symbol *sym)
{
    int idx = (int)(sym->hash % 256);
    for (int i = 0; i < 256; i++) {
        int check = (idx + i) % 256;
        if (!scope->symbols[check]) {
            scope->symbols[check] = sym;
            scope->hashes[check]  = sym->hash;
            return;
        }
    }
}

/* ==================================================================
 * SemaContext lifecycle
 * ================================================================== */

SemaContext *sema_create(void)
{
    SemaContext *sema = (SemaContext *)calloc(1, sizeof(SemaContext));
    if (!sema)
        return NULL;

    /* -- Type table -- */
    sema->type_cap  = 64;
    sema->type_table = (Type **)calloc(
        (size_t)sema->type_cap, sizeof(Type *));

    /* -- Scope chain -- */
    scope_init(&sema->global_scope, SCOPE_GLOBAL, NULL);
    sema->current_scope = &sema->global_scope;

    /* -- Symbol arena -- */
    sema->sym_cap = 64;
    sema->resolved_symbols = (Symbol **)calloc(
        (size_t)sema->sym_cap, sizeof(Symbol *));

    /* -- Register builtin types -- */
    { Type *t = type_alloc(); t->kind = TY_VOID;  t->size = 0;  t->sign = 0;
      sema->type_id_void  = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_INT;   t->size = 4;  t->sign = 0;
      sema->type_id_int   = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_CHAR;  t->size = 1;  t->sign = 0;
      sema->type_id_char  = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_LONG;  t->size = 8;  t->sign = 0;
      sema->type_id_long  = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_UINT;  t->size = 4;  t->sign = 1;
      sema->type_id_uint  = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_UCHAR; t->size = 1;  t->sign = 1;
      sema->type_id_uchar = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_ULONG; t->size = 8;  t->sign = 1;
      sema->type_id_ulong = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_SHORT;  t->size = 2; t->sign = 0;
      sema->type_id_short  = sema_add_type(sema, t); }
    { Type *t = type_alloc(); t->kind = TY_USHORT; t->size = 2; t->sign = 1;
      sema->type_id_ushort = sema_add_type(sema, t); }

    return sema;
}

void sema_destroy(SemaContext *sema)
{
    if (!sema)
        return;
    for (int i = 0; i < sema->type_count; i++)
        free(sema->type_table[i]);
    for (int i = 0; i < sema->sym_count; i++) {
        Symbol *s = sema->resolved_symbols[i];
        if (s) {
            free((void *)s->name);
            free(s);
        }
    }
    free(sema->type_table);
    free(sema->resolved_symbols);
    free(sema);
}

/* ==================================================================
 * Public API
 * ================================================================== */

Symbol *sema_lookup(SemaContext *sema, uint32_t hash, const char *name)
{
    /* Walk scope chain from innermost outward. */
    Scope *scope = sema->current_scope;
    while (scope) {
        int idx = (int)(hash % 256);
        for (int i = 0; i < 256; i++) {
            int check = (idx + i) % 256;
            Symbol *s = scope->symbols[check];
            if (s) {
                if (s->hash == hash && strcmp(s->name, name) == 0)
                    return s;
            } else {
                break; /* empty slot */
            }
        }
        scope = scope->parent;
    }

    /* Fallback: linear search resolved_symbols (survives destroyed scopes). */
    for (int i = 0; i < sema->sym_count; i++) {
        Symbol *s = sema->resolved_symbols[i];
        if (s->hash == hash && strcmp(s->name, name) == 0)
            return s;
    }
    return NULL;
}

Type *sema_get_type(SemaContext *sema, int type_id)
{
    if (type_id < 0 || type_id >= sema->type_count)
        return NULL;
    return sema->type_table[type_id];
}

int sema_add_type(SemaContext *sema, Type *t)
{
    if (sema->type_count >= sema->type_cap) {
        int new_cap = sema->type_cap * 2;
        sema->type_table = (Type **)realloc(
            (void *)sema->type_table,
            (size_t)new_cap * sizeof(Type *));
        sema->type_cap = new_cap;
    }
    int id = sema->type_count;
    sema->type_table[sema->type_count++] = t;
    return id;
}

/* Builtin type accessors */
int sema_int_type(SemaContext *sema)  { return sema->type_id_int;   }
int sema_char_type(SemaContext *sema) { return sema->type_id_char;  }
int sema_void_type(SemaContext *sema) { return sema->type_id_void;  }
int sema_long_type(SemaContext *sema) { return sema->type_id_long;  }

/* Get or create a pointer type for the given element type. */
int sema_ptr_type(SemaContext *sema, int elem_type_id)
{
    Type *elem = sema_get_type(sema, elem_type_id);
    if (!elem)
        return -1;

    /* Reuse existing pointer type if present. */
    for (int i = 0; i < sema->type_count; i++) {
        Type *t = sema->type_table[i];
        if (t->kind == TY_PTR && t->elem == elem)
            return i;
    }

    Type *t = type_alloc();
    t->kind = TY_PTR;
    t->size = 8;
    t->sign = 0;
    t->elem = elem;
    return sema_add_type(sema, t);
}

/* Build a Type from parser-declared specifiers/qualifiers. */
Type *sema_build_type(int spec_kind, int qualifiers)
{
    Type *t = type_alloc();
    int is_unsigned = qualifiers & 1;

    switch (spec_kind) {
    case 0: /* int */
        t->kind = is_unsigned ? TY_UINT : TY_INT;
        t->size = 4; t->sign = is_unsigned; break;
    case 1: /* void */
        t->kind = TY_VOID; t->size = 0; t->sign = 0; break;
    case 2: /* char */
        t->kind = is_unsigned ? TY_UCHAR : TY_CHAR;
        t->size = 1; t->sign = is_unsigned; break;
    case 3: /* short */
        t->kind = is_unsigned ? TY_USHORT : TY_SHORT;
        t->size = 2; t->sign = is_unsigned; break;
    case 4: /* long */
        t->kind = is_unsigned ? TY_ULONG : TY_LONG;
        t->size = 8; t->sign = is_unsigned; break;
    default:
        t->kind = TY_INT; t->size = 4; t->sign = 0; break;
    }
    return t;
}

void sema_error(SemaContext *sema, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    sema->error_count++;
}

/* ==================================================================
 * Forward declarations
 * ================================================================== */

static int analyze_expression(SemaContext *sema, Node *node);
static int analyze_statement (SemaContext *sema, Node *node);
static int analyze_block     (SemaContext *sema, Node *node);

/* ==================================================================
 * Declaration analysis
 * ================================================================== */

static int analyze_var_decl(SemaContext *sema, Node *node)
{
    int type_id = sema_int_type(sema);

    int64_t init_value = 0;
    if (node->u.var_decl.init) {
        int init_type = analyze_expression(sema, node->u.var_decl.init);
        if (init_type < 0)
            return -1;
        if (node->u.var_decl.init->is_const_expr)
            init_value = node->u.var_decl.init->const_val;
    }

    const char *name = node->u.var_decl.name;
    uint32_t hash    = hash_name(name);

    Symbol *existing = sema_lookup(sema, hash, name);
    if (existing) {
        sema_error(sema, "%zu:%zu: error: redefinition of '%s'",
                   node->line, node->col, name);
        return -1;
    }

    Symbol *sym = sym_alloc(sema, name, hash, SYM_VAR,
                            sema_get_type(sema, type_id),
                            init_value, NULL,
                            node->u.var_decl.is_static);
    Scope *scope = sema->current_scope ? sema->current_scope
                                       : &sema->global_scope;
    scope_insert(scope, sym);

    node->type_id   = type_id;
    node->is_lvalue = 1;
    return 0;
}

static int analyze_func_decl(SemaContext *sema, Node *node)
{
    sema->current_func = node;

    const char *name = node->u.func_decl.name;
    uint32_t hash    = hash_name(name);

    int ret_type_id = sema_int_type(sema);

    Type *func_type = type_alloc();
    func_type->kind        = TY_FUNC;
    func_type->size        = 0;
    func_type->sign        = 0;
    func_type->ret_type    = sema_get_type(sema, ret_type_id);
    func_type->params      = node->u.func_decl.params;
    func_type->param_count = node->u.func_decl.nparams;
    int func_type_id = sema_add_type(sema, func_type);

    Symbol *existing = sema_lookup(sema, hash, name);
    if (existing && existing->kind == SYM_FUNC) {
        sema_error(sema, "%zu:%zu: error: redefinition of function '%s'",
                   node->line, node->col, name);
        return -1;
    }

    Symbol *sym = sym_alloc(sema, name, hash, SYM_FUNC,
                            func_type, 0, NULL, 0);
    sym->scope_kind = SCOPE_GLOBAL;
    scope_insert(&sema->global_scope, sym);

    /* Parameter scope */
    Scope param_scope;
    scope_init(&param_scope, SCOPE_FUNC_PARAMS,
               sema->current_scope ? sema->current_scope
                                   : &sema->global_scope);
    Scope *old_scope = sema->current_scope;
    sema->current_scope = &param_scope;

    for (int i = 0; i < node->u.func_decl.nparams; i++) {
        ParamDecl *pd = &node->u.func_decl.params[i];
        if (!pd->name)
            continue;
        int param_type_id = pd->type_id >= 0 ? pd->type_id
                                             : sema_int_type(sema);
        Symbol *psym = sym_alloc(sema, pd->name, pd->hash, SYM_PARAM,
                                 sema_get_type(sema, param_type_id),
                                 0, NULL, 0);
        psym->scope_kind = SCOPE_FUNC_PARAMS;
        scope_insert(&param_scope, psym);
    }

    if (node->u.func_decl.body)
        analyze_block(sema, node->u.func_decl.body);

    sema->current_scope = old_scope;
    sema->current_func  = NULL;

    node->type_id = func_type_id;
    return 0;
}

/* ==================================================================
 * Expression analysis  (returns type_id >= 0 on success, -1 on error)
 * ================================================================== */

static int analyze_expression(SemaContext *sema, Node *node)
{
    if (!node)
        return -1;

    sema->current_node = node;

    switch (node->kind) {

    /* -- Literals -- */
    case NODE_INT_LIT: {
        node->type_id      = sema_int_type(sema);
        node->is_lvalue    = 0;
        node->is_const_expr = 1;
        node->const_val    = node->u.lit_int.val;
        return node->type_id;
    }
    case NODE_CHAR_LIT: {
        node->type_id      = sema_int_type(sema);
        node->is_lvalue    = 0;
        node->is_const_expr = 1;
        node->const_val    = node->u.lit_char.val;
        return node->type_id;
    }
    case NODE_STR_LIT: {
        int ptr_type = sema_ptr_type(sema, sema_char_type(sema));
        node->type_id      = ptr_type;
        node->is_lvalue    = 0;
        node->is_const_expr = 1;
        return ptr_type;
    }

    /* -- Identifier -- */
    case NODE_IDENT: {
        const char *name = node->u.ident.name;
        uint32_t hash    = node->u.ident.hash;
        Symbol *sym      = sema_lookup(sema, hash, name);
        if (!sym) {
            sema_error(sema, "%zu:%zu: error: undeclared identifier '%s'",
                       node->line, node->col, name);
            node->type_id   = sema_int_type(sema);
            node->is_lvalue = 0;
            return -1;
        }
        node->type_id = type_index_of(sema, sym->type);
        if (node->type_id < 0)
            node->type_id = sema_add_type(sema, type_copy(sym->type));
        node->is_lvalue = (sym->kind == SYM_VAR || sym->kind == SYM_PARAM);
        node->is_const_expr = 0;
        return node->type_id;
    }

    /* -- Unary operators -- */
    case NODE_UNARY: {
        int operand_type = analyze_expression(sema, node->u.unary.operand);
        if (operand_type < 0)
            return -1;

        switch (node->u.unary.op) {
        case U_NEG:
        case U_POS: {
            node->type_id   = operand_type;
            node->is_lvalue = 0;
            if (node->u.unary.operand->is_const_expr) {
                node->is_const_expr = 1;
                node->const_val = (node->u.unary.op == U_NEG)
                                    ? -node->u.unary.operand->const_val
                                    : node->u.unary.operand->const_val;
            } else {
                node->is_const_expr = 0;
            }
            return operand_type;
        }
        case U_NOT: {
            node->type_id   = sema_int_type(sema);
            node->is_lvalue = 0;
            if (node->u.unary.operand->is_const_expr) {
                node->is_const_expr = 1;
                node->const_val = (node->u.unary.operand->const_val == 0);
            } else {
                node->is_const_expr = 0;
            }
            return node->type_id;
        }
        case U_BITNOT: {
            node->type_id   = operand_type;
            node->is_lvalue = 0;
            if (node->u.unary.operand->is_const_expr) {
                node->is_const_expr = 1;
                node->const_val = ~node->u.unary.operand->const_val;
            } else {
                node->is_const_expr = 0;
            }
            return operand_type;
        }
        case U_PREINC:
        case U_PREDEC: {
            if (!node->u.unary.operand->is_lvalue) {
                sema_error(sema,
                           "%zu:%zu: error: increment/decrement requires lvalue",
                           node->line, node->col);
                return -1;
            }
            node->type_id      = operand_type;
            node->is_lvalue    = 0;
            node->is_const_expr = 0;
            return operand_type;
        }
        case U_POSTINC:
        case U_POSTDEC: {
            if (!node->u.unary.operand->is_lvalue) {
                sema_error(sema,
                           "%zu:%zu: error: increment/decrement requires lvalue",
                           node->line, node->col);
                return -1;
            }
            node->type_id      = operand_type;
            node->is_lvalue    = 0;
            node->is_const_expr = 0;
            return operand_type;
        }
        default:
            node->type_id      = operand_type;
            node->is_lvalue    = 0;
            node->is_const_expr = 0;
            return operand_type;
        }
    }

    /* -- Binary operators -- */
    case NODE_BINARY: {
        int left_type  = analyze_expression(sema, node->u.binary.left);
        if (left_type < 0)
            return -1;
        int right_type = analyze_expression(sema, node->u.binary.right);
        if (right_type < 0)
            return -1;

        Type *left_t  = sema_get_type(sema, left_type);
        Type *right_t = sema_get_type(sema, right_type);
        BinaryOp op   = node->u.binary.op;

        /* Assignment operators */
        if (op >= BIN_ASSIGN && op <= BIN_LSHREQ) {
            if (!node->u.binary.left->is_lvalue) {
                sema_error(sema,
                           "%zu:%zu: error: assignment requires lvalue",
                           node->line, node->col);
                return -1;
            }
            node->type_id      = left_type;
            node->is_lvalue    = 0;
            node->is_const_expr = 0;
            return left_type;
        }

        /* Arithmetic: + - * / % */
        if (op == BIN_ADD || op == BIN_SUB || op == BIN_MUL ||
            op == BIN_DIV || op == BIN_MOD) {
            int result_type = left_type;
            if (left_t && right_t && right_t->size > left_t->size)
                result_type = right_type;
            node->type_id   = result_type;
            node->is_lvalue = 0;

            if (node->u.binary.left->is_const_expr &&
                node->u.binary.right->is_const_expr) {
                node->is_const_expr = 1;
                int64_t lv = node->u.binary.left->const_val;
                int64_t rv = node->u.binary.right->const_val;
                switch (op) {
                case BIN_ADD: node->const_val = lv + rv; break;
                case BIN_SUB: node->const_val = lv - rv; break;
                case BIN_MUL: node->const_val = lv * rv; break;
                case BIN_DIV:
                    if (rv == 0) {
                        sema_error(sema,
                                   "%zu:%zu: error: division by zero",
                                   node->line, node->col);
                        node->const_val = 0;
                    } else {
                        node->const_val = lv / rv;
                    }
                    break;
                case BIN_MOD:
                    if (rv == 0) {
                        sema_error(sema,
                                   "%zu:%zu: error: modulo by zero",
                                   node->line, node->col);
                        node->const_val = 0;
                    } else {
                        node->const_val = lv % rv;
                    }
                    break;
                default: break;
                }
            } else {
                node->is_const_expr = 0;
            }
            return result_type;
        }

        /* Shift: << >> */
        if (op == BIN_LSHL || op == BIN_LSHR) {
            node->type_id   = left_type;
            node->is_lvalue = 0;
            if (node->u.binary.left->is_const_expr &&
                node->u.binary.right->is_const_expr) {
                node->is_const_expr = 1;
                int64_t lv = node->u.binary.left->const_val;
                int64_t rv = node->u.binary.right->const_val;
                node->const_val = (op == BIN_LSHL) ? (lv << rv) : (lv >> rv);
            } else {
                node->is_const_expr = 0;
            }
            return left_type;
        }

        /* Comparison: < > <= >= == != */
        if (op == BIN_LT || op == BIN_GT || op == BIN_LEQ ||
            op == BIN_GEQ || op == BIN_EQ || op == BIN_NEQ) {
            node->type_id   = sema_int_type(sema);
            node->is_lvalue = 0;
            if (node->u.binary.left->is_const_expr &&
                node->u.binary.right->is_const_expr) {
                node->is_const_expr = 1;
                int64_t lv = node->u.binary.left->const_val;
                int64_t rv = node->u.binary.right->const_val;
                switch (op) {
                case BIN_LT:  node->const_val = (lv <  rv); break;
                case BIN_GT:  node->const_val = (lv >  rv); break;
                case BIN_LEQ: node->const_val = (lv <= rv); break;
                case BIN_GEQ: node->const_val = (lv >= rv); break;
                case BIN_EQ:  node->const_val = (lv == rv); break;
                case BIN_NEQ: node->const_val = (lv != rv); break;
                default: break;
                }
            } else {
                node->is_const_expr = 0;
            }
            return node->type_id;
        }

        /* Bitwise: & ^ | */
        if (op == BIN_BAND || op == BIN_BXOR || op == BIN_BOR) {
            node->type_id   = left_type;
            node->is_lvalue = 0;
            if (node->u.binary.left->is_const_expr &&
                node->u.binary.right->is_const_expr) {
                node->is_const_expr = 1;
                int64_t lv = node->u.binary.left->const_val;
                int64_t rv = node->u.binary.right->const_val;
                switch (op) {
                case BIN_BAND: node->const_val = lv & rv; break;
                case BIN_BXOR: node->const_val = lv ^ rv; break;
                case BIN_BOR:  node->const_val = lv | rv; break;
                default: break;
                }
            } else {
                node->is_const_expr = 0;
            }
            return left_type;
        }

        /* Logical: && || */
        if (op == BIN_ANDAND || op == BIN_OROR) {
            node->type_id   = sema_int_type(sema);
            node->is_lvalue = 0;
            if (node->u.binary.left->is_const_expr &&
                node->u.binary.right->is_const_expr) {
                node->is_const_expr = 1;
                int64_t lv = node->u.binary.left->const_val;
                int64_t rv = node->u.binary.right->const_val;
                node->const_val = (op == BIN_ANDAND) ? (lv && rv) : (lv || rv);
            } else {
                node->is_const_expr = 0;
            }
            return node->type_id;
        }

        /* Fallback for unknown binary op */
        node->type_id      = left_type;
        node->is_lvalue    = 0;
        node->is_const_expr = 0;
        return left_type;
    }

    /* -- Address-of -- */
    case NODE_ADDR_OF: {
        int operand_type = analyze_expression(sema, node->u.unary.operand);
        if (operand_type < 0)
            return -1;
        if (!node->u.unary.operand->is_lvalue) {
            sema_error(sema, "%zu:%zu: error: address-of requires lvalue",
                       node->line, node->col);
            return -1;
        }
        int ptr_type = sema_ptr_type(sema, operand_type);
        node->type_id      = ptr_type;
        node->is_lvalue    = 0;
        node->is_const_expr = 0;
        return ptr_type;
    }

    /* -- Dereference -- */
    case NODE_DEREF: {
        int operand_type = analyze_expression(sema, node->u.unary.operand);
        if (operand_type < 0)
            return -1;
        Type *op_t = sema_get_type(sema, operand_type);
        if (!op_t || op_t->kind != TY_PTR) {
            sema_error(sema,
                       "%zu:%zu: error: dereference requires pointer type",
                       node->line, node->col);
            node->type_id   = sema_int_type(sema);
            node->is_lvalue = 1;
            return -1;
        }
        int elem_type_id = type_index_of(sema, op_t->elem);
        if (elem_type_id < 0)
            elem_type_id = sema_add_type(sema, type_copy(op_t->elem));
        node->type_id      = elem_type_id;
        node->is_lvalue    = 1;
        node->is_const_expr = 0;
        return elem_type_id;
    }

    /* -- Array index -- */
    case NODE_INDEX: {
        int base_type = analyze_expression(sema, node->u.index.array);
        if (base_type < 0)
            return -1;
        int idx_type = analyze_expression(sema, node->u.index.index);
        (void)idx_type;

        Type *base_t = sema_get_type(sema, base_type);
        if (!base_t)
            return -1;
        if (base_t->kind != TY_PTR && base_t->kind != TY_ARRAY) {
            sema_error(sema,
                       "%zu:%zu: error: subscript requires pointer or array",
                       node->line, node->col);
            node->type_id   = sema_int_type(sema);
            node->is_lvalue = 1;
            return -1;
        }
        int elem_type_id = type_index_of(sema, base_t->elem);
        if (elem_type_id < 0)
            elem_type_id = sema_add_type(sema, type_copy(base_t->elem));
        node->type_id      = elem_type_id;
        node->is_lvalue    = 1;
        node->is_const_expr = 0;
        return elem_type_id;
    }

    /* -- Function call -- */
    case NODE_CALL: {
        int callee_type = analyze_expression(sema, node->u.call.callee);
        if (callee_type < 0)
            return -1;
        Type *callee_t = sema_get_type(sema, callee_type);

        if (!callee_t ||
            (callee_t->kind != TY_FUNC && callee_t->kind != TY_PTR)) {
            sema_error(sema,
                       "%zu:%zu: error: called object is not a function",
                       node->line, node->col);
            node->type_id   = sema_int_type(sema);
            node->is_lvalue = 0;
            return -1;
        }

        for (int i = 0; i < node->u.call.nargs; i++) {
            if (node->u.call.args[i])
                analyze_expression(sema, node->u.call.args[i]);
        }

        if (callee_t->kind == TY_FUNC && callee_t->ret_type) {
            int ret_id = type_index_of(sema, callee_t->ret_type);
            if (ret_id < 0)
                ret_id = sema_add_type(sema, type_copy(callee_t->ret_type));
            node->type_id = ret_id;
        } else {
            node->type_id = sema_int_type(sema);
        }
        node->is_lvalue    = 0;
        node->is_const_expr = 0;
        return node->type_id;
    }

    /* -- Ternary ? : -- */
    case NODE_TERNARY: {
        int cond_type = analyze_expression(sema, node->u.ternary.cond);
        (void)cond_type;
        int then_type = analyze_expression(sema, node->u.ternary.then_e);
        if (then_type < 0)
            return -1;
        int else_type = analyze_expression(sema, node->u.ternary.else_e);
        if (else_type < 0)
            return -1;

        Type *then_t = sema_get_type(sema, then_type);
        Type *else_t = sema_get_type(sema, else_type);
        int result_type = then_type;
        if (then_t && else_t && else_t->size > then_t->size)
            result_type = else_type;
        node->type_id      = result_type;
        node->is_lvalue    = 0;
        node->is_const_expr = 0;
        return result_type;
    }

    /* -- Assignment node -- */
    case NODE_ASSIGN: {
        int lval_type = analyze_expression(sema, node->u.assign.lvalue);
        if (lval_type < 0)
            return -1;
        int rhs_type = analyze_expression(sema, node->u.assign.rhs);
        (void)rhs_type;

        if (!node->u.assign.lvalue->is_lvalue) {
            sema_error(sema,
                       "%zu:%zu: error: assignment requires lvalue",
                       node->line, node->col);
            return -1;
        }
        node->type_id      = lval_type;
        node->is_lvalue    = 0;
        node->is_const_expr = 0;
        return lval_type;
    }

    /* -- Cast -- */
    case NODE_CAST: {
        int operand_type = analyze_expression(sema, node->u.cast.operand);
        (void)operand_type;

        int target_type = sema_int_type(sema);
        if (node->u.cast.type_expr &&
            node->u.cast.type_expr->kind == NODE_IDENT) {
            const char *type_name =
                node->u.cast.type_expr->u.ident.name;
            uint32_t type_hash =
                node->u.cast.type_expr->u.ident.hash;

            Symbol *tsym = sema_lookup(sema, type_hash, type_name);
            if (tsym && tsym->kind == SYM_TYPEDEF) {
                int tid = type_index_of(sema, tsym->type);
                if (tid >= 0)
                    target_type = tid;
            } else if (strcmp(type_name, "int")  == 0) {
                target_type = sema_int_type(sema);
            } else if (strcmp(type_name, "char") == 0) {
                target_type = sema_char_type(sema);
            } else if (strcmp(type_name, "void") == 0) {
                target_type = sema_void_type(sema);
            } else if (strcmp(type_name, "long") == 0) {
                target_type = sema_long_type(sema);
            }
        }

        node->type_id = target_type;
        node->is_lvalue = 0;
        node->is_const_expr = node->u.cast.operand->is_const_expr;
        if (node->is_const_expr)
            node->const_val = node->u.cast.operand->const_val;
        return target_type;
    }

    /* -- sizeof -- */
    case NODE_SIZEOF: {
        if (node->u.sizeof_node.operand) {
            if (node->u.sizeof_node.operand->kind == NODE_IDENT) {
                const char *type_name =
                    node->u.sizeof_node.operand->u.ident.name;
                uint32_t type_hash =
                    node->u.sizeof_node.operand->u.ident.hash;

                Symbol *tsym =
                    sema_lookup(sema, type_hash, type_name);
                if (tsym) {
                    node->type_id      = sema_int_type(sema);
                    node->is_const_expr = 1;
                    node->const_val     = tsym->type
                                            ? tsym->type->size : 0;
                    node->is_lvalue    = 0;
                    return node->type_id;
                }
                if      (strcmp(type_name, "int")   == 0)
                    node->const_val = 4;
                else if (strcmp(type_name, "char")  == 0)
                    node->const_val = 1;
                else if (strcmp(type_name, "long")  == 0)
                    node->const_val = 8;
                else if (strcmp(type_name, "short") == 0)
                    node->const_val = 2;
                else if (strcmp(type_name, "void")  == 0)
                    node->const_val = 0;
                else {
                    node->const_val = 0;
                    sema_error(sema,
                               "%zu:%zu: error: unknown type in sizeof '%s'",
                               node->line, node->col, type_name);
                }
            } else {
                analyze_expression(sema, node->u.sizeof_node.operand);
                Type *op_t = sema_get_type(
                    sema, node->u.sizeof_node.operand->type_id);
                node->const_val = op_t ? op_t->size : 0;
            }
        } else {
            node->const_val = 0;
        }
        node->type_id      = sema_int_type(sema);
        node->is_const_expr = 1;
        node->is_lvalue    = 0;
        return node->type_id;
    }

    default:
        node->type_id   = sema_int_type(sema);
        node->is_lvalue = 0;
        return -1;
    }
}

/* ==================================================================
 * Statement analysis
 * ================================================================== */

static int analyze_statement(SemaContext *sema, Node *node)
{
    if (!node)
        return 0;

    sema->current_node = node;

    switch (node->kind) {
    case NODE_BLOCK:
        return analyze_block(sema, node);

    case NODE_IF: {
        if (node->u.if_stmt.cond)
            analyze_expression(sema, node->u.if_stmt.cond);
        if (node->u.if_stmt.then_body)
            analyze_statement(sema, node->u.if_stmt.then_body);
        if (node->u.if_stmt.else_body)
            analyze_statement(sema, node->u.if_stmt.else_body);
        return 0;
    }

    case NODE_WHILE:
    case NODE_DO_WHILE: {
        Scope loop_scope;
        scope_init(&loop_scope, SCOPE_LOOP, sema->current_scope);
        Scope *old_scope = sema->current_scope;
        sema->current_scope = &loop_scope;

        if (node->u.loop.cond)
            analyze_expression(sema, node->u.loop.cond);
        if (node->u.loop.body)
            analyze_statement(sema, node->u.loop.body);

        sema->current_scope = old_scope;
        return 0;
    }

    case NODE_FOR: {
        Scope loop_scope;
        scope_init(&loop_scope, SCOPE_LOOP, sema->current_scope);
        Scope *old_scope = sema->current_scope;
        sema->current_scope = &loop_scope;

        if (node->u.for_stmt.init) {
            if (node->u.for_stmt.init->kind == NODE_VAR_DECL) {
                analyze_var_decl(sema, node->u.for_stmt.init);
            } else {
                analyze_expression(sema, node->u.for_stmt.init);
            }
        }
        if (node->u.for_stmt.cond)
            analyze_expression(sema, node->u.for_stmt.cond);
        if (node->u.for_stmt.inc)
            analyze_expression(sema, node->u.for_stmt.inc);
        if (node->u.for_stmt.body)
            analyze_statement(sema, node->u.for_stmt.body);

        sema->current_scope = old_scope;
        return 0;
    }

    case NODE_RETURN: {
        if (node->u.ret.value)
            analyze_expression(sema, node->u.ret.value);
        return 0;
    }

    case NODE_BREAK:
    case NODE_CONTINUE:
        return 0;

    case NODE_GOTO:
        /* Allow forward references; no error for MVP. */
        return 0;

    case NODE_LABEL: {
        const char *label = node->u.label_stmt.name;
        uint32_t hash     = hash_name(label);
        Symbol *sym = sym_alloc(sema, label, hash, SYM_LABEL,
                                NULL, 0, NULL, 0);
        Scope *scope = sema->current_scope
                         ? sema->current_scope
                         : &sema->global_scope;
        scope_insert(scope, sym);
        if (node->u.label_stmt.stmt)
            analyze_statement(sema, node->u.label_stmt.stmt);
        return 0;
    }

    case NODE_SWITCH: {
        Scope switch_scope;
        scope_init(&switch_scope, SCOPE_LOCAL, sema->current_scope);
        Scope *old_scope = sema->current_scope;
        sema->current_scope = &switch_scope;

        if (node->u.switch_node.cond)
            analyze_expression(sema, node->u.switch_node.cond);
        if (node->u.switch_node.body)
            analyze_statement(sema, node->u.switch_node.body);

        sema->current_scope = old_scope;
        return 0;
    }

    case NODE_CASE:
    case NODE_DEFAULT: {
        if (node->u.case_stmt.body)
            analyze_statement(sema, node->u.case_stmt.body);
        return 0;
    }

    case NODE_EXPR_STMT: {
        if (node->u.ret.value)
            analyze_expression(sema, node->u.ret.value);
        return 0;
    }

    case NODE_VAR_DECL:
        return analyze_var_decl(sema, node);

    case NODE_FUNC_DECL:
        return analyze_func_decl(sema, node);

    case NODE_TYPEDEF: {
        const char *new_name = node->u.typedef_node.new_name;
        uint32_t hash        = hash_name(new_name);

        Type *t = type_alloc();
        t->kind = TY_INT;
        t->size = 4;
        t->sign = 0;
        sema_add_type(sema, t);

        Symbol *sym = sym_alloc(sema, new_name, hash, SYM_TYPEDEF,
                                t, 0, NULL, 0);
        Scope *scope = sema->current_scope
                         ? sema->current_scope
                         : &sema->global_scope;
        scope_insert(scope, sym);
        return 0;
    }

    default:
        return 0;
    }
}

/* ==================================================================
 * Block analysis
 * ================================================================== */

static int analyze_block(SemaContext *sema, Node *node)
{
    if (!node)
        return 0;

    Scope local_scope;
    scope_init(&local_scope, SCOPE_LOCAL, sema->current_scope);
    Scope *old_scope = sema->current_scope;
    sema->current_scope = &local_scope;

    for (int i = 0; i < node->u.block.nstmts; i++) {
        if (node->u.block.stmts[i])
            analyze_statement(sema, node->u.block.stmts[i]);
    }

    sema->current_scope = old_scope;
    return 0;
}

/* ==================================================================
 * Top-level entry
 * ================================================================== */

int sema_analyze(SemaContext *sema, Node *ast)
{
    if (!sema || !ast)
        return -1;

    if (ast->kind == NODE_BLOCK) {
        analyze_block(sema, ast);
    } else {
        analyze_statement(sema, ast);
    }

    return sema->error_count > 0 ? -1 : 0;
}
