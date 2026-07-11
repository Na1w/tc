#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- helpers ---- */

static void advance(Parser *p) {
    p->current = lexer_next(p->lexer);
}

static void expect(Parser *p, TokenKind kind) {
    if (p->current.kind != kind) {
        fprintf(stderr, "parse error: expected '%s' but got '%s' at line %zu, col %zu\n",
                token_kind_name(kind), token_kind_name(p->current.kind),
                p->current.line, p->current.col);
        p->error = 1;
    }
    advance(p);
}

static int is_type_qualifier(TokenKind kind) {
    return kind == TOK_KW_SIGNED || kind == TOK_KW_UNSIGNED
        || kind == TOK_KW_LONG || kind == TOK_KW_SHORT
        || kind == TOK_KW_CONST
        || kind == TOK_KW_STATIC || kind == TOK_KW_EXTERN
        || kind == TOK_KW_REGISTER;
}

static int is_base_type(TokenKind kind) {
    return kind == TOK_KW_INT || kind == TOK_KW_CHAR
        || kind == TOK_KW_VOID;
}

static int is_type_specifier(TokenKind kind) {
    return is_type_qualifier(kind) || is_base_type(kind)
        || kind == TOK_KW_TYPEDEF || kind == TOK_KW_STRUCT
        || kind == TOK_KW_UNION || kind == TOK_KW_ENUM;
}

/* ---- string pool ---- */

static void string_pool_add(Parser *p, const char *str) {
    if (p->string_count >= p->string_cap) {
        int new_cap = p->string_cap == 0 ? 16 : p->string_cap * 2;
        p->string_pool = (Node **)realloc(p->string_pool,
                                          (size_t)new_cap * sizeof(Node *));
        p->string_cap = new_cap;
    }
    p->string_pool[p->string_count++] = NULL;
    (void)str; /* stored in AST nodes, pool tracks count */
}

/* ---- Pratt parser ---- */

static int get_rbp(TokenKind kind) {
    switch (kind) {
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
            return 12;
        case TOK_PLUS:
        case TOK_MINUS:
            return 11;
        case TOK_LSHL:
        case TOK_RSHL:
            return 10;
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE:
            return 9;
        case TOK_EQEQ:
        case TOK_NE:
            return 8;
        case TOK_AMP:
            return 7;
        case TOK_CARET:
            return 6;
        case TOK_PIPE:
            return 5;
        case TOK_ANDAND:
            return 4;
        case TOK_OROR:
            return 3;
        case TOK_QUESTION:
            return 2;
        case TOK_EQ:
        case TOK_PLUSEQ:
        case TOK_MINEQ:
        case TOK_STAREQ:
        case TOK_SLASHEQ:
        case TOK_PERCENTEQ:
        case TOK_AMPREQ:
        case TOK_PIPEEQ:
        case TOK_CARETEQ:
        case TOK_LSHLEQ:
        case TOK_RSHLEQ:
            return 1;
        default:
            return 0;
    }
}

/* Forward declarations */
static Node* parse_expr(Parser *p, int min_prec);
static Node* parse_prefix(Parser *p);
static Node* parse_infix(Parser *p, Node *left, int prec);
static void parse_postfix(Parser *p, Node **node);
static Node* parse_declaration(Parser *p);
static Node* parse_statement(Parser *p);
static Node* parse_compound_stmt(Parser *p);

/* ---- Pratt parsing ---- */

static Node* parse_expr(Parser *p, int min_prec) {
    Node *left = parse_prefix(p);
    if (!left) return NULL;

    while (1) {
        /* Handle postfix operators */
        parse_postfix(p, &left);
        if (!left) return NULL;

        int prec = get_rbp(p->current.kind);
        if (prec < min_prec || prec == 0) break;

        /* Handle ternary */
        if (p->current.kind == TOK_QUESTION) {
            if (min_prec <= 2) {
                Node *tern = node_create(NODE_TERNARY, left->line, left->col);
                tern->u.ternary.cond = left;
                advance(p); /* consume ? */
                tern->u.ternary.then_e = parse_expr(p, 3);
                if (tern->u.ternary.then_e) {
                    tern->u.ternary.then_e->is_lvalue = 0;
                }
                if (p->current.kind == TOK_COLON) {
                    advance(p); /* consume : */
                }
                tern->u.ternary.else_e = parse_expr(p, 3);
                if (tern->u.ternary.else_e) {
                    tern->u.ternary.else_e->is_lvalue = 0;
                }
                tern->is_lvalue = 0;
                left = tern;
                continue;
            }
            break;
        }

        left = parse_infix(p, left, prec);
        if (!left) return NULL;
    }

    return left;
}

