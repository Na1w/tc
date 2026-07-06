#ifndef TC_LEXER_H
#define TC_LEXER_H

#include <stddef.h>
#include <stdint.h>

typedef enum TokenKind {
    TOK_EOF = 0,

    /* Literals */
    TOK_IDENT,
    TOK_INT_LIT,
    TOK_CHAR_LIT,
    TOK_STRING_LIT,

    /* Keywords (alphabetical) */
    TOK_KW_AUTO,
    TOK_KW_BREAK,
    TOK_KW_CASE,
    TOK_KW_CHAR,
    TOK_KW_CONST,
    TOK_KW_CONTINUE,
    TOK_KW_DEFAULT,
    TOK_KW_DO,
    TOK_KW_ELSE,
    TOK_KW_ENUM,
    TOK_KW_EXTERN,
    TOK_KW_FOR,
    TOK_KW_GOTO,
    TOK_KW_IF,
    TOK_KW_INT,
    TOK_KW_LONG,
    TOK_KW_REGISTER,
    TOK_KW_RETURN,
    TOK_KW_SHORT,
    TOK_KW_SIGNED,
    TOK_KW_SIZEOF,
    TOK_KW_STATIC,
    TOK_KW_STRUCT,
    TOK_KW_SWITCH,
    TOK_KW_TYPEDEF,
    TOK_KW_TYPEOF,
    TOK_KW_UNION,
    TOK_KW_UNSIGNED,
    TOK_KW_VOID,
    TOK_KW_WHILE,

    /* Single-char operators */
    TOK_PLUS,      /* + */
    TOK_MINUS,     /* - */
    TOK_STAR,      /* * */
    TOK_SLASH,     /* / */
    TOK_PERCENT,   /* % */
    TOK_AMP,       /* & */
    TOK_PIPE,      /* | */
    TOK_CARET,     /* ^ */
    TOK_TILDE,     /* ~ */
    TOK_BANG,      /* ! */
    TOK_EQ,        /* = */
    TOK_LT,        /* < */
    TOK_GT,        /* > */

    /* Two-char operators */
    TOK_PLUSEQ,    /* += */
    TOK_MINEQ,     /* -= */
    TOK_STAREQ,    /* *= */
    TOK_SLASHEQ,   /* /= */
    TOK_PERCENTEQ, /* %= */
    TOK_AMPREQ,    /* &= */
    TOK_PIPEEQ,    /* |= */
    TOK_CARETEQ,   /* ^= */
    TOK_LSHL,      /* << */
    TOK_RSHL,      /* >> */
    TOK_LSHLEQ,    /* <<= */
    TOK_RSHLEQ,    /* >>= */
    TOK_EQEQ,      /* == */
    TOK_NE,        /* != */
    TOK_LE,        /* <= */
    TOK_GE,        /* >= */
    TOK_ANDAND,    /* && */
    TOK_OROR,      /* || */
    TOK_INCR,      /* ++ */
    TOK_DECR,      /* -- */
    TOK_ARROW,     /* -> */

    /* Punctuators */
    TOK_LPAREN,    /* ( */
    TOK_RPAREN,    /* ) */
    TOK_LBRACE,    /* { */
    TOK_RBRACE,    /* } */
    TOK_LBRACKET,  /* [ */
    TOK_RBRACKET,  /* ] */
    TOK_COMMA,     /* , */
    TOK_SEMI,      /* ; */
    TOK_DOT,       /* . */
    TOK_HASH,      /* # */
    TOK_HASHHASH,  /* ## */
    TOK_QUESTION,  /* ? */
    TOK_COLON,     /* : */
    TOK_ELLIPSIS,  /* ... */
} TokenKind;

typedef struct Token {
    TokenKind kind;
    size_t line, col;
    int64_t ival;
    double fval;
    const char *ident;
    uint32_t id_hash;
} Token;

typedef struct Lexer Lexer;

Lexer *lexer_create(const char *source, size_t len);
void lexer_destroy(Lexer *lex);
Token lexer_next(Lexer *lex);
void lexer_unget(Lexer *lex, Token tok);
const char *token_kind_name(TokenKind kind);

#endif
