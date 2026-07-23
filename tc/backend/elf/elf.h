#ifndef TC_ELF_H
#define TC_ELF_H

#include <stddef.h>
#include <stdint.h>

/*
 * elf.h -- ELF64 writer for the tc C compiler.
 *
 * Produces either:
 *   - ELF64 ET_EXEC (position-dependent executable)
 *   - ELF64 ET_REL  (relocatable object file, -c flag)
 *
 * Memory layout (ET_EXEC only):
 *   0x401000  .text    (executable machine code)
 *   0x402000  .rodata  (strings, constants)
 *   0x600000  .data    (initialized global variables)
 *   0x601000  .bss     (zero-initialized globals, file size = 0)
 */

/* ---------------  public constants  --------------- */

#define ELF_TEXT_BASE   0x401000U
#define ELF_RODATA_BASE 0x410000U
#define ELF_DATA_BASE   0x600000U
#define ELF_BSS_BASE    0x601000U

/* ELF symbol binding and type */
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2

/* x86-64 relocation types */
#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_X86_64_GOT32    3
#define R_X86_64_PLT32    4
#define R_X86_64_COPY     5
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_GOTPCREL 9

/* ---------------  opaque handle  --------------- */

typedef struct {
    unsigned char *data;
    size_t         size;
    size_t         cap;
} ElfSection;

/* Symbol-table entry (enhanced for object mode) */
typedef struct ElfSym {
    char    name[256];
    uint64_t addr;        /* virtual address (EXEC) or section offset (REL) */
    uint16_t section;     /* section index this symbol belongs to */
    uint8_t  bind;        /* STB_LOCAL=0, STB_GLOBAL=1, STB_WEAK=2 */
    uint8_t  type;        /* STT_NOTYPE=0, STT_OBJECT=1, STT_FUNC=2 */
    int      is_extern;   /* 1 if this symbol is undefined (imported) */
} ElfSym;

/* Relocation entry (R_X86_64 relocations) */
typedef struct ElfRela {
    uint64_t offset;      /* offset within the section */
    uint64_t info;        /* (sym_index << 32) | reloc_type */
    int64_t  addend;      /* addend value */
} ElfRela;

typedef struct ElfWriter {
    ElfSection  text;
    ElfSection  rodata;
    ElfSection  data;
    size_t      bss_size;          /* zero-init bytes */

    ElfSym     *syms;
    size_t      sym_count;
    size_t      sym_cap;

    /* Relocation sections (one per content section) */
    ElfRela    *rela_text;
    size_t      rela_text_count;
    size_t      rela_text_cap;

    ElfRela    *rela_data;
    size_t      rela_data_count;
    size_t      rela_data_cap;

    uint64_t    entry;             /* virtual address of entry point */
    int         object_mode;       /* 1 = ET_REL, 0 = ET_EXEC */
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

/* Register a symbol at a known virtual address (EXEC mode). */
void elf_define_symbol(ElfWriter *elf, const char *name, uint64_t addr);

/* Lookup a previously-defined symbol. Returns -1 if not found. */
int64_t elf_lookup_symbol(ElfWriter *elf, const char *name);

/* ---------------  object mode (-c)  --------------- */

/* Enable ET_REL output mode. */
void elf_set_object_mode(ElfWriter *elf, int mode);

/*
 * Define a symbol for object mode.
 *   section: section index (1=.text, 2=.rodata, 3=.data, 4=.bss)
 *            or 0 for undefined/external symbols
 *   offset:  offset within that section (0 for external)
 *   bind:    STB_LOCAL (0), STB_GLOBAL (1), STB_WEAK (2)
 *   type:    STT_NOTYPE (0), STT_OBJECT (1), STT_FUNC (2)
 */
void elf_define_symbol_obj(ElfWriter *elf,
                           const char *name,
                           uint16_t section,
                           uint64_t offset,
                           uint8_t bind,
                           uint8_t type);

/*
 * Record an external (undefined) symbol reference.
 * The symbol will appear in .symtab with shndx=SHN_UNDEF.
 * Returns the symbol index assigned.
 */
size_t elf_extern_symbol(ElfWriter *elf, const char *name);

/* ---------------  relocations  --------------- */

/*
 * Add a relocation to .rela.text.
 *   offset: byte offset within .text
 *   sym_idx: index of the target symbol in the symbol table
 *   type: R_X86_64 relocation type (e.g., R_X86_64_PC32, R_X86_64_64)
 *   addend: addend value
 */
void elf_add_rela_text(ElfWriter *elf,
                       uint64_t offset,
                       size_t sym_idx,
                       uint32_t type,
                       int64_t addend);

/*
 * Add a relocation to .rela.data.
 */
void elf_add_rela_data(ElfWriter *elf,
                       uint64_t offset,
                       size_t sym_idx,
                       uint32_t type,
                       int64_t addend);

/* ---------------  write  --------------- */

/* Write ELF to file. Produces ET_EXEC or ET_REL depending on object_mode. */
int elf_write(ElfWriter *elf, const char *path);

#endif
