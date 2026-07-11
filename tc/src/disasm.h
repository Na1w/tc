#ifndef TC_DISASM_H
#define TC_DISASM_H

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

/*
 * disasm.h -- Capstone disassembler wrapper for the tc compiler project.
 *
 * Provides a thin wrapper around libcapstone for x86-64 disassembly.
 * When HAVE_CAPSTONE is defined, uses libcapstone directly.
 * Otherwise falls back to a plain hex dump.
 */

/*
 * Disassemble a block of x86-64 machine code and print the result to out.
 *
 * Parameters:
 *   code  - Pointer to the machine-code bytes.
 *   len   - Number of bytes in the buffer.
 *   addr  - Virtual address of the first byte (for PC-relative display).
 *   out   - Output stream (typically stderr for debug output).
 */
void disasm_x86_64(const uint8_t *code, size_t len, uint64_t addr, FILE *out);

#endif /* TC_DISASM_H */
