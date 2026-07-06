.intel_syntax noprefix
.text
.globl main
main:
    mov rax, 0
    mov [rbp-8*1], rax
    mov rax, [rbp-8*1]
    leave
    ret
