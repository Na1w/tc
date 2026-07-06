#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "lexer.h"

static int passed = 0, failed = 0;
static void check(const char *test, int cond) {
    if (cond) { passed++; printf("  PASS: %s\n", test); }
    else { failed++; printf("  FAIL: %s\n", test); }
}

static void test_keywords(void) {
    printf("Keywords:\n");
    const char *src = "auto break case char const continue default do else enum extern for goto if int long register return short signed sizeof static struct switch typedef typeof union unsigned void while";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;
    TokenKind expected[] = {
        TOK_KW_AUTO, TOK_KW_BREAK, TOK_KW_CASE, TOK_KW_CHAR, TOK_KW_CONST,
        TOK_KW_CONTINUE, TOK_KW_DEFAULT, TOK_KW_DO, TOK_KW_ELSE, TOK_KW_ENUM,
        TOK_KW_EXTERN, TOK_KW_FOR, TOK_KW_GOTO, TOK_KW_IF, TOK_KW_INT,
        TOK_KW_LONG, TOK_KW_REGISTER, TOK_KW_RETURN, TOK_KW_SHORT, TOK_KW_SIGNED,
        TOK_KW_SIZEOF, TOK_KW_STATIC, TOK_KW_STRUCT, TOK_KW_SWITCH, TOK_KW_TYPEDEF,
        TOK_KW_TYPEOF, TOK_KW_UNION, TOK_KW_UNSIGNED, TOK_KW_VOID, TOK_KW_WHILE
    };
    for (int i = 0; i < 30; i++) {
        tok = lexer_next(lex);
        check(token_kind_name(tok.kind), tok.kind == expected[i]);
    }
    lexer_destroy(lex);
}

static void test_operators(void) {
    printf("Operators:\n");
    const char *src = "+ - * / % & | ^ ~ ! = < > += -= *= /= %= &= |= ^= << >> <<= >>= == != <= >= && || ++ -- ->";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex); check("+", tok.kind == TOK_PLUS);
    tok = lexer_next(lex); check("-", tok.kind == TOK_MINUS);
    tok = lexer_next(lex); check("*", tok.kind == TOK_STAR);
    tok = lexer_next(lex); check("/", tok.kind == TOK_SLASH);
    tok = lexer_next(lex); check("%", tok.kind == TOK_PERCENT);
    tok = lexer_next(lex); check("&", tok.kind == TOK_AMP);
    tok = lexer_next(lex); check("|", tok.kind == TOK_PIPE);
    tok = lexer_next(lex); check("^", tok.kind == TOK_CARET);
    tok = lexer_next(lex); check("~", tok.kind == TOK_TILDE);
    tok = lexer_next(lex); check("!", tok.kind == TOK_BANG);
    tok = lexer_next(lex); check("=", tok.kind == TOK_EQ);
    tok = lexer_next(lex); check("<", tok.kind == TOK_LT);
    tok = lexer_next(lex); check(">", tok.kind == TOK_GT);
    tok = lexer_next(lex); check("+=", tok.kind == TOK_PLUSEQ);
    tok = lexer_next(lex); check("-=", tok.kind == TOK_MINEQ);
    tok = lexer_next(lex); check("*=", tok.kind == TOK_STAREQ);
    tok = lexer_next(lex); check("/=", tok.kind == TOK_SLASHEQ);
    tok = lexer_next(lex); check("%=", tok.kind == TOK_PERCENTEQ);
    tok = lexer_next(lex); check("&=", tok.kind == TOK_AMPREQ);
    tok = lexer_next(lex); check("|=", tok.kind == TOK_PIPEEQ);
    tok = lexer_next(lex); check("^=", tok.kind == TOK_CARETEQ);
    tok = lexer_next(lex); check("<<", tok.kind == TOK_LSHL);
    tok = lexer_next(lex); check(">>", tok.kind == TOK_RSHL);
    tok = lexer_next(lex); check("<<=", tok.kind == TOK_LSHLEQ);
    tok = lexer_next(lex); check(">>=", tok.kind == TOK_RSHLEQ);
    tok = lexer_next(lex); check("==", tok.kind == TOK_EQEQ);
    tok = lexer_next(lex); check("!=", tok.kind == TOK_NE);
    tok = lexer_next(lex); check("<=", tok.kind == TOK_LE);
    tok = lexer_next(lex); check(">=", tok.kind == TOK_GE);
    tok = lexer_next(lex); check("&&", tok.kind == TOK_ANDAND);
    tok = lexer_next(lex); check("||", tok.kind == TOK_OROR);
    tok = lexer_next(lex); check("++", tok.kind == TOK_INCR);
    tok = lexer_next(lex); check("--", tok.kind == TOK_DECR);
    tok = lexer_next(lex); check("->", tok.kind == TOK_ARROW);
    lexer_destroy(lex);
}

static void test_punctuators(void) {
    printf("Punctuators:\n");
    const char *src = "( ) { } [ ] , ; . # ## ? : ...";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex); check("(", tok.kind == TOK_LPAREN);
    tok = lexer_next(lex); check(")", tok.kind == TOK_RPAREN);
    tok = lexer_next(lex); check("{", tok.kind == TOK_LBRACE);
    tok = lexer_next(lex); check("}", tok.kind == TOK_RBRACE);
    tok = lexer_next(lex); check("[", tok.kind == TOK_LBRACKET);
    tok = lexer_next(lex); check("]", tok.kind == TOK_RBRACKET);
    tok = lexer_next(lex); check(",", tok.kind == TOK_COMMA);
    tok = lexer_next(lex); check(";", tok.kind == TOK_SEMI);
    tok = lexer_next(lex); check(".", tok.kind == TOK_DOT);
    tok = lexer_next(lex); check("#", tok.kind == TOK_HASH);
    tok = lexer_next(lex); check("##", tok.kind == TOK_HASHHASH);
    tok = lexer_next(lex); check("?", tok.kind == TOK_QUESTION);
    tok = lexer_next(lex); check(":", tok.kind == TOK_COLON);
    tok = lexer_next(lex); check("...", tok.kind == TOK_ELLIPSIS);
    lexer_destroy(lex);
}

