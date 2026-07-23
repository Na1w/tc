#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* String pool entry for decoded strings */
typedef struct PoolStr {
    struct PoolStr *next;
    char data[1];
} PoolStr;

struct Lexer {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    Token ungot;
    int has_unget;
    PoolStr *pool;
};

/* ---- helpers ---- */

static uint32_t djb2(const char *s, size_t len) {
    uint32_t h = 5381;
    for (size_t i = 0; i < len; i++) {
        h = h * 33 + (unsigned char)s[i];
    }
    return h;
}

static int is_ident_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int is_hex_digit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int is_octal_digit(int c) {
    return c >= '0' && c <= '7';
}

static int peek(Lexer *lex) {
    return lex->pos < lex->len ? (unsigned char)lex->src[lex->pos] : 0;
}

static int advance(Lexer *lex) {
    int c = (unsigned char)lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return c;
}

static Token make_token_at(Lexer *lex __attribute__((unused)), TokenKind kind, size_t line, size_t col) {
    Token tok;
    tok.kind = kind;
    tok.line = line;
    tok.col = col;
    tok.ival = 0;
    tok.fval = 0.0;
    tok.ident = NULL;
    tok.id_hash = 0;
    return tok;
}

static void skip_ws(Lexer *lex) {
    while (1) {
        int c = peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
        } else if (c == '/') {
            int n1 = (unsigned char)lex->src[lex->pos + 1];
            if (n1 == '/') {
                lex->pos += 2;
                while (lex->pos < lex->len && (unsigned char)lex->src[lex->pos] != '\n')
                    lex->pos++;
            } else if (n1 == '*') {
                lex->pos += 2;
                while (lex->pos + 1 < lex->len) {
                    if (lex->src[lex->pos] == '*' && lex->src[lex->pos + 1] == '/') {
                        lex->pos += 2;
                        break;
                    }
                    if (lex->src[lex->pos] == '\n') { lex->line++; lex->col = 1; }
                    lex->pos++;
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

static char *pool_alloc(Lexer *lex, const char *s, size_t len) {
    PoolStr *ps = (PoolStr *)malloc(sizeof(PoolStr) + len);
    if (!ps) { fprintf(stderr, "error: out of memory\n"); exit(1); }
    memcpy(ps->data, s, len);
    ps->data[len] = '\0';
    ps->next = lex->pool;
    lex->pool = ps;
    return ps->data;
}

/* ---- keyword table ---- */

typedef struct { const char *name; TokenKind kind; } KeywordEntry;

static const KeywordEntry KW_TABLE[] = {
    { "auto",     TOK_KW_AUTO },     { "break",    TOK_KW_BREAK },
    { "case",     TOK_KW_CASE },     { "char",     TOK_KW_CHAR },
    { "const",    TOK_KW_CONST },    { "continue", TOK_KW_CONTINUE },
    { "default",  TOK_KW_DEFAULT },  { "do",       TOK_KW_DO },
    { "else",     TOK_KW_ELSE },     { "enum",     TOK_KW_ENUM },
    { "extern",   TOK_KW_EXTERN },   { "for",      TOK_KW_FOR },
    { "goto",     TOK_KW_GOTO },     { "if",       TOK_KW_IF },
    { "int",      TOK_KW_INT },      { "long",     TOK_KW_LONG },
    { "register", TOK_KW_REGISTER }, { "return",   TOK_KW_RETURN },
    { "short",    TOK_KW_SHORT },    { "signed",   TOK_KW_SIGNED },
    { "sizeof",   TOK_KW_SIZEOF },   { "static",   TOK_KW_STATIC },
    { "struct",   TOK_KW_STRUCT },   { "switch",   TOK_KW_SWITCH },
    { "typedef",  TOK_KW_TYPEDEF },  { "typeof",   TOK_KW_TYPEOF },
    { "union",    TOK_KW_UNION },    { "unsigned", TOK_KW_UNSIGNED },
    { "void",     TOK_KW_VOID },     { "while",    TOK_KW_WHILE },
};

#define KW_COUNT (sizeof(KW_TABLE) / sizeof(KW_TABLE[0]))

static TokenKind lookup_keyword(const char *name, size_t len) {
    for (size_t i = 0; i < KW_COUNT; i++) {
        size_t klen = strlen(KW_TABLE[i].name);
        if (klen == len && memcmp(name, KW_TABLE[i].name, len) == 0)
            return KW_TABLE[i].kind;
    }
    return TOK_IDENT;
}

/* ---- literal parsing ---- */

static int64_t parse_int_literal(const char *s, size_t len) {
    int base = 10;
    if (len >= 2 && s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') base = 16;
        else base = 8;
    }
    int64_t val = 0;
    for (size_t i = 0; i < len; i++) {
        int c = s[i];
        if (base == 16) {
            if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
        } else if (base == 8) {
            val = val * 8 + (c - '0');
        } else {
            val = val * 10 + (c - '0');
        }
    }
    return val;
}

static void parse_escape(Lexer *lex, int *out_ch) {
    int c = advance(lex);
    if (c == 'n')  *out_ch = '\n';
    else if (c == 't') *out_ch = '\t';
    else if (c == 'r') *out_ch = '\r';
    else if (c == 'b') *out_ch = '\b';
    else if (c == 'f') *out_ch = '\f';
    else if (c == 'v') *out_ch = '\v';
    else if (c == 'a') *out_ch = '\a';
    else if (c == '\\') *out_ch = '\\';
    else if (c == '\'') *out_ch = '\'';
    else if (c == '"') *out_ch = '"';
    else if (c == 'x') {
        int val = 0;
        while (lex->pos < lex->len && is_hex_digit(peek(lex)))
            val = val * 16 + (advance(lex) - '0');
        *out_ch = val;
    } else if (is_octal_digit(c)) {
        int val = c - '0';
        int count = 1;
        while (count < 3 && lex->pos < lex->len && is_octal_digit(peek(lex))) {
            val = val * 8 + (advance(lex) - '0');
            count++;
        }
        *out_ch = val;
    } else {
        *out_ch = c;
    }
}

/* ---- public API ---- */

Lexer *lexer_create(const char *source, size_t len) {
    Lexer *lex = (Lexer *)calloc(1, sizeof(Lexer));
    if (!lex) { fprintf(stderr, "error: out of memory\n"); exit(1); }
    lex->src = source;
    lex->len = len;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->has_unget = 0;
    lex->pool = NULL;
    return lex;
}

void lexer_destroy(Lexer *lex) {
    if (!lex) return;
    PoolStr *ps = lex->pool;
    while (ps) { PoolStr *n = ps->next; free(ps); ps = n; }
    free(lex);
}

Token lexer_next(Lexer *lex) {
    if (lex->has_unget) { lex->has_unget = 0; return lex->ungot; }
    skip_ws(lex);
    if (lex->pos >= lex->len) return make_token_at(lex, TOK_EOF, lex->line, lex->col);

    int c = peek(lex);
    size_t sl = lex->line, sc = lex->col;

    /* Identifier / keyword */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        size_t spos = lex->pos;
        while (lex->pos < lex->len && is_ident_char(peek(lex))) advance(lex);
        size_t ilen = lex->pos - spos;
        TokenKind kind = lookup_keyword(lex->src + spos, ilen);
        /* Copy into pool for null-terminated access */
        char *stored = pool_alloc(lex, lex->src + spos, ilen);
        Token tok = make_token_at(lex, kind, sl, sc);
        tok.ident = stored;
        tok.id_hash = djb2(stored, ilen);
        return tok;
    }

    /* Integer literal */
    if (c >= '0' && c <= '9') {
        size_t spos = lex->pos;
        if (c == '0' && lex->pos + 1 < lex->len) {
            int n = (unsigned char)lex->src[lex->pos + 1];
            if (n == 'x' || n == 'X') {
                lex->pos += 2;
                while (lex->pos < lex->len && is_hex_digit(peek(lex))) advance(lex);
            } else {
                while (lex->pos < lex->len && is_octal_digit(peek(lex))) advance(lex);
            }
        } else {
            while (lex->pos < lex->len && peek(lex) >= '0' && peek(lex) <= '9') advance(lex);
        }
        /* Consume integer suffixes: U, L, UL, LL, etc. */
        while (lex->pos < lex->len) {
            int sc = peek(lex);
            if (sc == 'U' || sc == 'u' || sc == 'L' || sc == 'l') {
                advance(lex);
            } else {
                break;
            }
        }
        /* Extract just the numeric part for parsing */
        size_t num_end = lex->pos;
        /* Find where suffix starts by scanning back from num_end */
        while (num_end > spos) {
            int ch = (unsigned char)lex->src[num_end - 1];
            if (ch == 'U' || ch == 'u' || ch == 'L' || ch == 'l') num_end--;
            else break;
        }
        Token tok = make_token_at(lex, TOK_INT_LIT, sl, sc);
        tok.ival = parse_int_literal(lex->src + spos, num_end - spos);
        return tok;
    }

    /* Character literal */
    if (c == '\'') {
        advance(lex);
        int ch = 0;
        if (lex->pos < lex->len && peek(lex) == '\\') {
            advance(lex);
            parse_escape(lex, &ch);
        } else if (lex->pos < lex->len) {
            ch = advance(lex);
        }
        if (lex->pos < lex->len && peek(lex) == '\'') advance(lex);
        Token tok = make_token_at(lex, TOK_CHAR_LIT, sl, sc);
        tok.ival = ch;
        return tok;
    }

    /* String literal */
    if (c == '"') {
        advance(lex);
        /* Single pass with dynamic buffer */
        size_t cap = 64;
        char *buf = (char *)malloc(cap);
        if (!buf) { fprintf(stderr, "error: out of memory\n"); exit(1); }
        size_t bi = 0;
        while (lex->pos < lex->len) {
            int pc = peek(lex);
            if (pc == '"') break;
            if (pc == '\\') {
                advance(lex);
                int ch = 0;
                parse_escape(lex, &ch);
                if (bi >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
                buf[bi++] = (char)ch;
            } else {
                if (bi >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
                buf[bi++] = (char)advance(lex);
            }
        }
        buf[bi] = '\0';
        if (lex->pos < lex->len && peek(lex) == '"') advance(lex);
        char *stored = pool_alloc(lex, buf, bi);
        free(buf);
        Token tok = make_token_at(lex, TOK_STRING_LIT, sl, sc);
        tok.ident = stored;
        tok.id_hash = djb2(stored, bi);
        return tok;
    }

    /* Multi-char operators */
    if (c == '+') { advance(lex);
        if (peek(lex) == '+') { advance(lex); return make_token_at(lex, TOK_INCR, sl, sc); }
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_PLUSEQ, sl, sc); }
        return make_token_at(lex, TOK_PLUS, sl, sc); }
    if (c == '-') { advance(lex);
        if (peek(lex) == '-') { advance(lex); return make_token_at(lex, TOK_DECR, sl, sc); }
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_MINEQ, sl, sc); }
        if (peek(lex) == '>') { advance(lex); return make_token_at(lex, TOK_ARROW, sl, sc); }
        return make_token_at(lex, TOK_MINUS, sl, sc); }
    if (c == '*') { advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_STAREQ, sl, sc); }
        return make_token_at(lex, TOK_STAR, sl, sc); }
    if (c == '/') { advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_SLASHEQ, sl, sc); }
        return make_token_at(lex, TOK_SLASH, sl, sc); }
    if (c == '%') { advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_PERCENTEQ, sl, sc); }
        return make_token_at(lex, TOK_PERCENT, sl, sc); }
    if (c == '&') { advance(lex);
        if (peek(lex) == '&') { advance(lex); return make_token_at(lex, TOK_ANDAND, sl, sc); }
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_AMPREQ, sl, sc); }
        return make_token_at(lex, TOK_AMP, sl, sc); }
    if (c == '|') { advance(lex);
        if (peek(lex) == '|') { advance(lex); return make_token_at(lex, TOK_OROR, sl, sc); }
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_PIPEEQ, sl, sc); }
        return make_token_at(lex, TOK_PIPE, sl, sc); }
    if (c == '^') { advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_CARETEQ, sl, sc); }
        return make_token_at(lex, TOK_CARET, sl, sc); }
    if (c == '!') { advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_NE, sl, sc); }
        return make_token_at(lex, TOK_BANG, sl, sc); }
    if (c == '=') { advance(lex);
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_EQEQ, sl, sc); }
        return make_token_at(lex, TOK_EQ, sl, sc); }
    if (c == '<') { advance(lex);
        if (peek(lex) == '<') {
            advance(lex);
            if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_LSHLEQ, sl, sc); }
            return make_token_at(lex, TOK_LSHL, sl, sc); }
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_LE, sl, sc); }
        return make_token_at(lex, TOK_LT, sl, sc); }
    if (c == '>') { advance(lex);
        if (peek(lex) == '>') {
            advance(lex);
            if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_RSHLEQ, sl, sc); }
            return make_token_at(lex, TOK_RSHL, sl, sc); }
        if (peek(lex) == '=') { advance(lex); return make_token_at(lex, TOK_GE, sl, sc); }
        return make_token_at(lex, TOK_GT, sl, sc); }
    if (c == '~') { advance(lex); return make_token_at(lex, TOK_TILDE, sl, sc); }
    if (c == '?') { advance(lex); return make_token_at(lex, TOK_QUESTION, sl, sc); }
    if (c == '.') { advance(lex);
        if (peek(lex) == '.' && lex->pos + 1 < lex->len && (unsigned char)lex->src[lex->pos + 1] == '.') {
            advance(lex); advance(lex); return make_token_at(lex, TOK_ELLIPSIS, sl, sc); }
        return make_token_at(lex, TOK_DOT, sl, sc); }
    if (c == '#') { advance(lex);
        if (peek(lex) == '#') { advance(lex); return make_token_at(lex, TOK_HASHHASH, sl, sc); }
        return make_token_at(lex, TOK_HASH, sl, sc); }

    /* Single-char punctuators */
    if (c == '(')  { advance(lex); return make_token_at(lex, TOK_LPAREN, sl, sc); }
    if (c == ')')  { advance(lex); return make_token_at(lex, TOK_RPAREN, sl, sc); }
    if (c == '{')  { advance(lex); return make_token_at(lex, TOK_LBRACE, sl, sc); }
    if (c == '}')  { advance(lex); return make_token_at(lex, TOK_RBRACE, sl, sc); }
    if (c == '[')  { advance(lex); return make_token_at(lex, TOK_LBRACKET, sl, sc); }
    if (c == ']')  { advance(lex); return make_token_at(lex, TOK_RBRACKET, sl, sc); }
    if (c == ',')  { advance(lex); return make_token_at(lex, TOK_COMMA, sl, sc); }
    if (c == ';')  { advance(lex); return make_token_at(lex, TOK_SEMI, sl, sc); }
    if (c == ':')  { advance(lex); return make_token_at(lex, TOK_COLON, sl, sc); }

    /* Unknown char - skip */
    advance(lex);
    return make_token_at(lex, TOK_EOF, sl, sc);
}

