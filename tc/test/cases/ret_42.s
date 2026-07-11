.intel_syntax noprefix
.text
.globl main
main:
    mov rax, 42
    mov [rbp-8*1], rax
    mov rax, [rbp-8*1]
    leave
    ret