static Node* parse_prefix(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;

    switch (p->current.kind) {
        case TOK_BANG: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_UNARY, line, col);
            n->u.unary.op = U_NOT;
            n->u.unary.operand = operand;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_TILDE: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_UNARY, line, col);
            n->u.unary.op = U_BITNOT;
            n->u.unary.operand = operand;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_MINUS: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_UNARY, line, col);
            n->u.unary.op = U_NEG;
            n->u.unary.operand = operand;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_PLUS: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_UNARY, line, col);
            n->u.unary.op = U_POS;
            n->u.unary.operand = operand;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_AMP: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_ADDR_OF, line, col);
            n->u.unary.operand = operand;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_STAR: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_DEREF, line, col);
            n->u.unary.operand = operand;
            n->is_lvalue = 1;
            return n;
        }
        case TOK_INCR: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_UNARY, line, col);
            n->u.unary.op = U_PREINC;
            n->u.unary.operand = operand;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_DECR: {
            advance(p);
            Node *operand = parse_expr(p, 15);
            Node *n = node_create(NODE_UNARY, line, col);
            n->u.unary.op = U_PREDEC;
            n->u.unary.operand = operand;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_KW_SIZEOF: {
            advance(p);
            Node *n = node_create(NODE_SIZEOF, line, col);
            n->is_lvalue = 0;
            if (p->current.kind == TOK_LPAREN) {
                advance(p);
                /* Handle sizeof(type) where type is a keyword like int, char, void */
                if (is_base_type(p->current.kind) || is_type_qualifier(p->current.kind)) {
                    /* Parse as a type name, not an expression */
                    Node *type_node = node_create(NODE_IDENT, p->current.line, p->current.col);
                    type_node->u.ident.name = p->current.ident;
                    type_node->u.ident.hash = p->current.id_hash;
                    advance(p);
                    /* Consume additional type qualifiers if present */
                    while (is_type_qualifier(p->current.kind)) {
                        advance(p);
                    }
                    n->u.sizeof_node.operand = type_node;
                } else {
                    n->u.sizeof_node.operand = parse_expr(p, 0);
                }
                if (p->current.kind == TOK_RPAREN) {
                    advance(p);
                }
            } else {
                n->u.sizeof_node.operand = parse_expr(p, 15);
            }
            return n;
        }
        case TOK_LPAREN: {
            /* Could be cast or parenthesized expression */
            advance(p);
            Token save = p->current;
            if (p->current.kind == TOK_IDENT) {
                advance(p);
                if (p->current.kind == TOK_RPAREN) {
                    /* This is a cast: (type) expr */
                    const char *type_name = save.ident;
                    advance(p); /* consume ) */
                    Node *operand = parse_expr(p, 15);
                    Node *type_node = node_create(NODE_IDENT, line, col);
                    type_node->u.ident.name = type_name;
                    type_node->u.ident.hash = save.id_hash;
                    Node *n = node_create(NODE_CAST, line, col);
                    n->u.cast.type_expr = type_node;
                    n->u.cast.operand = operand;
                    n->is_lvalue = 0;
                    return n;
                }
                /* Not a cast, it's a parenthesized expression */
                lexer_unget(p->lexer, p->current);
                p->current = save;
            }
            Node *expr = parse_expr(p, 0);
            if (p->current.kind == TOK_RPAREN) {
                advance(p);
            }
            return expr;
        }
        case TOK_INT_LIT: {
            int64_t val = p->current.ival;
            advance(p);
            Node *n = node_create(NODE_INT_LIT, line, col);
            n->u.lit_int.val = val;
            n->const_val = val;
            n->is_const_expr = 1;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_CHAR_LIT: {
            int64_t val = p->current.ival;
            advance(p);
            Node *n = node_create(NODE_CHAR_LIT, line, col);
            n->u.lit_char.val = val;
            n->const_val = val;
            n->is_const_expr = 1;
            n->is_lvalue = 0;
            return n;
        }
        case TOK_STRING_LIT: {
            const char *str = p->current.ident;
            size_t len = (size_t)p->current.ival;
            if (len == 0 && str) {
                len = strlen(str);
            }
            advance(p);
            Node *n = node_create(NODE_STR_LIT, line, col);
            n->u.str_lit.str = str;
            n->u.str_lit.len = len;
            n->is_lvalue = 0;
            string_pool_add(p, str);
            return n;
        }
        case TOK_IDENT: {
            const char *name = p->current.ident;
            uint32_t hash = p->current.id_hash;
            advance(p);
            Node *n = node_create(NODE_IDENT, line, col);
            n->u.ident.name = name;
            n->u.ident.hash = hash;
            n->is_lvalue = 1;
            return n;
        }
        default:
            fprintf(stderr, "parse error: unexpected token '%s' in expression at line %zu, col %zu\n",
                    token_kind_name(p->current.kind), p->current.line, p->current.col);
            p->error = 1;
            return NULL;
    }
}

