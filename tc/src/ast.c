#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Node *node_create(NodeKind kind, size_t line, size_t col) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    n->kind = kind;
    n->line = line;
    n->col = col;
    n->type_id = -1;
    n->is_lvalue = 0;
    n->const_val = 0;
    n->is_const_expr = 0;
    return n;
}

void node_destroy(Node *node) {
    if (!node) return;

    switch (node->kind) {
        case NODE_UNARY:
            node_destroy(node->u.unary.operand);
            break;
        case NODE_BINARY:
            node_destroy(node->u.binary.left);
            node_destroy(node->u.binary.right);
            break;
        case NODE_CALL: {
            node_destroy(node->u.call.callee);
            if (node->u.call.args) {
                for (int i = 0; i < node->u.call.nargs; i++) {
                    node_destroy(node->u.call.args[i]);
                }
                free(node->u.call.args);
            }
            break;
        }
        case NODE_TERNARY:
            node_destroy(node->u.ternary.cond);
            node_destroy(node->u.ternary.then_e);
            node_destroy(node->u.ternary.else_e);
            break;
        case NODE_ASSIGN:
            node_destroy(node->u.assign.lvalue);
            node_destroy(node->u.assign.rhs);
            break;
        case NODE_INDEX:
            node_destroy(node->u.index.array);
            node_destroy(node->u.index.index);
            break;
        case NODE_ADDR_OF:
        case NODE_DEREF:
        case NODE_SIZEOF:
            node_destroy(node->u.unary.operand);
            break;
        case NODE_CAST:
            node_destroy(node->u.cast.type_expr);
            node_destroy(node->u.cast.operand);
            break;
        case NODE_IF:
            node_destroy(node->u.if_stmt.cond);
            node_destroy(node->u.if_stmt.then_body);
            node_destroy(node->u.if_stmt.else_body);
            break;
        case NODE_WHILE:
        case NODE_DO_WHILE:
            node_destroy(node->u.loop.cond);
            node_destroy(node->u.loop.body);
            break;
        case NODE_FOR:
            node_destroy(node->u.for_stmt.init);
            node_destroy(node->u.for_stmt.cond);
            node_destroy(node->u.for_stmt.inc);
            node_destroy(node->u.for_stmt.body);
            break;
        case NODE_RETURN:
        case NODE_EXPR_STMT:
            node_destroy(node->u.ret.value);
            break;
        case NODE_LABEL:
            node_destroy(node->u.label_stmt.stmt);
            break;
        case NODE_SWITCH:
            node_destroy(node->u.switch_node.cond);
            node_destroy(node->u.switch_node.body);
            break;
        case NODE_CASE:
        case NODE_DEFAULT:
            node_destroy(node->u.case_stmt.body);
            break;
        case NODE_BLOCK: {
            if (node->u.block.stmts) {
                for (int i = 0; i < node->u.block.nstmts; i++) {
                    node_destroy(node->u.block.stmts[i]);
                }
                free(node->u.block.stmts);
            }
            break;
        }
        case NODE_VAR_DECL:
            node_destroy(node->u.var_decl.init);
            break;
        case NODE_FUNC_DECL: {
            node_destroy(node->u.func_decl.body);
            if (node->u.func_decl.params) {
                free(node->u.func_decl.params);
            }
            break;
        }
        case NODE_INT_LIT:
        case NODE_CHAR_LIT:
        case NODE_STR_LIT:
        case NODE_IDENT:
        case NODE_BREAK:
        case NODE_CONTINUE:
        case NODE_GOTO:
        case NODE_TYPEDEF:
            /* No children to free */
            break;
    }

    free(node);
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
}

