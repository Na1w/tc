/*
 * stdlib.c -- Math and utility functions for the tc runtime.
 *
 * Minimal ANSI C implementation of stdlib functions required by the
 * tc compiler's target runtime.  All symbols are exported (public).
 *
 * Functions provided:
 *   int       abs(int)
 *   int       atoi(const char *)
 *   char     *itoa(int, char *, int)    -- base 10 and base 16
 *   void     *malloc(size_t)            -- bump allocator (1 MiB)
 *   void      free(void *)              -- no-op (bump allocator)
 *   void     *calloc(size_t, size_t)    -- zeroed bump allocation
 *   void     *realloc(void *, size_t)   -- reallocate via bump
 *   void      exit(int)                 -- terminate via syscall
 *
 * NOTE: memcpy, memset, memcmp, strlen, strcpy, strcat, strcmp
 *       are provided by string.c (not duplicated here).
 */

/* ------------------------------------------------------------------ */
/*  Fundamental types                                                  */
/* ------------------------------------------------------------------ */

typedef unsigned long size_t;
#define NULL ((void *)0)

/* ------------------------------------------------------------------ */
/*  Syscall support (must be included early for exit() below)           */
/* ------------------------------------------------------------------ */

#include "syscall.h"

/* ------------------------------------------------------------------ */
/*  Forward declarations for functions provided by string.c             */
/* ------------------------------------------------------------------ */

void *memset(void *ptr, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);

/* ------------------------------------------------------------------ */
/*  abs -- absolute value of an integer                                */
/* ------------------------------------------------------------------ */

int abs(int x)
{
    if (x < 0)
        return -x;
    return x;
}

/* ------------------------------------------------------------------ */
/*  atoi -- convert ASCII string to integer (base 10)                  */
/* ------------------------------------------------------------------ */

int atoi(const char *str)
{
    int sign = 1;
    int acc  = 0;

    /* skip leading whitespace */
    while (*str == ' '  || *str == '\t' || *str == '\n' ||
           *str == '\r' || *str == '\f' || *str == '\v') {
        str++;
    }

    /* optional sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* digits */
    while (*str >= '0' && *str <= '9') {
        acc = acc * 10 + (*str - '0');
        str++;
    }

    return acc * sign;
}

/* ------------------------------------------------------------------ */
/*  itoa -- convert integer to string (base 10 or base 16)             */
/* ------------------------------------------------------------------ */

char *itoa(int value, char *str, int radix)
{
    if (str == NULL)
        return NULL;

    if (radix != 10 && radix != 16) {
        str[0] = '\0';
        return str;
    }

    unsigned int unval;
    int negative = 0;

    if (radix == 10) {
        if (value < 0) {
            negative = 1;
            /* Avoid INT_MIN edge case by converting to unsigned
               after negation.  For INT_MIN the negation wraps,
               but that is the best we can do in pure C. */
            unval = (unsigned int)(0U - (unsigned int)value);
        } else {
            unval = (unsigned int)value;
        }
    } else {
        /* base 16: treat as unsigned */
        unval = (unsigned int)value;
    }

    /* Build the string backwards */
    char buf[33];  /* 32 hex digits + sign + NUL */
    int  pos = 0;

    do {
        unsigned int digit = unval % (unsigned int)radix;
        if (digit < 10)
            buf[pos] = (char)('0' + digit);
        else
            buf[pos] = (char)('a' + digit - 10);
        pos++;
        unval /= (unsigned int)radix;
    } while (unval != 0);

    if (negative) {
        buf[pos] = '-';
        pos++;
    }

    /* Reverse into output buffer */
    int out = 0;
    while (pos > 0) {
        pos--;
        str[out] = buf[pos];
        out++;
    }
    str[out] = '\0';

    return str;
}

/* ------------------------------------------------------------------ */
/*  Bump allocator internals                                           */
/* ------------------------------------------------------------------ */

/* 1 MiB allocation buffer, statically allocated in BSS. */
static char  __alloc_buf[1024 * 1024];
static char *__alloc_ptr = __alloc_buf;

/*
 * Each allocation is preceded by a size_t header that records the
 * user-requested size (before alignment).  Layout in the buffer:
 *
 *   [ size_t header ][ user data (aligned to 8 bytes) ]
 *
 * The returned pointer points to the user data (after the header).
 * The header itself is aligned to 8 bytes (sizeof(size_t) == 8 on
 * x86-64, so no extra padding is needed).
 */
#define __ALLOC_HDR_SIZE  (sizeof(size_t))

/*
 * __alloc_align -- round up ptr to the next multiple of align
 *                  (align must be a power of 2).
 */
static char *
__alloc_align(char *ptr, size_t align)
{
    size_t mask = align - 1;
    size_t sp = (size_t)ptr;
    return (char *)((sp + mask) & ~mask);
}

/*
 * __alloc_get_size -- retrieve the stored size for a user pointer.
 */
static size_t
__alloc_get_size(void *ptr)
{
    size_t *hdr = (size_t *)((char *)ptr - __ALLOC_HDR_SIZE);
    return *hdr;
}

/* ------------------------------------------------------------------ */
/*  malloc -- allocate memory from the bump allocator                   */
/* ------------------------------------------------------------------ */

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;

    /* Align the user data to 8 bytes (pointer alignment on x86-64).
       The header is already 8-byte aligned because sizeof(size_t)==8
       and the total allocation (header + aligned data) is 8-byte
       aligned, so the next header will also be aligned. */
    char *data = __alloc_align(__alloc_ptr + __ALLOC_HDR_SIZE, 8);
    char *next = data + size;

    /* Guard: do not overflow the buffer */
    if (next > __alloc_buf + sizeof(__alloc_buf))
        return NULL;

    /* Write the size header before the user data */
    size_t *hdr = (size_t *)(data - __ALLOC_HDR_SIZE);
    *hdr = size;

    __alloc_ptr = next;
    return (void *)data;
}

/* ------------------------------------------------------------------ */
/*  calloc -- allocate and zero-initialize memory                      */
/* ------------------------------------------------------------------ */

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p != NULL)
        memset(p, 0, total);
    return p;
}

/* ------------------------------------------------------------------ */
/*  free -- no-op for bump allocator                                   */
/* ------------------------------------------------------------------ */

void free(void *ptr)
{
    (void)ptr;
    /* Bump allocator does not reclaim memory on a per-block basis. */
}

/* ------------------------------------------------------------------ */
/*  realloc -- reallocate memory via bump allocator                    */
/* ------------------------------------------------------------------ */

void *realloc(void *ptr, size_t size)
{
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    if (ptr == NULL)
        return malloc(size);

    /*
     * Bump allocator cannot shrink or move blocks.  We allocate a
     * new block and copy the old data.  The old block is orphaned
     * (not reclaimed) -- acceptable for a minimal runtime.
     *
     * The size header preceding each allocation tells us exactly
     * how many bytes to copy from the old block.
     */
    size_t old_size = __alloc_get_size(ptr);
    void *new_ptr = malloc(size);
    if (new_ptr == NULL)
        return NULL;

    /* Copy the smaller of old_size and size */
    size_t copy_size = (old_size < size) ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);
    return new_ptr;
}

/* ------------------------------------------------------------------ */
/*  exit -- terminate the process via syscall                          */
/* ------------------------------------------------------------------ */

void exit(int code)
{
    sys_invoke(SYS_EXIT_GROUP, (long)code, 0, 0, 0, 0, 0);
    /* Unreachable, but silence compiler warnings. */
    for (;;) {}
}