static Node* parse_infix(Parser *p, Node *left, int prec) {
    BinaryOp op = BIN_NONE;
    TokenKind tok = p->current.kind;
    size_t line = p->current.line;
    size_t col = p->current.col;

    /* Check if this is an assignment (must be before the switch) */
    if (tok == TOK_EQ || tok == TOK_PLUSEQ || tok == TOK_MINEQ ||
        tok == TOK_STAREQ || tok == TOK_SLASHEQ || tok == TOK_PERCENTEQ ||
        tok == TOK_AMPREQ || tok == TOK_PIPEEQ || tok == TOK_CARETEQ ||
        tok == TOK_LSHLEQ || tok == TOK_RSHLEQ) {
        BinaryOp assign_op = BIN_NONE;
        switch (tok) {
            case TOK_EQ:        assign_op = BIN_ASSIGN; break;
            case TOK_PLUSEQ:    assign_op = BIN_PLUSEQ; break;
            case TOK_MINEQ:     assign_op = BIN_MINUSEQ; break;
            case TOK_STAREQ:    assign_op = BIN_STAREQ; break;
            case TOK_SLASHEQ:   assign_op = BIN_SLASHEQ; break;
            case TOK_PERCENTEQ: assign_op = BIN_PERCENTEQ; break;
            case TOK_AMPREQ:    assign_op = BIN_AMPREQ; break;
            case TOK_PIPEEQ:    assign_op = BIN_PIPEEQ; break;
            case TOK_CARETEQ:   assign_op = BIN_CARETEQ; break;
            case TOK_LSHLEQ:    assign_op = BIN_LSHLEQ; break;
            case TOK_RSHLEQ:    assign_op = BIN_LSHREQ; break;
            default: break;
        }
        advance(p);
        Node *rhs = parse_expr(p, 1); /* right associative */
        Node *n = node_create(NODE_ASSIGN, line, col);
        n->u.assign.lvalue = left;
        n->u.assign.rhs = rhs;
        n->u.assign.assign_op = assign_op;
        n->is_lvalue = 0;
        return n;
    }

    switch (tok) {
        case TOK_STAR:    op = BIN_MUL; break;
        case TOK_SLASH:   op = BIN_DIV; break;
        case TOK_PERCENT: op = BIN_MOD; break;
        case TOK_PLUS:    op = BIN_ADD; break;
        case TOK_MINUS:   op = BIN_SUB; break;
        case TOK_LSHL:    op = BIN_LSHL; break;
        case TOK_RSHL:    op = BIN_LSHR; break;
        case TOK_LT:      op = BIN_LT; break;
        case TOK_LE:      op = BIN_LEQ; break;
        case TOK_GT:      op = BIN_GT; break;
        case TOK_GE:      op = BIN_GEQ; break;
        case TOK_EQEQ:    op = BIN_EQ; break;
        case TOK_NE:      op = BIN_NEQ; break;
        case TOK_AMP:     op = BIN_BAND; break;
        case TOK_CARET:   op = BIN_BXOR; break;
        case TOK_PIPE:    op = BIN_BOR; break;
        case TOK_ANDAND:  op = BIN_ANDAND; break;
        case TOK_OROR:    op = BIN_OROR; break;
        default:
            return NULL;
    }

    advance(p);

    /* Left-associative: parse right operand with prec+1 */
    Node *right = parse_expr(p, prec + 1);

    Node *n = node_create(NODE_BINARY, line, col);
    n->u.binary.op = op;
    n->u.binary.left = left;
    n->u.binary.right = right;
    n->is_lvalue = 0;
    return n;
}