static void test_int_literals(void) {
    printf("Integer literals:\n");
    const char *src = "42 0 0xFF 0x1A 077 12345";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex); check("42", tok.kind == TOK_INT_LIT && tok.ival == 42);
    tok = lexer_next(lex); check("0", tok.kind == TOK_INT_LIT && tok.ival == 0);
    tok = lexer_next(lex); check("0xFF", tok.kind == TOK_INT_LIT && tok.ival == 255);
    tok = lexer_next(lex); check("0x1A", tok.kind == TOK_INT_LIT && tok.ival == 26);
    tok = lexer_next(lex); check("077", tok.kind == TOK_INT_LIT && tok.ival == 63);
    tok = lexer_next(lex); check("12345", tok.kind == TOK_INT_LIT && tok.ival == 12345);
    lexer_destroy(lex);
}

static void test_char_literals(void) {
    printf("Char literals:\n");
    const char *src = "'a' '\\n' '\\t' '\\'' '\"' '\\x41' '\\101'";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex); check("'a'", tok.kind == TOK_CHAR_LIT && tok.ival == 'a');
    tok = lexer_next(lex); check("'\\n'", tok.kind == TOK_CHAR_LIT && tok.ival == '\n');
    tok = lexer_next(lex); check("'\\t'", tok.kind == TOK_CHAR_LIT && tok.ival == '\t');
    tok = lexer_next(lex); check("'\\''", tok.kind == TOK_CHAR_LIT && tok.ival == '\'');
    tok = lexer_next(lex); check("'\\\"'", tok.kind == TOK_CHAR_LIT && tok.ival == '"');
    tok = lexer_next(lex); check("'\\x41'", tok.kind == TOK_CHAR_LIT && tok.ival == 'A');
    tok = lexer_next(lex); check("'\\101'", tok.kind == TOK_CHAR_LIT && tok.ival == 'A');
    lexer_destroy(lex);
}

static void test_string_literals(void) {
    printf("String literals:\n");
    const char *src = "\"hello\" \"\\n\\t\" \"\\x41\\101\"";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex);
    check("\"hello\"", tok.kind == TOK_STRING_LIT && strcmp(tok.ident, "hello") == 0);

    tok = lexer_next(lex);
    check("\"\\n\\t\"", tok.kind == TOK_STRING_LIT && tok.ident[0] == '\n' && tok.ident[1] == '\t' && tok.ident[2] == '\0');

    tok = lexer_next(lex);
    check("\"\\x41\\101\"", tok.kind == TOK_STRING_LIT && strcmp(tok.ident, "AA") == 0);
    lexer_destroy(lex);
}

static void test_comments(void) {
    printf("Comments:\n");
    const char *src = "int /* block */ x; // line comment\ny;";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex); check("int kw", tok.kind == TOK_KW_INT);
    tok = lexer_next(lex); check("x ident", tok.kind == TOK_IDENT && strcmp(tok.ident, "x") == 0);
    tok = lexer_next(lex); check("; after x", tok.kind == TOK_SEMI);
    tok = lexer_next(lex); check("y ident", tok.kind == TOK_IDENT && strcmp(tok.ident, "y") == 0);
    lexer_destroy(lex);
}

static void test_unget(void) {
    printf("Unget:\n");
    const char *src = "int x;";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex); check("int", tok.kind == TOK_KW_INT);
    lexer_unget(lex, tok);
    tok = lexer_next(lex); check("int after unget", tok.kind == TOK_KW_INT);
    tok = lexer_next(lex); check("x", tok.kind == TOK_IDENT);
    lexer_destroy(lex);
}

static void test_ident_hash(void) {
    printf("Identifier hash:\n");
    const char *src = "fooBar123";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok = lexer_next(lex);
    check("ident kind", tok.kind == TOK_IDENT);
    check("ident text", strcmp(tok.ident, "fooBar123") == 0);
    check("hash nonzero", tok.id_hash != 0);
    /* Verify djb2 manually */
    uint32_t h = 5381;
    const char *s = "fooBar123";
    for (size_t i = 0; i < strlen(s); i++) h = h * 33 + (unsigned char)s[i];
    check("hash correct", tok.id_hash == h);
    lexer_destroy(lex);
}

static void test_lineno(void) {
    printf("Line/col tracking:\n");
    const char *src = "int\nx;\nfoo";
    Lexer *lex = lexer_create(src, strlen(src));
    Token tok;

    tok = lexer_next(lex); check("int line 1", tok.line == 1 && tok.col == 1);
    tok = lexer_next(lex); check("x line 2", tok.line == 2 && tok.col == 1);
    tok = lexer_next(lex); check("; line 2", tok.line == 2 && tok.col == 2);
    tok = lexer_next(lex); check("foo line 3", tok.line == 3 && tok.col == 1);
    lexer_destroy(lex);
}

int main(void) {
    printf("=== Lexer Tests ===\n\n");
    test_keywords();
    test_operators();
    test_punctuators();
    test_int_literals();
    test_char_literals();
    test_string_literals();
    test_comments();
    test_unget();
    test_ident_hash();
    test_lineno();
    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
