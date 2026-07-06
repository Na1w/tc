#ifndef TC_ELF_H
#define TC_ELF_H

#include <stddef.h>
#include <stdint.h>

/*
 * elf.h -- ELF64 executable writer for the tc C compiler.
 *
 * Produces a valid ELF64 ET_EXEC (position-dependent executable)
 * targeting Linux x86-64.  No relocation support; symbols are
 * resolved at compile-time using known virtual addresses.
 *
 * Memory layout:
 *   0x401000  .text    (executable machine code)
 *   0x402000  .rodata  (strings, constants)
 *   0x600000  .data    (initialized global variables)
 *   0x601000  .bss     (zero-initialized globals, file size = 0)
 */

/* ---------------  public constants  --------------- */

#define ELF_TEXT_BASE   0x401000U
#define ELF_RODATA_BASE 0x402000U
#define ELF_DATA_BASE   0x600000U
#define ELF_BSS_BASE    0x601000U

/* ---------------  opaque handle  --------------- */

typedef struct {
    unsigned char *data;
    size_t         size;
    size_t         cap;
} ElfSection;

/* Symbol-table entry */
typedef struct ElfSym {
    char    name[256];
    uint64_t addr;
} ElfSym;

typedef struct ElfWriter {
    ElfSection  text;
    ElfSection  rodata;
    ElfSection  data;
    size_t      bss_size;          /* zero-init bytes */

    ElfSym     *syms;
    size_t      sym_count;
    size_t      sym_cap;

    uint64_t    entry;             /* virtual address of entry point */
} ElfWriter;

/* ---------------  lifecycle  --------------- */

ElfWriter *elf_create(void);
void       elf_destroy(ElfWriter *elf);

/* ---------------  section helpers  --------------- */

void elf_add_text(ElfWriter *elf, const unsigned char *bytes, size_t len);
void elf_add_rodata(ElfWriter *elf, const unsigned char *bytes, size_t len);
void elf_add_data(ElfWriter *elf, const unsigned char *bytes, size_t len,
                  int is_zero_init);

/* ---------------  symbol table  --------------- */

/* Register a symbol at a known virtual address. */
void elf_define_symbol(ElfWriter *elf, const char *name, uint64_t addr);

/* Lookup a previously-defined symbol.  Returns -1 on failure. */
int64_t elf_lookup_symbol(ElfWriter *elf, const char *name);

/* ---------------  output  --------------- */

/* Write the complete ELF64 executable to *path*.  Returns 0 on success. */
int elf_write(ElfWriter *elf, const char *path);

/* ---------------  base-address queries  --------------- */

static inline size_t elf_text_base(void)   { return ELF_TEXT_BASE; }
static inline size_t elf_rodata_base(void) { return ELF_RODATA_BASE; }
static inline size_t elf_data_base(void)   { return ELF_DATA_BASE; }
static inline size_t elf_bss_base(void)    { return ELF_BSS_BASE; }

#endif /* TC_ELF_H */
