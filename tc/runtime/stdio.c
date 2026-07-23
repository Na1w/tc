/*
 * stdio.c -- printf/puts implementation module for the tc runtime.
 *
 * Minimal ANSI C implementation of standard I/O functions required by
 * the tc compiler's target runtime.  All symbols are exported (public).
 *
 * Functions provided:
 *   int    puts(const char *)
 *   int    printf(const char *, ...)
 *
 * Dependencies:
 *   "syscall.h"       -- sys_write(), sys_exit(), size_t
 *   <stdarg.h>        -- va_list, va_start, va_arg, va_end
 */

#include "syscall.h"
#include <stdarg.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ------------------------------------------------------------------ */
/*  Internal helpers (static)                                         */
/* ------------------------------------------------------------------ */

/*
 * str_length -- compute length of a null-terminated string.
 */
static size_t str_length(const char *s)
{
    size_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

/*
 * int_to_buf -- convert a signed integer to a decimal string in buf.
 *   Returns the number of characters written (excluding null terminator).
 */
static int int_to_buf(char *buf, int value)
{
    char tmp[16];
    int i = 0;
    int negative = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    if (value < 0) {
        negative = 1;
        /* Use unsigned arithmetic to avoid overflow on INT_MIN */
        unsigned int uval = (unsigned int)(-(long)value);
        while (uval > 0) {
            tmp[i++] = '0' + (uval % 10);
            uval /= 10;
        }
    } else {
        unsigned int uval = (unsigned int)value;
        while (uval > 0) {
            tmp[i++] = '0' + (uval % 10);
            uval /= 10;
        }
    }

    if (negative)
        buf[0] = '-';

    /* Reverse tmp into buf */
    int j = negative ? 1 : 0;
    for (int k = i - 1; k >= 0; k--)
        buf[j++] = tmp[k];
    buf[j] = '\0';
    return j;
}

/*
 * uint_to_buf -- convert an unsigned integer to a decimal string in buf.
 *   Returns the number of characters written (excluding null terminator).
 */
static int uint_to_buf(char *buf, unsigned int value)
{
    char tmp[11];
    int i = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    while (value > 0) {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    }

    /* Reverse tmp into buf */
    for (int k = i - 1, j = 0; k >= 0; k--, j++)
        buf[j] = tmp[k];
    buf[i] = '\0';
    return i;
}

/*
 * uintptr_to_hex -- convert an unsigned long (pointer value) to hex string.
 *   Returns the number of characters written (excluding null terminator).
 */
static int uintptr_to_hex(char *buf, unsigned long value)
{
    const char hexdigits[] = "0123456789abcdef";
    char tmp[17];
    int i = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    while (value > 0 && i < 16) {
        tmp[i++] = hexdigits[value & 0xF];
        value >>= 4;
    }

    /* Reverse tmp into buf */
    for (int k = i - 1, j = 0; k >= 0; k--, j++)
        buf[j] = tmp[k];
    buf[i] = '\0';
    return i;
}

/*
 * append_char -- append a single character to buf at position pos.
 *   Returns the new position.
 */
static int append_char(char *buf, int pos, char c)
{
    buf[pos] = c;
    return pos + 1;
}

/*
 * append_str -- append a null-terminated string to buf at position pos.
 *   Returns the new position.
 */
static int append_str(char *buf, int pos, const char *s)
{
    if (s == NULL)
        s = "(null)";
    while (*s != '\0')
        buf[pos++] = *s++;
    return pos;
}

/* ------------------------------------------------------------------ */
/*  puts -- write a string followed by a newline to stdout             */
/* ------------------------------------------------------------------ */

/*
 * puts(const char *str)
 *
 * Writes the null-terminated string STR to stdout (fd 1), followed by
 * a newline character ('\n').  Returns the total number of bytes written,
 * or -1 on error.
 *
 * Implementation:
 *   1. Compute length of str using str_length().
 *   2. Call sys_write(1, str, len).
 *   3. Call sys_write(1, "\n", 1).
 */
int puts(const char *str)
{
    if (str == NULL)
        return -1;

    size_t len = str_length(str);

    long result = sys_write(STDOUT_FILENO, str, len);
    if (result < 0)
        return (int)result;

    result = sys_write(STDOUT_FILENO, "\n", 1);
    if (result < 0)
        return (int)result;

    return (int)(len + 1);
}

/* ------------------------------------------------------------------ */
/*  printf -- format and print to stdout                               */
/* ------------------------------------------------------------------ */

/*
 * printf(const char *fmt, ...)
 *
 * Formats arguments according to the format string FMT and writes the
 * result to stdout (fd 1).
 *
 * Supported format specifiers:
 *   %s  -- null-terminated string (const char *)
 *   %d  -- signed decimal integer (int)
 *   %u  -- unsigned decimal integer (unsigned int)
 *   %x  -- unsigned hexadecimal (unsigned int, lowercase)
 *   %p  -- pointer value (void *, printed as hex with "0x" prefix)
 *   %c  -- single character (int, promoted from char)
 *   %%  -- literal percent sign
 *
 * Implementation:
 *   1. Use va_list to iterate over variadic arguments.
 *   2. Build formatted output in a fixed-size buffer (4096 bytes).
 *   3. Call sys_write(1, buf, len) to write the result.
 *   4. Return the number of bytes written, or -1 on error.
 */
int printf(const char *fmt, ...)
{
    char buf[4096];
    int pos = 0;
    va_list args;

    if (fmt == NULL)
        return -1;

    va_start(args, fmt);

    while (*fmt != '\0') {
        /* Safety: prevent buffer overflow */
        if (pos >= 4094)
            break;

        if (*fmt == '%') {
            fmt++; /* skip '%' */

            if (*fmt == '\0') {
                /* Trailing % with no specifier -- output literal % */
                buf[pos++] = '%';
                break;
            }

            switch (*fmt) {
                case 's': {
                    /* %s -- string */
                    const char *s = va_arg(args, const char *);
                    pos = append_str(buf, pos, s);
                    break;
                }

                case 'd': {
                    /* %d -- signed decimal integer */
                    int val = va_arg(args, int);
                    char numbuf[16];
                    int nlen = int_to_buf(numbuf, val);
                    for (int k = 0; k < nlen && pos < 4094; k++)
                        buf[pos++] = numbuf[k];
                    break;
                }

                case 'u': {
                    /* %u -- unsigned decimal integer */
                    unsigned int val = (unsigned int)va_arg(args, unsigned int);
                    char numbuf[11];
                    int nlen = uint_to_buf(numbuf, val);
                    for (int k = 0; k < nlen && pos < 4094; k++)
                        buf[pos++] = numbuf[k];
                    break;
                }

                case 'x': {
                    /* %x -- unsigned hexadecimal (lowercase) */
                    unsigned int val = (unsigned int)va_arg(args, unsigned int);
                    char numbuf[16];
                    int nlen = uintptr_to_hex(numbuf, (unsigned long)val);
                    for (int k = 0; k < nlen && pos < 4094; k++)
                        buf[pos++] = numbuf[k];
                    break;
                }

                case 'p': {
                    /* %p -- pointer (hex with "0x" prefix) */
                    void *ptr = va_arg(args, void *);
                    if (ptr == NULL) {
                        pos = append_str(buf, pos, "(nil)");
                    } else {
                        buf[pos++] = '0';
                        buf[pos++] = 'x';
                        char numbuf[17];
                        int nlen = uintptr_to_hex(numbuf, (unsigned long)ptr);
                        for (int k = 0; k < nlen && pos < 4094; k++)
                            buf[pos++] = numbuf[k];
                    }
                    break;
                }

                case 'c': {
                    /* %c -- single character */
                    int ch = va_arg(args, int);
                    pos = append_char(buf, pos, (char)ch);
                    break;
                }

                case '%': {
                    /* %% -- literal percent sign */
                    pos = append_char(buf, pos, '%');
                    break;
                }

                default: {
                    /* Unknown specifier: output '%' followed by the char */
                    buf[pos++] = '%';
                    buf[pos++] = *fmt;
                    break;
                }
            }

            fmt++; /* advance past the specifier */
        } else {
            /* Regular character -- copy literally */
            buf[pos++] = *fmt;
            fmt++;
        }
    }

    va_end(args);

    /* Write the formatted buffer to stdout */
    long result = sys_write(STDOUT_FILENO, buf, (size_t)pos);
    if (result < 0)
        return -1;

    return (int)result;
}

/* ------------------------------------------------------------------ */
/*  putstr -- write string directly to stdout (NO trailing newline)    */
/* ------------------------------------------------------------------ */
void putstr(const char *s)
{
    if (s != NULL)
        sys_write(STDOUT_FILENO, s, str_length(s));
}
