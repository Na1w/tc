# crt0.S - C Runtime Startup for x86-64 Linux
# Entry point called by kernel with: [rsp] = argc, [rsp+8] = argv[]
.global _start
.text
.align 16
_start:
    # Align stack to 16 bytes (push rbp makes it 8-byte aligned, then align)
    and $-16, %rsp
    
    # Pass argc, argv to main
    mov (%rsp), %rdi        # argc
    lea 8(%rsp), %rsi       # argv
    
    # Call main
    call main
    
    # Exit with return value from main
    mov %rax, %rdi          # exit code
    mov $231, %rax          # SYS_exit_group
    syscall
