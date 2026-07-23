/*
 * syscall_wrapper.c -- Inline-assembly syscall wrappers for x86-64 Linux.
 *
 * Strictly ANSI C compatible (with __asm__ extension for inline assembly).
 * Targets the Linux syscall interface on x86-64 using the SysV ABI.
 *
 * Register convention (SysV ABI -> Linux syscall):
 *   RAX = syscall number
 *   RDI = argument 1
 *   RSI = argument 2
 *   RDX = argument 3
 *   R10 = argument 4
 *   R8  = argument 5
 *   R9  = argument 6
 *
 * The syscall instruction clobbers RCX and R11 (kernel use).
 * Stack must be 16-byte aligned before the syscall instruction.
 *
 * The compiler-generated prologue typically ensures 16-byte alignment
 * at function entry (after the call instruction pushes an 8-byte return
 * address, making the stack 8 mod 16). We therefore insert a single
 * NOP-aligned padding push/pop pair to restore 16-byte alignment before
 * the syscall, or simply rely on the fact that leaf functions with no
 * local variables maintain the alignment invariant.
 *
 * For safety, we use explicit clobber lists so the compiler knows which
 * registers are modified.
 */

#include "syscall.h"

/* ----------------------------------------------------------------
 * sys_write(fd, buf, count)
 *
 * Performs the write(2) syscall.
 *
 * SysV ABI input registers on entry:
 *   RDI = fd
 *   RSI = buf
 *   RDX = count
 *
 * We load RAX with SYS_WRITE and issue the syscall.
 * ---------------------------------------------------------------- */

long sys_write(int fd, const char *buf, size_t count)
{
    long result;

    __asm__ __volatile__ (
        "syscall"
        : "=a"(result)                          /* output: RAX -> result */
        : "a"(SYS_WRITE),                       /* input:  RAX = syscall nr */
          "D"(fd),                              /* input:  RDI = fd */
          "S"(buf),                             /* input:  RSI = buf */
          "d"(count)                            /* input:  RDX = count */
        : "rcx", "r11", "memory"               /* clobbers: RCX, R11, memory */
    );

    return result;
}

/* ----------------------------------------------------------------
 * sys_exit(code)
 *
 * Performs the exit_group(231) syscall to terminate the process.
 * This function does not return.
 *
 * SysV ABI input register on entry:
 *   RDI = code
 *
 * We load RAX with SYS_EXIT_GROUP and issue the syscall.
 * ---------------------------------------------------------------- */

void sys_exit(int code)
{
    __asm__ __volatile__ (
        "syscall"
        :                                         /* no outputs -- never returns */
        : "a"(SYS_EXIT_GROUP),                    /* input:  RAX = syscall nr */
          "D"(code)                               /* input:  RDI = exit code */
        : "rcx", "r11", "memory"                 /* clobbers: RCX, R11, memory */
    );

    /* Unreachable, but required for well-formed C. */
    __asm__ __volatile__ ("ud2");                /* illegal instruction as abort */
}