void node_dump(const Node *node, int indent) {
    if (!node) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }

    print_indent(indent);
    printf("%s (line %zu, col %zu", node_kind_name(node->kind), node->line, node->col);
    if (node->type_id != -1) {
        printf(", type_id=%d", node->type_id);
    }
    if (node->is_lvalue) {
        printf(", lvalue");
    }
    if (node->is_const_expr) {
        printf(", const");
    }
    printf(")\n");

    indent += 2;

    switch (node->kind) {
        case NODE_INT_LIT:
            print_indent(indent);
            printf("val: %lld\n", (long long)node->u.lit_int.val);
            break;
        case NODE_CHAR_LIT:
            print_indent(indent);
            printf("val: %lld\n", (long long)node->u.lit_char.val);
            break;
        case NODE_STR_LIT:
            print_indent(indent);
            printf("str: \"%s\" (len=%zu)\n", node->u.str_lit.str, node->u.str_lit.len);
            break;
        case NODE_IDENT:
            print_indent(indent);
            printf("name: \"%s\"\n", node->u.ident.name);
            break;
        case NODE_UNARY:
            print_indent(indent);
            printf("op: %s\n", unary_op_name(node->u.unary.op));
            node_dump(node->u.unary.operand, indent);
            break;
        case NODE_BINARY:
            print_indent(indent);
            printf("op: %s\n", binary_op_name(node->u.binary.op));
            node_dump(node->u.binary.left, indent);
            node_dump(node->u.binary.right, indent);
            break;
        case NODE_CALL:
            print_indent(indent);
            printf("nargs: %d\n", node->u.call.nargs);
            print_indent(indent);
            printf("callee:\n");
            node_dump(node->u.call.callee, indent);
            if (node->u.call.args) {
                for (int i = 0; i < node->u.call.nargs; i++) {
                    print_indent(indent);
                    printf("arg[%d]:\n", i);
                    node_dump(node->u.call.args[i], indent);
                }
            }
            break;
        case NODE_TERNARY:
            print_indent(indent);
            printf("cond:\n");
            node_dump(node->u.ternary.cond, indent);
            print_indent(indent);
            printf("then:\n");
            node_dump(node->u.ternary.then_e, indent);
            print_indent(indent);
            printf("else:\n");
            node_dump(node->u.ternary.else_e, indent);
            break;
        case NODE_ASSIGN:
            print_indent(indent);
            printf("op: %s\n", binary_op_name(node->u.assign.assign_op));
            print_indent(indent);
            printf("lvalue:\n");
            node_dump(node->u.assign.lvalue, indent);
            print_indent(indent);
            printf("rhs:\n");
            node_dump(node->u.assign.rhs, indent);
            break;
        case NODE_INDEX:
            print_indent(indent);
            printf("array:\n");
            node_dump(node->u.index.array, indent);
            print_indent(indent);
            printf("index:\n");
            node_dump(node->u.index.index, indent);
            break;
        case NODE_ADDR_OF:
        case NODE_DEREF:
        case NODE_SIZEOF:
            node_dump(node->u.unary.operand, indent);
            break;
        case NODE_CAST:
            print_indent(indent);
            printf("type:\n");
            node_dump(node->u.cast.type_expr, indent);
            print_indent(indent);
            printf("operand:\n");
            node_dump(node->u.cast.operand, indent);
            break;
        case NODE_IF:
            print_indent(indent);
            printf("cond:\n");
            node_dump(node->u.if_stmt.cond, indent);
            print_indent(indent);
            printf("then:\n");
            node_dump(node->u.if_stmt.then_body, indent);
            if (node->u.if_stmt.else_body) {
                print_indent(indent);
                printf("else:\n");
                node_dump(node->u.if_stmt.else_body, indent);
            }
            break;
        case NODE_WHILE:
        case NODE_DO_WHILE:
            print_indent(indent);
            printf("cond:\n");
            node_dump(node->u.loop.cond, indent);
            print_indent(indent);
            printf("body:\n");
            node_dump(node->u.loop.body, indent);
            break;
        case NODE_FOR:
            if (node->u.for_stmt.init) {
                print_indent(indent);
                printf("init:\n");
                node_dump(node->u.for_stmt.init, indent);
            }
            if (node->u.for_stmt.cond) {
                print_indent(indent);
                printf("cond:\n");
                node_dump(node->u.for_stmt.cond, indent);
            }
            if (node->u.for_stmt.inc) {
                print_indent(indent);
                printf("inc:\n");
                node_dump(node->u.for_stmt.inc, indent);
            }
            print_indent(indent);
            printf("body:\n");
            node_dump(node->u.for_stmt.body, indent);
            break;
        case NODE_RETURN:
            if (node->u.ret.value) {
                print_indent(indent);
                printf("value:\n");
                node_dump(node->u.ret.value, indent);
            }
            break;
        case NODE_GOTO:
            print_indent(indent);
            printf("label: \"%s\"\n", node->u.goto_stmt.label);
            break;
        case NODE_LABEL:
            print_indent(indent);
            printf("name: \"%s\"\n", node->u.label_stmt.name);
            node_dump(node->u.label_stmt.stmt, indent);
            break;
        case NODE_SWITCH:
            print_indent(indent);
            printf("cond:\n");
            node_dump(node->u.switch_node.cond, indent);
            print_indent(indent);
            printf("body:\n");
            node_dump(node->u.switch_node.body, indent);
            break;
        case NODE_CASE:
            print_indent(indent);
            printf("lo: %lld, hi: %lld\n", (long long)node->u.case_stmt.lo, (long long)node->u.case_stmt.hi);
            node_dump(node->u.case_stmt.body, indent);
            break;
        case NODE_DEFAULT:
            node_dump(node->u.case_stmt.body, indent);
            break;
        case NODE_EXPR_STMT:
            if (node->u.ret.value) {
                node_dump(node->u.ret.value, indent);
            }
            break;
        case NODE_BLOCK:
            if (node->u.block.stmts) {
                for (int i = 0; i < node->u.block.nstmts; i++) {
                    node_dump(node->u.block.stmts[i], indent);
                }
            }
            break;
        case NODE_VAR_DECL:
            print_indent(indent);
            printf("name: \"%s\", type_id: %d", node->u.var_decl.name, node->u.var_decl.type_id);
            if (node->u.var_decl.is_static) printf(", static");
            printf("\n");
            if (node->u.var_decl.init) {
                print_indent(indent);
                printf("init:\n");
                node_dump(node->u.var_decl.init, indent);
            }
            break;
        case NODE_FUNC_DECL:
            print_indent(indent);
            printf("name: \"%s\", ret_type_id: %d, nparams: %d\n",
                   node->u.func_decl.name, node->u.func_decl.ret_type_id, node->u.func_decl.nparams);
            if (node->u.func_decl.params) {
                for (int i = 0; i < node->u.func_decl.nparams; i++) {
                    print_indent(indent);
                    printf("param[%d]: \"%s\" (type_id=%d)\n",
                           i, node->u.func_decl.params[i].name,
                           node->u.func_decl.params[i].type_id);
                }
            }
            print_indent(indent);
            printf("body:\n");
            node_dump(node->u.func_decl.body, indent);
            break;
        case NODE_TYPEDEF:
            print_indent(indent);
            printf("old: \"%s\", new: \"%s\"\n", node->u.typedef_node.old_name, node->u.typedef_node.new_name);
            break;
        case NODE_BREAK:
        case NODE_CONTINUE:
            break;
    }
}

