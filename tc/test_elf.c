/*
 * test_elf.c -- Verify the ELF writer produces valid ELF64 executables
 *
 * Manually constructs a tiny "Hello, World!" program using raw syscalls:
 *   write(1, msg, len)   -> syscall 1
 *   exit(0)              -> syscall 60
 * Then uses elf_writer to package it into a real ELF64 executable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "src/elf.h"

/* Emit bytes into a buffer */
static unsigned char code_buf[4096];
static size_t code_pos = 0;

static void emit_byte(unsigned char b)
{
    code_buf[code_pos++] = b;
}

static void emit_u64(uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        code_buf[code_pos++] = (unsigned char)(v & 0xff);
        v >>= 8;
    }
}

/* Encode a syscall: syscall number in rax, args in rdi, rsi, rdx */
static void emit_syscall(uint64_t rax_val, uint64_t rdi_val,
                         uint64_t rsi_val, uint64_t rdx_val)
{
    /* mov rax, <syscall_num> */
    emit_byte(0x48); emit_byte(0xb8); /* mov rax, imm64 */
    emit_u64(rax_val);

    /* mov rdi, <arg1> */
    emit_byte(0x48); emit_byte(0xbf); /* mov rdi, imm64 */
    emit_u64(rdi_val);

    /* mov rsi, <arg2> */
    emit_byte(0x48); emit_byte(0xbe); /* mov rsi, imm64 */
    emit_u64(rsi_val);

    /* mov rdx, <arg3> */
    emit_byte(0x48); emit_byte(0xba); /* mov rdx, imm64 */
    emit_u64(rdx_val);

    /* syscall */
    emit_byte(0x0f); emit_byte(0x05);
}

int main(void)
{
    const char *hello = "Hello, World!\n";
    size_t hello_len = strlen(hello);

    /* Build the machine code */
    code_pos = 0;

    /* write(1, hello_addr, hello_len) */
    emit_syscall(1, 1, ELF_RODATA_BASE, hello_len);

    /* exit(0) */
    emit_syscall(60, 0, 0, 0);

    printf("Generated %zu bytes of machine code\n", code_pos);

    /* Disassemble-like dump */
    printf("Machine code hex dump:\n");
    for (size_t i = 0; i < code_pos; i++) {
        printf("%02x ", code_buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    /* Create ELF writer */
    ElfWriter *elf = elf_create();
    if (!elf) {
        fprintf(stderr, "Failed to create ELF writer\n");
        return 1;
    }

    /* Entry point is auto-calculated by elf_write based on text vaddr.
     * We leave it at the default (ELF_TEXT_BASE) so elf_write recalculates. */

    /* Add code */
    elf_add_text(elf, code_buf, code_pos);

    /* Add string to rodata */
    elf_add_rodata(elf, (const unsigned char *)hello, hello_len + 1);

    /* Define symbols for verification */
    elf_define_symbol(elf, "main", ELF_TEXT_BASE);
    elf_define_symbol(elf, "hello_str", ELF_RODATA_BASE);

    /* Write the ELF */
    const char *output = "test_elf_output";
    if (elf_write(elf, output) != 0) {
        fprintf(stderr, "Failed to write ELF\n");
        elf_destroy(elf);
        return 1;
    }

    printf("Wrote ELF to %s\n", output);

    /* Verify symbol lookup */
    int64_t main_addr = elf_lookup_symbol(elf, "main");
    int64_t hello_addr = elf_lookup_symbol(elf, "hello_str");
    printf("Symbol 'main' at 0x%lx\n", (unsigned long)main_addr);
    printf("Symbol 'hello_str' at 0x%lx\n", (unsigned long)hello_addr);

    /* Make executable */
    chmod(output, 0755);

    elf_destroy(elf);

    printf("\nNow run: file %s && %s\n", output, output);
    return 0;
}