static void parse_postfix(Parser *p, Node **node) {
    while (1) {
        switch (p->current.kind) {
            case TOK_LPAREN: {
                /* Function call */
                Node *call = node_create(NODE_CALL, (*node)->line, (*node)->col);
                call->u.call.callee = *node;
                advance(p); /* consume ( */
                call->u.call.nargs = 0;
                call->u.call.args = NULL;
                call->is_lvalue = 0;

                if (p->current.kind != TOK_RPAREN) {
                    Node *arg = parse_expr(p, 0);
                    if (arg) {
                        call->u.call.args = (Node **)malloc(sizeof(Node *) * 8);
                        call->u.call.nargs = 1;
                        call->u.call.args[0] = arg;
                        while (p->current.kind == TOK_COMMA) {
                            advance(p);
                            int nargs = call->u.call.nargs;
                            call->u.call.args = (Node **)realloc(call->u.call.args,
                                sizeof(Node *) * (size_t)(nargs + 8));
                            arg = parse_expr(p, 0);
                            call->u.call.args[nargs] = arg;
                            call->u.call.nargs++;
                        }
                    }
                }
                if (p->current.kind == TOK_RPAREN) {
                    advance(p);
                }
                *node = call;
                break;
            }
            case TOK_LBRACKET: {
                /* Array index */
                Node *idx = node_create(NODE_INDEX, (*node)->line, (*node)->col);
                idx->u.index.array = *node;
                advance(p); /* consume [ */
                idx->u.index.index = parse_expr(p, 0);
                if (p->current.kind == TOK_RBRACKET) {
                    advance(p);
                }
                idx->is_lvalue = 1;
                *node = idx;
                break;
            }
            case TOK_DOT: {
                /* Struct member access */
                advance(p); /* consume . */
                if (p->current.kind != TOK_IDENT) {
                    fprintf(stderr, "parse error: expected identifier after '.' at line %zu, col %zu\n",
                            p->current.line, p->current.col);
                    p->error = 1;
                    return;
                }
                const char *member = p->current.ident;
                uint32_t mhash = p->current.id_hash;
                advance(p);
                Node *n = node_create(NODE_IDENT, (*node)->line, (*node)->col);
                n->u.ident.name = member;
                n->u.ident.hash = mhash;
                n->is_lvalue = 1;
                Node *access = node_create(NODE_BINARY, (*node)->line, (*node)->col);
                access->u.binary.op = BIN_ADD; /* placeholder */
                access->u.binary.left = *node;
                access->u.binary.right = n;
                access->is_lvalue = 1;
                *node = access;
                break;
            }
            case TOK_ARROW: {
                /* Pointer member access */
                advance(p); /* consume -> */
                if (p->current.kind != TOK_IDENT) {
                    fprintf(stderr, "parse error: expected identifier after '->' at line %zu, col %zu\n",
                            p->current.line, p->current.col);
                    p->error = 1;
                    return;
                }
                const char *member = p->current.ident;
                uint32_t mhash = p->current.id_hash;
                advance(p);
                Node *n = node_create(NODE_IDENT, (*node)->line, (*node)->col);
                n->u.ident.name = member;
                n->u.ident.hash = mhash;
                n->is_lvalue = 1;
                Node *access = node_create(NODE_BINARY, (*node)->line, (*node)->col);
                access->u.binary.op = BIN_SUB; /* placeholder */
                access->u.binary.left = *node;
                access->u.binary.right = n;
                access->is_lvalue = 1;
                *node = access;
                break;
            }
            case TOK_INCR: {
                advance(p);
                Node *n = node_create(NODE_UNARY, (*node)->line, (*node)->col);
                n->u.unary.op = U_POSTINC;
                n->u.unary.operand = *node;
                n->is_lvalue = 0;
                *node = n;
                break;
            }
            case TOK_DECR: {
                advance(p);
                Node *n = node_create(NODE_UNARY, (*node)->line, (*node)->col);
                n->u.unary.op = U_POSTDEC;
                n->u.unary.operand = *node;
                n->is_lvalue = 0;
                *node = n;
                break;
            }
            default:
                return; /* No more postfix */
        }
    }
}

