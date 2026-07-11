.intel_syntax noprefix
.text
.globl main
main:
    ; alloc 4 bytes on stack
    mov rax, 2
    mov [rbp-8*1], rax
    lea rax, [rbp+-4]
    mov [rbp-8*4], rax
    mov rax, [rbp-8*1]
    mov rcx, [rbp-8*4]
    mov [rcx], rax
    ; alloc 4 bytes on stack
    mov rax, 3
    mov [rbp-8*2], rax
    lea rax, [rbp+-8]
    mov [rbp-8*8], rax
    mov rax, [rbp-8*2]
    mov rcx, [rbp-8*8]
    mov [rcx], rax
    lea rax, [rbp+0]
    mov [rbp-8*10], rax
    mov rax, [rbp-8*10]
    mov rax, [rax]
    mov [rbp-8*3], rax
    lea rax, [rbp+0]
    mov [rbp-8*12], rax
    mov rax, [rbp-8*12]
    mov rax, [rax]
    mov [rbp-8*4], rax
    mov rax, [rbp-8*3]
    add rax, [rbp-8*4]
    mov [rbp-8*5], rax
    mov rax, [rbp-8*5]
    leave
    ret
