#ifndef TC_SEMA_H
#define TC_SEMA_H

#include "ast.h"
#include <stdarg.h>
#include <stdint.h>

/* ==================================================================
 * Scope & Symbol tables
 * ================================================================== */

typedef enum {
    SCOPE_GLOBAL,
    SCOPE_FUNC_PARAMS,
    SCOPE_LOCAL,
    SCOPE_LOOP
} ScopeKind;

typedef enum {
    SYM_NONE = 0,
    SYM_VAR,
    SYM_PARAM,
    SYM_FUNC,
    SYM_LABEL,
    SYM_TYPEDEF
} SymKind;

typedef struct Symbol {
    const char *name;
    uint32_t hash;
    SymKind kind;
    Type *type;             /* pointer into type table */
    int scope_kind;         /* SCOPE_* */
    int64_t init_value;     /* for global vars with constant init */
    void *data;             /* context: stack offset, data section info */
    int is_extern;
} Symbol;

typedef struct Scope {
    Symbol *symbols[256];   /* hash-bucketed symbol pointers */
    uint32_t hashes[256];   /* stored hashes for collision resolution */
    struct Scope *parent;   /* parent scope for nesting */
    ScopeKind kind;
} Scope;

/* ==================================================================
 * Semantic analysis context
 * ================================================================== */

typedef struct SemaContext {
    /* Type table */
    Type **type_table;
    int type_count;
    int type_cap;

    /* Symbol table */
    Scope global_scope;
    Scope *current_scope;
    Symbol **resolved_symbols;
    int sym_count;
    int sym_cap;

    /* Cached builtin type indices */
    int type_id_void;
    int type_id_int;
    int type_id_char;
    int type_id_long;
    int type_id_short;
    int type_id_uint;
    int type_id_uchar;
    int type_id_ulong;
    int type_id_ushort;

    /* Diagnostics */
    int error_count;
    Node *current_node;
    Node *current_func;
} SemaContext;

/* ==================================================================
 * Core API
 * ================================================================== */

SemaContext *sema_create(void);
void         sema_destroy(SemaContext *sema);

/* Analyze the entire AST. Returns 0 on success, -1 if errors were found. */
int sema_analyze(SemaContext *sema, Node *ast);

/* Lookup an identifier by hash + name, walking scope chain upward. */
Symbol *sema_lookup(SemaContext *sema, uint32_t hash, const char *name);

/* Get a Type pointer by its table index. */
Type *sema_get_type(SemaContext *sema, int type_id);

/* Register a new Type in the table; returns its index. */
int sema_add_type(SemaContext *sema, Type *t);

/* ==================================================================
 * Builtin type accessors
 * ================================================================== */

int sema_int_type(SemaContext *sema);
int sema_char_type(SemaContext *sema);
int sema_void_type(SemaContext *sema);
int sema_long_type(SemaContext *sema);
int sema_ptr_type(SemaContext *sema, int elem_type_id);

/* Build a Type from parser-declared specifiers. */
Type *sema_build_type(int spec_kind, int qualifiers);

/* ==================================================================
 * Error reporting
 * ================================================================== */

void sema_error(SemaContext *sema, const char *fmt, ...);

#endif /* TC_SEMA_H */
