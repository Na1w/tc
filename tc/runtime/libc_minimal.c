/*
 * libc_minimal.c -- Unified minimal C runtime library for x86-64 Linux.
 *
 * Combines all runtime modules (stdio, stdlib, string, syscall) into
 * a single standalone ANSI C file.  Compiles independently with gcc
 * and links with crt0.S for full program startup.
 *
 * Functions provided:
 *   Syscall:  sys_write, sys_exit
 *   String:   strlen, strcpy, strncpy, strcat, strcmp, strncmp,
 *             strchr, strstr, memcpy, memset, memcmp
 *   Stdlib:   abs, atoi, itoa, malloc, calloc, free, realloc, exit
 *   Stdio:    printf, puts, putstr
 *
 * Architecture: x86-64 Linux (SysV ABI syscall convention).
 */

/* ==================================================================
 *  SECTION 1 — Fundamental types and constants
 * ================================================================== */

typedef unsigned long size_t;
typedef unsigned char  u8;
typedef unsigned int   uint;

#define NULL   ((void *)0)
#define MIN(a,b) (((a)<(b))?(a):(b))

/* Linux x86-64 syscall numbers */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_EXIT        60
#define SYS_EXIT_GROUP  231
#define SYS_BRK         45
#define SYS_MMAP        9

/* File descriptor constants */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Open flags */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0100
#define O_APPEND  02000
#define O_TRUNC   01000

/* ==================================================================
 *  SECTION 2 — Variadic argument support (va_list)
 *
 *  Uses GCC builtins which are available on all x86-64 Linux targets.
 * ================================================================== */

typedef __builtin_va_list va_list;
#define va_start(ap, param)  __builtin_va_start(ap, param)
#define va_arg(ap, type)     __builtin_va_arg(ap, type)
#define va_end(ap)           __builtin_va_end(ap)

/* ==================================================================
 *  SECTION 3 — Inline-assembly syscall wrappers (x86-64 SysV ABI)
 *
 *  Register convention:
 *    RAX = syscall number   R10 = arg4   R8  = arg5   R9  = arg6
 *    RDI = arg1             RSI = arg2   RDX = arg3
 *
 *  The syscall instruction clobbers RCX and R11.
 * ================================================================== */

long sys_write(int fd, const char *buf, size_t count)
{
    long result;
    __asm__ __volatile__ (
        "syscall"
        : "=a"(result)
        : "a"(SYS_WRITE), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return result;
}

void sys_exit(int code)
{
    __asm__ __volatile__ (
        "syscall"
        : /* no outputs -- never returns */
        : "a"(SYS_EXIT_GROUP), "D"(code)
        : "rcx", "r11", "memory"
    );
    __asm__ __volatile__("ud2"); /* abort if unreachable code is reached */
}

/* Generic 6-argument syscall helper (used internally). */
static long sys_invoke(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
    long result;
    __asm__ __volatile__ (
        "mov %4, %%r10\n"
        "mov %5, %%r8\n"
        "mov %6, %%r9\n"
        "syscall"
        : "=a"(result)
        : "a"(nr), "D"(a0), "S"(a1), "d"(a2),
          "r"(a3), "r"(a4), "r"(a5)
        : "rcx", "r11", "r10", "memory"
    );
    return result;
}

/* ==================================================================
 *  SECTION 4 — String and memory functions
 * ================================================================== */

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0')
        /* nothing */;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d != '\0') d++;
    while ((*d++ = *src++) != '\0')
        /* nothing */;
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 != '\0' && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        if (c1 == '\0') return 0;
        if (c1 != c2) return (int)c1 - (int)c2;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    unsigned char uc = (unsigned char)c;
    while (*s != '\0' && (unsigned char)*s != uc) s++;
    if (*s == '\0' && uc != '\0') return NULL;
    return (char *)s;
}