/* ---- Type parsing ---- */

static int parse_type_spec(Parser *p) {
    /* Consume any type qualifiers */
    while (is_type_qualifier(p->current.kind) || p->current.kind == TOK_KW_CONST) {
        advance(p);
    }
    /* Consume base type */
    if (is_base_type(p->current.kind)) {
        advance(p);
    } else if (p->current.kind == TOK_KW_STRUCT || p->current.kind == TOK_KW_UNION
               || p->current.kind == TOK_KW_ENUM) {
        advance(p);
        if (p->current.kind == TOK_IDENT) {
            advance(p);
        }
    }
    return -1; /* unresolved type, sema will handle */
}

/* ---- Statement parsing ---- */

static Node* parse_compound_stmt(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;
    expect(p, TOK_LBRACE);

    Node **stmts = NULL;
    int nstmts = 0;
    int cap = 8;
    stmts = (Node **)malloc(sizeof(Node *) * (size_t)cap);

    while (p->current.kind != TOK_RBRACE && p->current.kind != TOK_EOF) {
        if (nstmts >= cap) {
            cap *= 2;
            stmts = (Node **)realloc(stmts, sizeof(Node *) * (size_t)cap);
        }
        stmts[nstmts++] = parse_statement(p);
    }

    expect(p, TOK_RBRACE);

    Node *n = node_create(NODE_BLOCK, line, col);
    n->u.block.stmts = stmts;
    n->u.block.nstmts = nstmts;
    return n;
}

static Node* parse_if_stmt(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;
    advance(p); /* consume if */
    expect(p, TOK_LPAREN);
    Node *cond = parse_expr(p, 0);
    expect(p, TOK_RPAREN);
    Node *then_body = parse_statement(p);
    Node *else_body = NULL;
    if (p->current.kind == TOK_KW_ELSE) {
        advance(p);
        else_body = parse_statement(p);
    }
    Node *n = node_create(NODE_IF, line, col);
    n->u.if_stmt.cond = cond;
    n->u.if_stmt.then_body = then_body;
    n->u.if_stmt.else_body = else_body;
    return n;
}

static Node* parse_while_stmt(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;
    advance(p); /* consume while */
    expect(p, TOK_LPAREN);
    Node *cond = parse_expr(p, 0);
    expect(p, TOK_RPAREN);
    Node *body = parse_statement(p);
    Node *n = node_create(NODE_WHILE, line, col);
    n->u.loop.cond = cond;
    n->u.loop.body = body;
    return n;
}

static Node* parse_do_while_stmt(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;
    advance(p); /* consume do */
    Node *body = parse_statement(p);
    expect(p, TOK_KW_WHILE);
    expect(p, TOK_LPAREN);
    Node *cond = parse_expr(p, 0);
    expect(p, TOK_RPAREN);
    expect(p, TOK_SEMI);
    Node *n = node_create(NODE_DO_WHILE, line, col);
    n->u.loop.cond = cond;
    n->u.loop.body = body;
    return n;
}

