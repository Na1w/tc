.intel_syntax noprefix
.text
.globl main
double:
    ; param 0
    lea rax, [rbp+8*1]
    mov [rbp-8*3], rax
    mov rax, [rbp-8*3]
    mov rax, [rax]
    mov [rbp-8*1], rax
    mov rax, 2
    mov [rbp-8*2], rax
    mov rax, [rbp-8*1]
    imul rax, [rbp-8*2]
    mov [rbp-8*3], rax
    mov rax, [rbp-8*3]
    leave
    ret
main:
    mov rax, 7
    mov [rbp-8*4], rax
    mov rax, [rbp-8*4]
    push rax
    call double
    add rsp, 8
    mov [rbp-8*5], rax
    mov rax, [rbp-8*5]
    leave
    ret