char *strstr(const char *haystack, const char *needle)
{
    if (*needle == '\0') return (char *)haystack;
    for (; *haystack != '\0'; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h != '\0' && *n != '\0' && *h == *n) { h++; n++; }
        if (*n == '\0') return (char *)haystack;
    }
    return NULL;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n > 0) { *d++ = *s++; n--; }
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    unsigned char v = (unsigned char)c;
    while (n > 0) { *p++ = v; n--; }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n > 0) {
        if (*p1 != *p2) return (*p1 < *p2) ? -1 : 1;
        p1++; p2++; n--;
    }
    return 0;
}

/* ==================================================================
 *  SECTION 5 — Stdlib functions
 * ================================================================== */

int abs(int x)
{
    return (x < 0) ? -x : x;
}

int atoi(const char *str)
{
    int sign = 1, acc = 0;
    while (*str == ' ' || *str == '\t' || *str == '\n' ||
           *str == '\r' || *str == '\f' || *str == '\v') str++;
    if (*str == '-')       { sign = -1; str++; }
    else if (*str == '+')  { str++; }
    while (*str >= '0' && *str <= '9') { acc = acc * 10 + (*str - '0'); str++; }
    return acc * sign;
}

char *itoa(int value, char *str, int radix)
{
    if (str == NULL) return NULL;
    if (radix != 10 && radix != 16) { str[0] = '\0'; return str; }

    unsigned int unval;
    int negative = 0;

    if (radix == 10) {
        if (value < 0) { negative = 1; unval = (unsigned int)(0U - (unsigned int)value); }
        else            { unval = (unsigned int)value; }
    } else {
        unval = (unsigned int)value;
    }

    char buf[33];
    int pos = 0;
    do {
        unsigned int digit = unval % (unsigned int)radix;
        buf[pos++] = (digit < 10) ? (char)('0' + digit)
                                  : (char)('a' + digit - 10);
        unval /= (unsigned int)radix;
    } while (unval != 0);

    if (negative) { buf[pos] = '-'; pos++; }

    int out = 0;
    while (pos > 0) { pos--; str[out++] = buf[pos]; }
    str[out] = '\0';
    return str;
}

/* ------------------------------------------------------------------ */
/*  Bump allocator internals (1 MiB static buffer)                    */
/* ------------------------------------------------------------------ */

static char  __alloc_buf[1024 * 1024];
static char *__alloc_ptr = __alloc_buf;

static char *__alloc_align(char *ptr, size_t align)
{
    size_t mask = align - 1;
    return (char *)(((size_t)ptr + mask) & ~mask);
}

void *malloc(size_t size)
{
    if (size == 0) return NULL;
    char *ptr  = __alloc_align(__alloc_ptr, 8);
    char *next = ptr + size;
    if (next > __alloc_buf + sizeof(__alloc_buf)) return NULL;
    __alloc_ptr = next;
    return (void *)ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p != NULL) memset(p, 0, total);
    return p;
}

void free(void *ptr)
{
    (void)ptr; /* bump allocator does not reclaim individual blocks */
}

void *realloc(void *ptr, size_t size)
{
    if (size == 0) { free(ptr); return NULL; }
    if (ptr == NULL) return malloc(size);
    void *new_ptr = malloc(size);
    if (new_ptr == NULL) return NULL;
    memcpy(new_ptr, ptr, size);
    return new_ptr;
}

void exit(int code)
{
    sys_invoke(SYS_EXIT_GROUP, (long)code, 0, 0, 0, 0, 0);
    while (1) { } /* should not return */
}

/* ==================================================================
 *  SECTION 6 — Internal printf helpers
 * ================================================================== */