static Node* parse_for_stmt(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;
    advance(p); /* consume for */
    expect(p, TOK_LPAREN);

    Node *init = NULL;
    Node *cond = NULL;
    Node *inc = NULL;

    /* Check if init is a declaration */
    if (is_type_specifier(p->current.kind)) {
        init = parse_declaration(p);
    } else if (p->current.kind != TOK_SEMI) {
        init = parse_expr(p, 0);
    }
    if (p->current.kind == TOK_SEMI) {
        advance(p);
    }

    if (p->current.kind != TOK_SEMI) {
        cond = parse_expr(p, 0);
    }
    if (p->current.kind == TOK_SEMI) {
        advance(p);
    }

    if (p->current.kind != TOK_RPAREN) {
        inc = parse_expr(p, 0);
    }
    expect(p, TOK_RPAREN);

    Node *body = parse_statement(p);

    Node *n = node_create(NODE_FOR, line, col);
    n->u.for_stmt.init = init;
    n->u.for_stmt.cond = cond;
    n->u.for_stmt.inc = inc;
    n->u.for_stmt.body = body;
    return n;
}

static Node* parse_return_stmt(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;
    advance(p); /* consume return */
    Node *value = NULL;
    if (p->current.kind != TOK_SEMI) {
        value = parse_expr(p, 0);
    }
    expect(p, TOK_SEMI);
    Node *n = node_create(NODE_RETURN, line, col);
    n->u.ret.value = value;
    return n;
}

static Node* parse_switch_stmt(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;
    advance(p); /* consume switch */
    expect(p, TOK_LPAREN);
    Node *cond = parse_expr(p, 0);
    expect(p, TOK_RPAREN);
    Node *body = parse_statement(p);
    Node *n = node_create(NODE_SWITCH, line, col);
    n->u.switch_node.cond = cond;
    n->u.switch_node.body = body;
    return n;
}

static Node* parse_statement(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;

    if (p->current.kind == TOK_EOF) {
        Node *n = node_create(NODE_BLOCK, line, col);
        n->u.block.stmts = NULL;
        n->u.block.nstmts = 0;
        return n;
    }

    if (p->current.kind == TOK_LBRACE) {
        return parse_compound_stmt(p);
    }

    if (p->current.kind == TOK_KW_IF) {
        return parse_if_stmt(p);
    }

    if (p->current.kind == TOK_KW_WHILE) {
        return parse_while_stmt(p);
    }

    if (p->current.kind == TOK_KW_DO) {
        return parse_do_while_stmt(p);
    }

    if (p->current.kind == TOK_KW_FOR) {
        return parse_for_stmt(p);
    }

    if (p->current.kind == TOK_KW_RETURN) {
        return parse_return_stmt(p);
    }

    if (p->current.kind == TOK_KW_SWITCH) {
        return parse_switch_stmt(p);
    }

    if (p->current.kind == TOK_KW_BREAK) {
        advance(p);
        expect(p, TOK_SEMI);
        return node_create(NODE_BREAK, line, col);
    }

    if (p->current.kind == TOK_KW_CONTINUE) {
        advance(p);
        expect(p, TOK_SEMI);
        return node_create(NODE_CONTINUE, line, col);
    }

    if (p->current.kind == TOK_KW_GOTO) {
        advance(p);
        if (p->current.kind != TOK_IDENT) {
            fprintf(stderr, "parse error: expected identifier after 'goto' at line %zu, col %zu\n",
                    p->current.line, p->current.col);
            p->error = 1;
            return NULL;
        }
        const char *label = p->current.ident;
        advance(p);
        expect(p, TOK_SEMI);
        Node *n = node_create(NODE_GOTO, line, col);
        n->u.goto_stmt.label = label;
        return n;
    }

    /* Check for label: IDENT ':' */
    if (p->current.kind == TOK_IDENT) {
        Token next = lexer_next(p->lexer);
        if (next.kind == TOK_COLON) {
            const char *label = p->current.ident;
            advance(p); /* consume IDENT */
            advance(p); /* consume : */
            Node *stmt = parse_statement(p);
            Node *n = node_create(NODE_LABEL, line, col);
            n->u.label_stmt.name = label;
            n->u.label_stmt.stmt = stmt;
            return n;
        }
        lexer_unget(p->lexer, next);
    }

    /* Check for case/default inside switch */
    if (p->current.kind == TOK_KW_CASE) {
        advance(p);
        Node *lo_expr = parse_expr(p, 0);
        int64_t lo = 0;
        int64_t hi = 0;
        if (lo_expr && lo_expr->kind == NODE_INT_LIT) {
            lo = lo_expr->u.lit_int.val;
        }
        if (p->current.kind == TOK_COMMA) {
            advance(p);
            Node *hi_expr = parse_expr(p, 0);
            if (hi_expr && hi_expr->kind == NODE_INT_LIT) {
                hi = hi_expr->u.lit_int.val;
            }
        }
        expect(p, TOK_COLON);
        Node *body = parse_statement(p);
        Node *n = node_create(NODE_CASE, line, col);
        n->u.case_stmt.lo = lo;
        n->u.case_stmt.hi = hi;
        n->u.case_stmt.body = body;
        return n;
    }

    if (p->current.kind == TOK_KW_DEFAULT) {
        advance(p);
        expect(p, TOK_COLON);
        Node *body = parse_statement(p);
        Node *n = node_create(NODE_DEFAULT, line, col);
        n->u.case_stmt.body = body;
        return n;
    }

    /* Check for declaration */
    if (is_type_specifier(p->current.kind)) {
        return parse_declaration(p);
    }

    /* Expression statement or empty statement */
    if (p->current.kind == TOK_SEMI) {
        advance(p);
        Node *n = node_create(NODE_EXPR_STMT, line, col);
        n->u.ret.value = NULL;
        return n;
    }

    /* Expression statement */
    Node *expr = parse_expr(p, 0);
    if (!expr && p->error) {
        /* parse_expr failed and didn't advance; skip one token to avoid infinite loop */
        if (p->current.kind != TOK_EOF) {
            advance(p);
        }
        Node *n = node_create(NODE_EXPR_STMT, line, col);
        n->u.ret.value = NULL;
        return n;
    }
    if (p->current.kind == TOK_SEMI) {
        advance(p);
    }
    Node *n = node_create(NODE_EXPR_STMT, line, col);
    n->u.ret.value = expr;
    return n;
}