const char *node_kind_name(NodeKind kind) {
    switch (kind) {
        case NODE_INT_LIT:   return "NODE_INT_LIT";
        case NODE_CHAR_LIT:  return "NODE_CHAR_LIT";
        case NODE_STR_LIT:   return "NODE_STR_LIT";
        case NODE_IDENT:     return "NODE_IDENT";
        case NODE_UNARY:     return "NODE_UNARY";
        case NODE_BINARY:    return "NODE_BINARY";
        case NODE_CALL:      return "NODE_CALL";
        case NODE_TERNARY:   return "NODE_TERNARY";
        case NODE_ASSIGN:    return "NODE_ASSIGN";
        case NODE_INDEX:     return "NODE_INDEX";
        case NODE_ADDR_OF:   return "NODE_ADDR_OF";
        case NODE_DEREF:     return "NODE_DEREF";
        case NODE_CAST:      return "NODE_CAST";
        case NODE_SIZEOF:    return "NODE_SIZEOF";
        case NODE_BLOCK:     return "NODE_BLOCK";
        case NODE_IF:        return "NODE_IF";
        case NODE_WHILE:     return "NODE_WHILE";
        case NODE_DO_WHILE:  return "NODE_DO_WHILE";
        case NODE_FOR:       return "NODE_FOR";
        case NODE_RETURN:    return "NODE_RETURN";
        case NODE_BREAK:     return "NODE_BREAK";
        case NODE_CONTINUE:  return "NODE_CONTINUE";
        case NODE_GOTO:      return "NODE_GOTO";
        case NODE_LABEL:     return "NODE_LABEL";
        case NODE_SWITCH:    return "NODE_SWITCH";
        case NODE_CASE:      return "NODE_CASE";
        case NODE_DEFAULT:   return "NODE_DEFAULT";
        case NODE_EXPR_STMT: return "NODE_EXPR_STMT";
        case NODE_VAR_DECL:  return "NODE_VAR_DECL";
        case NODE_FUNC_DECL: return "NODE_FUNC_DECL";
        case NODE_TYPEDEF:   return "NODE_TYPEDEF";
    }
    return "NODE_UNKNOWN";
}

const char *unary_op_name(UnaryOp op) {
    switch (op) {
        case OP_NONE:   return "none";
        case U_NEG:     return "-";
        case U_POS:     return "+";
        case U_NOT:     return "!";
        case U_BITNOT:  return "~";
        case U_PREINC:  return "++";
        case U_PREDEC:  return "--";
        case U_POSTINC: return "++(post)";
        case U_POSTDEC: return "--(post)";
    }
    return "unknown_unary";
}

const char *binary_op_name(BinaryOp op) {
    switch (op) {
        case BIN_NONE:     return "none";
        case BIN_ADD:      return "+";
        case BIN_SUB:      return "-";
        case BIN_MUL:      return "*";
        case BIN_DIV:      return "/";
        case BIN_MOD:      return "%";
        case BIN_LSHL:     return "<<";
        case BIN_LSHR:     return ">>";
        case BIN_LT:       return "<";
        case BIN_GT:       return ">";
        case BIN_LEQ:      return "<=";
        case BIN_GEQ:      return ">=";
        case BIN_EQ:       return "==";
        case BIN_NEQ:      return "!=";
        case BIN_BAND:     return "&";
        case BIN_BXOR:     return "^";
        case BIN_BOR:      return "|";
        case BIN_ANDAND:   return "&&";
        case BIN_OROR:     return "||";
        case BIN_ASSIGN:   return "=";
        case BIN_PLUSEQ:   return "+=";
        case BIN_MINUSEQ:  return "-=";
        case BIN_STAREQ:   return "*=";
        case BIN_SLASHEQ:  return "/=";
        case BIN_PERCENTEQ: return "%=";
        case BIN_AMPREQ:   return "&=";
        case BIN_PIPEEQ:   return "|=";
        case BIN_CARETEQ:  return "^=";
        case BIN_LSHLEQ:   return "<<=";
        case BIN_LSHREQ:   return ">>=";
    }
    return "unknown_binary";
}
