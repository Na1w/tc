/*
 * string.c -- String and memory utility functions for the tc runtime.
 *
 * Minimal ANSI C implementation of core string/memory functions required
 * by the tc compiler's target runtime.  All symbols are exported (public).
 * No includes are used -- this IS the foundational string library.
 *
 * Functions provided:
 *   size_t    strlen(const char *)
 *   char     *strcpy(char *, const char *)
 *   char     *strncpy(char *, const char *, size_t)
 *   char     *strcat(char *, const char *)
 *   int       strcmp(const char *, const char *)
 *   int       strncmp(const char *, const char *, size_t)
 *   char     *strchr(const char *, int)
 *   char     *strstr(const char *, const char *)
 *   void     *memcpy(void *, const void *, size_t)
 *   void     *memset(void *, int, size_t)
 *   int       memcmp(const void *, const void *, size_t)
 */

/* ------------------------------------------------------------------ */
/*  Fundamental types                                                  */
/* ------------------------------------------------------------------ */

typedef unsigned long size_t;
#define NULL ((void *)0)

/* ------------------------------------------------------------------ */
/*  strlen -- return the length of a null-terminated string            */
/* ------------------------------------------------------------------ */

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

/* ------------------------------------------------------------------ */
/*  strcpy -- copy src to dest (including null terminator)             */
/* ------------------------------------------------------------------ */

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    const char *s = src;
    while ((*d++ = *s++) != '\0')
        /* nothing */;
    return dest;
}

/* ------------------------------------------------------------------ */
/*  strncpy -- copy up to n characters from src to dest                */
/*              pads with null bytes if src is shorter than n          */
/* ------------------------------------------------------------------ */

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

/* ------------------------------------------------------------------ */
/*  strcat -- append src to the end of dest                            */
/* ------------------------------------------------------------------ */

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d != '\0')
        d++;
    while ((*d++ = *src++) != '\0')
        /* nothing */;
    return dest;
}

/* ------------------------------------------------------------------ */
/*  strcmp -- lexicographic comparison of two strings                  */
/*              returns <0, 0, or >0                                   */
/* ------------------------------------------------------------------ */

int strcmp(const char *s1, const char *s2)
{
    unsigned char c1, c2;
    while (1) {
        c1 = (unsigned char)*s1;
        c2 = (unsigned char)*s2;
        if (c1 == '\0' || c1 != c2)
            return (int)c1 - (int)c2;
        s1++;
        s2++;
    }
}

/* ------------------------------------------------------------------ */
/*  strncmp -- compare up to n characters of two strings               */
/* ------------------------------------------------------------------ */

int strncmp(const char *s1, const char *s2, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        if (c1 != c2)
            return (int)c1 - (int)c2;
        if (c1 == '\0')
            return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  strchr -- find the first occurrence of character c in s            */
/*              returns NULL if not found                              */
/* ------------------------------------------------------------------ */

char *strchr(const char *s, int c)
{
    unsigned char uc = (unsigned char)c;
    while (*s != '\0' && (unsigned char)*s != uc)
        s++;
    if (*s == '\0' && uc != '\0')
        return NULL;
    return (char *)s;
}

/* ------------------------------------------------------------------ */
/*  strstr -- find the first occurrence of needle in haystack          */
/*              returns NULL if needle is not found                    */
/* ------------------------------------------------------------------ */

char *strstr(const char *haystack, const char *needle)
{
    if (*needle == '\0')
        return (char *)haystack;
    for (; *haystack != '\0'; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h != '\0' && *n != '\0' && *h == *n) {
            h++;
            n++;
        }
        if (*n == '\0')
            return (char *)haystack;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  memcpy -- copy n bytes from src to dest                            */
/*              returns dest                                           */
/* ------------------------------------------------------------------ */

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

/* ------------------------------------------------------------------ */
/*  memset -- fill n bytes of s with byte value c                      */
/*              returns s                                              */
/* ------------------------------------------------------------------ */

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = uc;
    return s;
}

/* ------------------------------------------------------------------ */
/*  memcmp -- lexicographic comparison of n bytes                      */
/*              returns <0, 0, or >0                                   */
/* ------------------------------------------------------------------ */

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    size_t i;
    for (i = 0; i < n; i++) {
        if (p1[i] != p2[i])
            return (int)p1[i] - (int)p2[i];
    }
    return 0;
}