void lexer_unget(Lexer *lex, Token tok) {
    lex->ungot = tok;
    lex->has_unget = 1;
}

void lexer_save_state(Lexer *lex, LexerState *state) {
    state->pos = lex->pos;
    state->line = lex->line;
    state->col = lex->col;
    state->has_unget = lex->has_unget;
    state->ungot = lex->ungot;
}

void lexer_restore_state(Lexer *lex, const LexerState *state) {
    lex->pos = state->pos;
    lex->line = state->line;
    lex->col = state->col;
    lex->has_unget = state->has_unget;
    lex->ungot = state->ungot;
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
#define K(x) case x: return #x
        K(TOK_EOF); K(TOK_IDENT); K(TOK_INT_LIT); K(TOK_CHAR_LIT); K(TOK_STRING_LIT);
        K(TOK_KW_AUTO); K(TOK_KW_BREAK); K(TOK_KW_CASE); K(TOK_KW_CHAR);
        K(TOK_KW_CONST); K(TOK_KW_CONTINUE); K(TOK_KW_DEFAULT); K(TOK_KW_DO);
        K(TOK_KW_ELSE); K(TOK_KW_ENUM); K(TOK_KW_EXTERN); K(TOK_KW_FOR);
        K(TOK_KW_GOTO); K(TOK_KW_IF); K(TOK_KW_INT); K(TOK_KW_LONG);
        K(TOK_KW_REGISTER); K(TOK_KW_RETURN); K(TOK_KW_SHORT); K(TOK_KW_SIGNED);
        K(TOK_KW_SIZEOF); K(TOK_KW_STATIC); K(TOK_KW_STRUCT); K(TOK_KW_SWITCH);
        K(TOK_KW_TYPEDEF); K(TOK_KW_TYPEOF); K(TOK_KW_UNION); K(TOK_KW_UNSIGNED);
        K(TOK_KW_VOID); K(TOK_KW_WHILE);
        K(TOK_PLUS); K(TOK_MINUS); K(TOK_STAR); K(TOK_SLASH); K(TOK_PERCENT);
        K(TOK_AMP); K(TOK_PIPE); K(TOK_CARET); K(TOK_TILDE); K(TOK_BANG);
        K(TOK_EQ); K(TOK_LT); K(TOK_GT);
        K(TOK_PLUSEQ); K(TOK_MINEQ); K(TOK_STAREQ); K(TOK_SLASHEQ); K(TOK_PERCENTEQ);
        K(TOK_AMPREQ); K(TOK_PIPEEQ); K(TOK_CARETEQ);
        K(TOK_LSHL); K(TOK_RSHL); K(TOK_LSHLEQ); K(TOK_RSHLEQ);
        K(TOK_EQEQ); K(TOK_NE); K(TOK_LE); K(TOK_GE);
        K(TOK_ANDAND); K(TOK_OROR); K(TOK_INCR); K(TOK_DECR); K(TOK_ARROW);
        K(TOK_LPAREN); K(TOK_RPAREN); K(TOK_LBRACE); K(TOK_RBRACE);
        K(TOK_LBRACKET); K(TOK_RBRACKET); K(TOK_COMMA); K(TOK_SEMI);
        K(TOK_DOT); K(TOK_HASH); K(TOK_HASHHASH); K(TOK_QUESTION); K(TOK_COLON);
        K(TOK_ELLIPSIS);
#undef K
        default: return "TOK_UNKNOWN";
    }
}
