#ifndef TC_LINKER_H
#define TC_LINKER_H

#include <stddef.h>
#include <stdint.h>

/*
 * linker.h -- Minimal ELF64 static linker for the tc C compiler.
 *
 * Reads multiple ET_REL object files (.o), resolves symbols, applies
 * relocations, and produces an ET_EXEC executable.
 *
 * Supported relocations:
 *   R_X86_64_PC32  (type 2)  -- PC-relative 32-bit  (S + A - P)
 *   R_X86_64_PLT32 (type 4)  -- PLT-relative 32-bit (treated as PC32 for static)
 *   R_X86_64_64    (type 1)  -- Absolute 64-bit     (S + A)
 */

/* ---------------  opaque handle  --------------- */

typedef struct Linker Linker;

/* ---------------  lifecycle  --------------- */

/*
 * Create a new linker instance.
 */
Linker *linker_create(void);

/*
 * Destroy a linker instance, freeing all memory.
 */
void linker_destroy(Linker *linker);

/* ---------------  input  --------------- */

/*
 * Add an ELF object file (.o) to the linker input.
 * Returns 0 on success, -1 on error (message in linker->error).
 */
int linker_add_object(Linker *linker, const char *path);

/*
 * Add all object files from a static library archive (.a) to the linker
 * input. Returns 0 on success, -1 on error.
 */
int linker_add_archive(Linker *linker, const char *path);

/* ---------------  linking  --------------- */

/*
 * Set the entry point symbol name (default: "main").
 */
void linker_set_entry(Linker *linker, const char *symbol);

/*
 * Set the output base address for .text section.
 * Default is 0x401000 (standard Linux x86-64 executable base).
 */
void linker_set_base(Linker *linker, uint64_t text_base);

/*
 * Perform the link: resolve symbols, apply relocations, and write
 * the output executable. Returns 0 on success, -1 on error.
 */
int linker_link(Linker *linker, const char *output_path);

/* ---------------  error reporting  --------------- */

/*
 * Get the last error message. Returns NULL if no error.
 */
const char *linker_error(Linker *linker);

/* ---------------  utility  --------------- */

/*
 * Print a summary of loaded objects and unresolved symbols
 * (useful for debugging).
 */
void linker_print_status(Linker *linker);

#endif
