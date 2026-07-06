/*
 * libc_minimal.c -- Minimal C runtime for tc-compiled programs.
 *
 * Provides: putstr(), printf(), exit()
 * Uses only Linux x86-64 syscalls. No glibc dependency.
 *
 * Variadic support: manual va_list as char * with pointer arithmetic.
 * All variadic args are assumed to be on the stack at 8-byte intervals.
 * (x86-64 SysV ABI register args are not handled; this is intentional
 *  for tc compatibility where all args are stack-passed.)
 */

/* ------------------------------------------------------------------ */
/*  Syscall numbers (Linux x86-64)                                    */
/* ------------------------------------------------------------------ */

#define SYS_WRITE      1
#define SYS_EXIT_GROUP 231

/* ------------------------------------------------------------------ */
/*  Inline syscall helpers                                            */
/* ------------------------------------------------------------------ */

static inline long sys_write(long fd, const void *buf, long count) {
    long ret;
    __asm__ __volatile__("syscall"
        : "=a"(ret)
        : "a"(SYS_WRITE), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory");
    return ret;
}

static inline __attribute__((noreturn)) void sys_exit_group(long code) {
    __asm__ __volatile__("syscall"
        :
        : "a"(SYS_EXIT_GROUP), "D"(code)
        : "rcx", "r11", "memory");
    for (;;)
        ;
}

/* ------------------------------------------------------------------ */
/*  putstr -- write a null-terminated string to stdout (fd 1)         */
/* ------------------------------------------------------------------ */

int putstr(const char *s) {
    long len = 0;
    while (s[len])
        len++;
    if (len == 0)
        return 0;
    return (int)sys_write(1, s, len);
}

/* ------------------------------------------------------------------ */
/*  exit -- terminate process via exit_group                          */
/* ------------------------------------------------------------------ */

__attribute__((noreturn)) void exit(int code) {
    sys_exit_group((long)code);
}

/* ------------------------------------------------------------------ */
/*  Minimal va_list -- char * walking the stack                       */
/* ------------------------------------------------------------------ */

typedef char *va_list;

/*
 * va_start: point ap just past the last named argument on the stack.
 * va_arg  : advance by 8 bytes (x86-64 slot size), then read the value.
 * va_end  : no-op (nothing to clean up).
 */

#define va_start(ap, last)  ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type)    ((ap) += 8, *(type *)((ap) - 8))
#define va_end(ap)          ((void)0)

/* ------------------------------------------------------------------ */
/*  Internal output helpers                                           */
/* ------------------------------------------------------------------ */

static void putbyte(char c) {
    sys_write(1, &c, 1);
}

static void putbuf(const char *s, long len) {
    if (len > 0)
        sys_write(1, s, len);
}

/* ------------------------------------------------------------------ */
/*  Integer-to-string conversion (buffer must be >= 24 bytes)         */
/* ------------------------------------------------------------------ */

static char *itoa(char *buf, long long val, int base, int upper) {
    static const char digs_lo[] = "0123456789abcdef";
    static const char digs_hi[] = "0123456789ABCDEF";
    const char *digs = upper ? digs_hi : digs_lo;

    char tmp[24];
    char *tp = tmp;
    long long n = val;
    int neg = 0;

    if (base == 10 && n < 0) {
        neg = 1;
        n = -n;
    }

    do {
        *tp++ = digs[(unsigned long long)n % (unsigned long long)base];
        n /= base;
    } while (n > 0);

    if (neg)
        *tp++ = '-';

    char *p = buf;
    while (tp > tmp)
        *p++ = *--tp;
    *p = '\0';
    return buf;
}

static char *ptostr(char *buf, unsigned long long val) {
    char *p = buf;
    *p++ = '0';
    *p++ = 'x';
    char tmp[17];
    char *tp = tmp;
    unsigned long long n = val;
    do {
        unsigned d = n & 0xf;
        *tp++ = d < 10 ? ('0' + (char)d) : ('a' + (char)(d - 10));
        n >>= 4;
    } while (n > 0);
    while (tp > tmp)
        *p++ = *--tp;
    *p = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/*  printf -- formatted output to stdout                              */
/* ------------------------------------------------------------------ */

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int total = 0;
    char buf[32];

    while (*fmt) {
        if (*fmt != '%') {
            char c = *fmt++;
            putbyte(c);
            total++;
            continue;
        }

        fmt++; /* skip '%' */

        /* '%%' -> literal '%' */
        if (*fmt == '%') {
            putbyte('%');
            total++;
            fmt++;
            continue;
        }

        switch (*fmt) {
            case 'd': {
                long long val = va_arg(ap, long long);
                char *s = itoa(buf, val, 10, 0);
                long len = 0;
                while (s[len])
                    len++;
                putbuf(s, len);
                total += (int)len;
                break;
            }
            case 'u': {
                unsigned long long val = va_arg(ap, unsigned long long);
                char *s = itoa(buf, (long long)val, 10, 0);
                long len = 0;
                while (s[len])
                    len++;
                putbuf(s, len);
                total += (int)len;
                break;
            }
            case 'c': {
                int ch = va_arg(ap, int);
                putbyte((char)ch);
                total++;
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (s) {
                    long len = 0;
                    while (s[len])
                        len++;
                    putbuf(s, len);
                    total += (int)len;
                }
                break;
            }
            case 'x': {
                unsigned long long val = va_arg(ap, unsigned long long);
                char *s = itoa(buf, (long long)val, 16, 0);
                long len = 0;
                while (s[len])
                    len++;
                putbuf(s, len);
                total += (int)len;
                break;
            }
            case 'p': {
                unsigned long long ptr = va_arg(ap, unsigned long long);
                char *s = ptostr(buf, ptr);
                long len = 0;
                while (s[len])
                    len++;
                putbuf(s, len);
                total += (int)len;
                break;
            }
            default: {
                putbyte('%');
                putbyte(*fmt);
                total += 2;
                break;
            }
        }
        fmt++;
    }

    va_end(ap);
    return total;
}
