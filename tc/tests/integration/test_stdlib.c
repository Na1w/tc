/*
 * test_stdlib.c -- Comprehensive tests for tc/runtime/stdlib.c
 *
 * Compile: gcc -std=c99 -Wall -Wextra -I tc/runtime test_stdlib.c tc/runtime/stdlib.c -o test_stdlib
 */
#include <stdio.h>

/* Pull in the runtime types and functions */
/* Stub sys_invoke so we can compile without the assembly wrapper */
long sys_invoke(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
    (void)nr; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}
#include "../../runtime/stdlib.c"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* ---- abs tests ---- */
static void test_abs(void)
{
    printf("=== abs ===\n");
    ASSERT(abs(0) == 0,       "abs(0)");
    ASSERT(abs(5) == 5,       "abs(5)");
    ASSERT(abs(-5) == 5,      "abs(-5)");
    ASSERT(abs(-1) == 1,      "abs(-1)");
    ASSERT(abs(100) == 100,   "abs(100)");
    ASSERT(abs(-100) == 100,  "abs(-100)");
}

/* ---- atoi tests ---- */
static void test_atoi(void)
{
    printf("=== atoi ===\n");
    ASSERT(atoi("0") == 0,        "atoi(0)");
    ASSERT(atoi("42") == 42,      "atoi(42)");
    ASSERT(atoi("-42") == -42,    "atoi(-42)");
    ASSERT(atoi("+42") == 42,     "atoi(+42)");
    ASSERT(atoi("  123") == 123,  "atoi(leading spaces)");
    ASSERT(atoi("  -7") == -7,    "atoi(leading spaces + sign)");
    ASSERT(atoi("99abc") == 99,   "atoi(trailing garbage)");
    ASSERT(atoi("") == 0,         "atoi(empty)");
}

/* ---- itoa tests ---- */
static void test_itoa(void)
{
    printf("=== itoa ===\n");
    char buf[33];

    ASSERT(strcmp(itoa(0, buf, 10), "0") == 0,       "itoa(0,10)");
    ASSERT(strcmp(itoa(42, buf, 10), "42") == 0,     "itoa(42,10)");
    ASSERT(strcmp(itoa(-42, buf, 10), "-42") == 0,   "itoa(-42,10)");
    ASSERT(strcmp(itoa(100, buf, 10), "100") == 0,   "itoa(100,10)");
    ASSERT(strcmp(itoa(-999, buf, 10), "-999") == 0, "itoa(-999,10)");

    ASSERT(strcmp(itoa(0, buf, 16), "0") == 0,       "itoa(0,16)");
    ASSERT(strcmp(itoa(255, buf, 16), "ff") == 0,    "itoa(255,16)");
    ASSERT(strcmp(itoa(16, buf, 16), "10") == 0,     "itoa(16,16)");
    ASSERT(strcmp(itoa(256, buf, 16), "100") == 0,   "itoa(256,16)");
    ASSERT(strcmp(itoa(10, buf, 16), "a") == 0,      "itoa(10,16)");
    ASSERT(strcmp(itoa(15, buf, 16), "f") == 0,      "itoa(15,16)");

    /* Invalid radix should produce empty string */
    itoa(42, buf, 8);
    ASSERT(strcmp(buf, "") == 0, "itoa(invalid radix)");

    /* NULL destination */
    ASSERT(itoa(42, NULL, 10) == NULL, "itoa(NULL dst)");
}

/* ---- strlen tests ---- */
static void test_strlen(void)
{
    printf("=== strlen ===\n");
    ASSERT(strlen("") == 0,       "strlen(empty)");
    ASSERT(strlen("a") == 1,      "strlen(a)");
    ASSERT(strlen("hello") == 5,  "strlen(hello)");
}