/* ---- Declaration parsing ---- */

static Node* parse_declaration(Parser *p) {
    size_t line = p->current.line;
    size_t col = p->current.col;

    /* Check for typedef */
    int is_typedef = (p->current.kind == TOK_KW_TYPEDEF);
    if (is_typedef) {
        advance(p);
    }

    int type_id = parse_type_spec(p);
    int ptr_stars = 0;
    while (p->current.kind == TOK_STAR) {
        advance(p);
        ptr_stars++;
    }

    if (p->current.kind != TOK_IDENT) {
        fprintf(stderr, "parse error: expected identifier in declaration at line %zu, col %zu\n",
                p->current.line, p->current.col);
        p->error = 1;
        return NULL;
    }

    const char *name = p->current.ident;
    uint32_t hash = p->current.id_hash;
    advance(p);

    /* Check for array declarator: [expr] */
    if (p->current.kind == TOK_LBRACKET) {
        advance(p);
        parse_expr(p, 0);
        if (p->current.kind == TOK_RBRACKET) {
            advance(p);
        }
    }

    /* Check if this is a function definition: IDENT ( params ) { body } */
    if (p->current.kind == TOK_LPAREN) {
        advance(p); /* consume ( */
        ParamDecl *params = NULL;
        int nparams = 0;
        int is_variadic = 0;
        int pcap = 4;
        params = (ParamDecl *)malloc(sizeof(ParamDecl) * (size_t)pcap);

        if (p->current.kind != TOK_RPAREN) {
            while (1) {
                if (p->current.kind == TOK_KW_VOID) {
                    advance(p);
                    break;
                }
                // Check for variadic (...) as the last parameter
                if (p->current.kind == TOK_ELLIPSIS) {
                    is_variadic = 1;
                    advance(p);
                    break;
                }
                int param_type = parse_type_spec(p);
                /* Consume pointer stars on param */
                while (p->current.kind == TOK_STAR) {
                    advance(p);
                }

                if (p->current.kind != TOK_IDENT) {
                    fprintf(stderr, "parse error: expected parameter name at line %zu, col %zu\n",
                            p->current.line, p->current.col);
                    p->error = 1;
                    break;
                }
                const char *param_name = p->current.ident;
                uint32_t param_hash = p->current.id_hash;
                advance(p);

                /* Skip array brackets in param */
                if (p->current.kind == TOK_LBRACKET) {
                    advance(p);
                    if (p->current.kind == TOK_RBRACKET) advance(p);
                }

                if (nparams >= pcap) {
                    pcap *= 2;
                    params = (ParamDecl *)realloc(params, sizeof(ParamDecl) * (size_t)pcap);
                }
                params[nparams].name = param_name;
                params[nparams].hash = param_hash;
                params[nparams].type_id = param_type;
                nparams++;

                if (p->current.kind == TOK_COMMA) {
                    advance(p);
                } else {
                    break;
                }
            }
        }
        expect(p, TOK_RPAREN);

        /* Check for function body */
        if (p->current.kind == TOK_LBRACE) {
            Node *body = parse_compound_stmt(p);
            Node *n = node_create(NODE_FUNC_DECL, line, col);
            n->u.func_decl.name = name;
            n->u.func_decl.ret_type_id = type_id;
            n->u.func_decl.params = params;
            n->u.func_decl.nparams = nparams;
            n->u.func_decl.is_variadic = is_variadic;
            n->u.func_decl.body = body;
            (void)hash;
            (void)ptr_stars;
            return n;
        }

        /* Forward declaration - no body */
        free(params);
        if (p->current.kind == TOK_SEMI) {
            advance(p); /* consume ; */
        }
        Node *n = node_create(NODE_FUNC_DECL, line, col);
        n->u.func_decl.name = name;
        n->u.func_decl.ret_type_id = type_id;
        n->u.func_decl.params = NULL;
        n->u.func_decl.nparams = 0;
        n->u.func_decl.is_variadic = is_variadic;
        n->u.func_decl.body = NULL;
        (void)hash;
        (void)ptr_stars;
        return n;
    }

    /* Variable declaration */
    Node *init = NULL;
    int is_static = 0;

    if (p->current.kind == TOK_EQ) {
        advance(p);
        init = parse_expr(p, 0);
    }

    expect(p, TOK_SEMI);

    if (is_typedef) {
        Node *n = node_create(NODE_TYPEDEF, line, col);
        n->u.typedef_node.old_name = name;
        n->u.typedef_node.new_name = name;
        (void)hash;
        (void)ptr_stars;
        (void)type_id;
        (void)init;
        return n;
    }

    Node *n = node_create(NODE_VAR_DECL, line, col);
    n->u.var_decl.name = name;
    n->u.var_decl.type_id = type_id;
    n->u.var_decl.init = init;
    n->u.var_decl.is_static = is_static;
    (void)hash;
    (void)ptr_stars;
    return n;
}