static int int_to_buf(char *buf, int value)
{
    char tmp[16];
    int i = 0, negative = 0;

    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }

    if (value < 0) {
        negative = 1;
        unsigned int uval = (unsigned int)(0L - (long)value);
        while (uval > 0) { tmp[i++] = '0' + (uval % 10); uval /= 10; }
    } else {
        unsigned int uval = (unsigned int)value;
        while (uval > 0) { tmp[i++] = '0' + (uval % 10); uval /= 10; }
    }

    int j = negative ? 1 : 0;
    if (negative) buf[0] = '-';
    for (int k = i - 1; k >= 0; k--) buf[j++] = tmp[k];
    buf[j] = '\0';
    return j;
}

static int uintptr_to_hex(char *buf, unsigned long value)
{
    const char hexdigits[] = "0123456789abcdef";
    char tmp[17];
    int i = 0;

    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }

    while (value > 0 && i < 16) {
        tmp[i++] = hexdigits[value & 0xF];
        value >>= 4;
    }

    for (int k = i - 1, j = 0; k >= 0; k--, j++) buf[j] = tmp[k];
    buf[i] = '\0';
    return i;
}

static int append_char(char *buf, int pos, char c)
{
    buf[pos] = c;
    return pos + 1;
}

static int append_str(char *buf, int pos, const char *s)
{
    if (s == NULL) s = "(null)";
    while (*s != '\0') buf[pos++] = *s++;
    return pos;
}

/* ==================================================================
 *  SECTION 7 — Stdio functions (printf / puts)
 * ================================================================== */

int puts(const char *str)
{
    if (str == NULL) return -1;
    size_t len = strlen(str);
    long r = sys_write(STDOUT_FILENO, str, len);
    if (r < 0) return (int)r;
    r = sys_write(STDOUT_FILENO, "\n", 1);
    if (r < 0) return (int)r;
    return (int)(len + 1);
}

void putstr(const char *s)
{
    if (s != NULL)
        sys_write(STDOUT_FILENO, s, strlen(s));
}

int printf(const char *fmt, ...)
{
    char buf[4096];
    int pos = 0;
    va_list args;

    if (fmt == NULL) return -1;

    va_start(args, fmt);

    while (*fmt != '\0') {
        if (pos >= 4094) break;

        if (*fmt == '%') {
            fmt++;
            if (*fmt == '\0') { buf[pos++] = '%'; break; }

            switch (*fmt) {
                case 's': {
                    const char *s = va_arg(args, const char *);
                    pos = append_str(buf, pos, s);
                    break;
                }
                case 'd': {
                    int val = va_arg(args, int);
                    char nb[16];
                    int nl = int_to_buf(nb, val);
                    for (int k = 0; k < nl && pos < 4094; k++) buf[pos++] = nb[k];
                    break;
                }
                case 'x': {
                    unsigned int val = (unsigned int)va_arg(args, unsigned int);
                    char nb[16];
                    int nl = uintptr_to_hex(nb, (unsigned long)val);
                    for (int k = 0; k < nl && pos < 4094; k++) buf[pos++] = nb[k];
                    break;
                }
                case 'p': {
                    void *ptr = va_arg(args, void *);
                    if (ptr == NULL) { pos = append_str(buf, pos, "(nil)"); }
                    else {
                        buf[pos++] = '0'; buf[pos++] = 'x';
                        char nb[17];
                        int nl = uintptr_to_hex(nb, (unsigned long)ptr);
                        for (int k = 0; k < nl && pos < 4094; k++) buf[pos++] = nb[k];
                    }
                    break;
                }
                case 'c': {
                    int ch = va_arg(args, int);
                    pos = append_char(buf, pos, (char)ch);
                    break;
                }
                case '%': {
                    pos = append_char(buf, pos, '%');
                    break;
                }
                default: {
                    buf[pos++] = '%';
                    buf[pos++] = *fmt;
                    break;
                }
            }
            fmt++;
        } else {
            buf[pos++] = *fmt++;
        }
    }

    va_end(args);

    long result = sys_write(STDOUT_FILENO, buf, (size_t)pos);
    if (result < 0) return -1;
    return (int)result;
}

/* ==================================================================
 *  End of libc_minimal.c
 * ================================================================== */
