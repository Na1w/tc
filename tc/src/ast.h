#ifndef TC_AST_H
#define TC_AST_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    NODE_INT_LIT, NODE_CHAR_LIT, NODE_STR_LIT, NODE_IDENT,
    NODE_UNARY, NODE_BINARY, NODE_CALL, NODE_TERNARY, NODE_ASSIGN,
    NODE_INDEX, NODE_ADDR_OF, NODE_DEREF, NODE_CAST, NODE_SIZEOF,
    NODE_BLOCK, NODE_IF, NODE_WHILE, NODE_DO_WHILE, NODE_FOR,
    NODE_RETURN, NODE_BREAK, NODE_CONTINUE, NODE_GOTO, NODE_LABEL,
    NODE_SWITCH, NODE_CASE, NODE_DEFAULT, NODE_EXPR_STMT,
    NODE_VAR_DECL, NODE_FUNC_DECL, NODE_TYPEDEF
} NodeKind;

typedef enum {
    OP_NONE = 0, U_NEG, U_POS, U_NOT, U_BITNOT,
    U_PREINC, U_PREDEC, U_POSTINC, U_POSTDEC
} UnaryOp;

typedef enum {
    BIN_NONE = 0, BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_LSHL, BIN_LSHR, BIN_LT, BIN_GT, BIN_LEQ, BIN_GEQ,
    BIN_EQ, BIN_NEQ, BIN_BAND, BIN_BXOR, BIN_BOR,
    BIN_ANDAND, BIN_OROR,
    BIN_ASSIGN, BIN_PLUSEQ, BIN_MINUSEQ, BIN_STAREQ,
    BIN_SLASHEQ, BIN_PERCENTEQ, BIN_AMPREQ, BIN_PIPEEQ,
    BIN_CARETEQ, BIN_LSHLEQ, BIN_LSHREQ
} BinaryOp;

struct Node;

typedef struct {
    const char *name;
    uint32_t hash;
    int type_id;
} ParamDecl;

typedef struct Type {
    enum { TY_VOID=0, TY_INT, TY_CHAR, TY_LONG, TY_SHORT,
           TY_UCHAR, TY_UINT, TY_ULONG, TY_USHORT,
           TY_PTR, TY_ARRAY, TY_FUNC } kind;
    int size;              // sizeof in bytes (4 for int, 1 for char, 8 for ptr/long)
    int sign;              // 0=signed, 1=unsigned
    struct Type *elem;     // for PTR, ARRAY: element type
    int array_count;       // for ARRAY: number of elements
    struct Type *ret_type; // for FUNC: return type
    ParamDecl *params;     // for FUNC: parameter list
    int param_count;       // for FUNC: number of params
} Type;

typedef struct Node {
    NodeKind kind;
    size_t line, col;
    int type_id;
    int is_lvalue;
    int64_t const_val;
    int is_const_expr;
    union {
        struct { int64_t val; } lit_int;
        struct { int64_t val; } lit_char;
        struct { const char *str; size_t len; } str_lit;
        struct { const char *name; uint32_t hash; } ident;
        struct { UnaryOp op; struct Node *operand; } unary;
        struct { BinaryOp op; struct Node *left; struct Node *right; } binary;
        struct { struct Node *callee; struct Node **args; int nargs; } call;
        struct { struct Node *cond; struct Node *then_e; struct Node *else_e; } ternary;
        struct { struct Node *lvalue; struct Node *rhs; BinaryOp assign_op; } assign;
        struct { struct Node *array; struct Node *index; } index;
        struct { struct Node *cond; struct Node *then_body; struct Node *else_body; } if_stmt;
        struct { struct Node *cond; struct Node *body; } loop;
        struct { struct Node *init; struct Node *cond; struct Node *inc; struct Node *body; } for_stmt;
        struct { struct Node *value; } ret;
        struct { const char *label; } goto_stmt;
        struct { const char *name; struct Node *stmt; } label_stmt;
        struct { struct Node *cond; struct Node *body; } switch_node;
        struct { int64_t lo; int64_t hi; struct Node *body; } case_stmt;
        struct { struct Node **stmts; int nstmts; } block;
        struct { const char *name; int type_id; struct Node *init; int is_static; } var_decl;
        struct { const char *name; int ret_type_id; ParamDecl *params; int nparams; struct Node *body; } func_decl;
        struct { const char *old_name; const char *new_name; } typedef_node;
        struct { struct Node *type_expr; struct Node *operand; } cast;
        struct { struct Node *operand; } sizeof_node;
    } u;
} Node;

Node *node_create(NodeKind kind, size_t line, size_t col);
void node_destroy(Node *node);
void node_dump(const Node *node, int indent);
const char *node_kind_name(NodeKind kind);
const char *unary_op_name(UnaryOp op);
const char *binary_op_name(BinaryOp op);

#endif
