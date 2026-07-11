.intel_syntax noprefix
.text
.globl main
main:
    ; alloc 4 bytes on stack
    mov rax, 5
    mov [rbp-8*1], rax
    lea rax, [rbp+-4]
    mov [rbp-8*4], rax
    mov rax, [rbp-8*1]
    mov rcx, [rbp-8*4]
    mov [rcx], rax
    ; alloc 4 bytes on stack
    mov rax, 7
    mov [rbp-8*2], rax
    lea rax, [rbp+-8]
    mov [rbp-8*8], rax
    mov rax, [rbp-8*2]
    mov rcx, [rbp-8*8]
    mov [rcx], rax
    ; alloc 4 bytes on stack
    lea rax, [rbp+0]
    mov [rbp-8*11], rax
    mov rax, [rbp-8*11]
    mov rax, [rax]
    mov [rbp-8*3], rax
    lea rax, [rbp+0]
    mov [rbp-8*13], rax
    mov rax, [rbp-8*13]
    mov rax, [rax]
    mov [rbp-8*4], rax
    mov rax, [rbp-8*3]
    add rax, [rbp-8*4]
    mov [rbp-8*5], rax
    lea rax, [rbp+-12]
    mov [rbp-8*16], rax
    mov rax, [rbp-8*5]
    mov rcx, [rbp-8*16]
    mov [rcx], rax
    lea rax, [rbp+0]
    mov [rbp-8*18], rax
    mov rax, [rbp-8*18]
    mov rax, [rax]
    mov [rbp-8*6], rax
    mov rax, [rbp-8*6]
    leave
    ret