/* ---- Program parsing ---- */

Node* parse_program(Parser *parser) {
    Node **stmts = NULL;
    int nstmts = 0;
    int cap = 8;
    stmts = (Node **)malloc(sizeof(Node *) * (size_t)cap);

    while (parser->current.kind != TOK_EOF) {
        if (nstmts >= cap) {
            cap *= 2;
            stmts = (Node **)realloc(stmts, sizeof(Node *) * (size_t)cap);
        }
        stmts[nstmts++] = parse_statement(parser);
        if (parser->error) break;
    }

    Node *n = node_create(NODE_BLOCK, 1, 1);
    n->u.block.stmts = stmts;
    n->u.block.nstmts = nstmts;
    return n;
}

/* ---- Error reporting ---- */

const char* parser_error(Parser *p, const char *fmt, ...) {
    static char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, "parse error: %s\n", buf);
    p->error = 1;
    return buf;
}

/* ---- Parser lifecycle ---- */

Parser* parser_create(Lexer *lex) {
    Parser *p = (Parser *)calloc(1, sizeof(Parser));
    if (!p) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    p->lexer = lex;
    p->current = lexer_next(lex);
    p->error = 0;
    p->string_pool = NULL;
    p->string_count = 0;
    p->string_cap = 0;
    return p;
}

void parser_destroy(Parser *parser) {
    if (!parser) return;
    if (parser->string_pool) {
        free(parser->string_pool);
    }
    free(parser);
}