/* ---- memcpy tests ---- */
static void test_memcpy(void)
{
    printf("=== memcpy ===\n");
    char src[] = "hello";
    char dst[10];
    memcpy(dst, src, 6);
    ASSERT(strcmp(dst, "hello") == 0, "memcpy basic");

    /* byte copy */
    unsigned char s2[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    unsigned char d2[4];
    memcpy(d2, s2, 4);
    ASSERT(d2[0] == 0xAA && d2[1] == 0xBB && d2[2] == 0xCC && d2[3] == 0xDD, "memcpy bytes");
}

/* ---- memset tests ---- */
static void test_memset(void)
{
    printf("=== memset ===\n");
    char buf[8];
    memset(buf, 'x', 8);
    ASSERT(buf[0] == 'x' && buf[4] == 'x' && buf[7] == 'x', "memset fill");

    memset(buf, 0, 8);
    ASSERT(buf[0] == 0 && buf[7] == 0, "memset zero");
}

/* ---- memcmp tests ---- */
static void test_memcmp(void)
{
    printf("=== memcmp ===\n");
    ASSERT(memcmp("abc", "abc", 3) == 0,   "memcmp equal");
    ASSERT(memcmp("abc", "abd", 3) < 0,    "memcmp less");
    ASSERT(memcmp("abd", "abc", 3) > 0,    "memcmp greater");
    ASSERT(memcmp("", "", 0) == 0,         "memcmp empty");
}

/* ---- strcpy / strcat / strcmp tests ---- */
static void test_str_funcs(void)
{
    printf("=== strcpy/strcat/strcmp ===\n");
    char buf[32];

    strcpy(buf, "hello");
    ASSERT(strcmp(buf, "hello") == 0, "strcpy basic");

    strcat(buf, " world");
    ASSERT(strcmp(buf, "hello world") == 0, "strcat basic");

    ASSERT(strcmp("abc", "abc") == 0,  "strcmp equal");
    ASSERT(strcmp("abc", "abd") < 0,   "strcmp less");
    ASSERT(strcmp("abd", "abc") > 0,   "strcmp greater");
}

/* ---- malloc / calloc / free / realloc tests ---- */
static void test_alloc(void)
{
    printf("=== malloc/calloc/free/realloc ===\n");

    /* Basic allocation */
    int *p = (int *)malloc(sizeof(int));
    ASSERT(p != NULL, "malloc(sizeof(int)) non-NULL");
    *p = 42;
    ASSERT(*p == 42, "malloc write/read");

    /* Larger allocation */
    char *q = (char *)malloc(1024);
    ASSERT(q != NULL, "malloc(1024) non-NULL");
    memset(q, 'A', 1024);
    ASSERT(q[0] == 'A' && q[1023] == 'A', "malloc fill");

    /* calloc should zero */
    int *z = (int *)calloc(10, sizeof(int));
    ASSERT(z != NULL, "calloc non-NULL");
    int all_zero = 1;
    for (int i = 0; i < 10; i++) {
        if (z[i] != 0) { all_zero = 0; break; }
    }
    ASSERT(all_zero, "calloc zeroes memory");

    /* free is no-op (should not crash) */
    free(p);
    free(q);
    free(z);

    /* realloc from NULL == malloc */
    char *r = (char *)realloc(NULL, 64);
    ASSERT(r != NULL, "realloc(NULL, 64)");
    strcpy(r, "test");
    ASSERT(strcmp(r, "test") == 0, "realloc write");

    /* realloc existing */
    char *r2 = (char *)realloc(r, 128);
    ASSERT(r2 != NULL, "realloc existing");
    ASSERT(strcmp(r2, "test") == 0, "realloc preserves data");

    /* malloc(0) should return NULL */
    ASSERT(malloc(0) == NULL, "malloc(0) returns NULL");
}

/* ---- alignment test ---- */
static void test_alignment(void)
{
    printf("=== alignment ===\n");
    /* Allocate several blocks and check 8-byte alignment */
    void *ptrs[16];
    for (int i = 0; i < 16; i++) {
        ptrs[i] = malloc(3);  /* small odd sizes */
        ASSERT(((size_t)ptrs[i] & 7) == 0, "8-byte aligned");
    }
}

int main(void)
{
    printf("stdlib.c test suite\n");
    printf("====================\n\n");

    test_abs();
    test_atoi();
    test_itoa();
    test_strlen();
    test_memcpy();
    test_memset();
    test_memcmp();
    test_str_funcs();
    test_alignment();
    test_alloc();

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    if (tests_failed > 0) {
        printf("OVERALL: FAIL\n");
        return 1;
    }
    printf("OVERALL: PASS\n");
    return 0;
}
