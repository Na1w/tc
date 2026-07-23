/*
 * syscall.S -- Minimal x86-64 Linux syscall wrapper.
 *
 * Provides: long sys_invoke(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
 *
 * SysV ABI incoming:
 *   RDI=nr, RSI=a0, RDX=a1, RCX=a2, R8=a3, R9=a4, [rsp+8]=a5
 *
 * Linux syscall convention:
 *   RAX=nr, RDI=a0, RSI=a1, RDX=a2, R10=a3, R8=a4, R9=a5
 *
 * The syscall instruction clobbers RCX and R11.
 */

        .text
        .globl sys_invoke
        .type  sys_invoke, @function

sys_invoke:
        pushq   %rdi            /* save nr */
        pushq   %rsi            /* save a0 */
        pushq   %rdx            /* save a1 */
        pushq   %rcx            /* save a2 (clobbered by syscall) */
        pushq   %r8             /* save a3 */
        pushq   %r9             /* save a4 */

        /* Stack now: [a4, a3, a2, a1, a0, nr] + [ret_addr, a5] */
        /* Offset from rsp: a4=0, a3=8, a2=16, a1=24, a0=32, nr=40, ret=48, a5=56 */

        movq    40(%rsp), %rax  /* nr -> RAX */
        movq    32(%rsp), %rdi  /* a0 -> RDI */
        movq    24(%rsp), %rsi  /* a1 -> RSI */
        movq    16(%rsp), %rdx  /* a2 -> RDX */
        movq    8(%rsp),  %r10  /* a3 -> R10 */
        movq    0(%rsp),  %r8   /* a4 -> R8  */
        movq    56(%rsp), %r9   /* a5 -> R9  */

        syscall

        popq    %r9
        popq    %r8
        popq    %rcx
        popq    %rdx
        popq    %rsi
        popq    %rdi
        ret

        .size  sys_invoke, . - sys_invoke
