/*
 * elf.c -- ELF64 writer for the tc C compiler
 *
 * Emits either:
 *   - ET_EXEC: position-dependent executable (default)
 *   - ET_REL:  relocatable object file (-c flag)
 *
 * Targets Linux x86-64 System V ABI.
 */

#include "elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  helpers                                                            */
/* ------------------------------------------------------------------ */

static void section_grow(ElfSection *sec, size_t need)
{
    if (sec->size + need <= sec->cap)
        return;
    size_t new_cap = sec->cap ? sec->cap * 2 : 4096;
    while (new_cap < sec->size + need)
        new_cap *= 2;
    unsigned char *tmp = realloc(sec->data, new_cap);
    if (!tmp) {
        fprintf(stderr, "elf: out of memory\n");
        exit(1);
    }
    sec->data = tmp;
    sec->cap = new_cap;
}

static void rela_grow(ElfRela **rela_arr, size_t *count, size_t *cap, size_t need)
{
    while (*count + need > *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        ElfRela *ns = realloc(*rela_arr, nc * sizeof(ElfRela));
        if (!ns) {
            fprintf(stderr, "elf: out of memory\n");
            exit(1);
        }
        *rela_arr = ns;
        *cap = nc;
    }
}

static uint64_t align_up(uint64_t val, uint64_t align)
{
    return (val + align - 1) & ~(align - 1);
}

/* ------------------------------------------------------------------ */
/*  lifecycle                                                          */
/* ------------------------------------------------------------------ */

ElfWriter *elf_create(void)
{
    ElfWriter *elf = calloc(1, sizeof(*elf));
    if (!elf) return NULL;
    elf->text.data   = NULL;
    elf->rodata.data = NULL;
    elf->data.data   = NULL;
    elf->syms        = NULL;
    elf->sym_cap     = 0;
    elf->entry       = ELF_TEXT_BASE;   /* default for EXEC */
    elf->object_mode = 0;
    return elf;
}

void elf_destroy(ElfWriter *elf)
{
    if (!elf) return;
    free(elf->text.data);
    free(elf->rodata.data);
    free(elf->data.data);
    free(elf->syms);
    free(elf->rela_text);
    free(elf->rela_data);
    free(elf);
}

/* ------------------------------------------------------------------ */
/*  section helpers                                                    */
/* ------------------------------------------------------------------ */

void elf_add_text(ElfWriter *elf, const unsigned char *bytes, size_t len)
{
    if (len == 0) return;
    section_grow(&elf->text, len);
    memcpy(elf->text.data + elf->text.size, bytes, len);
    elf->text.size += len;
}

void elf_add_rodata(ElfWriter *elf, const unsigned char *bytes, size_t len)
{
    if (len == 0) return;
    section_grow(&elf->rodata, len);
    memcpy(elf->rodata.data + elf->rodata.size, bytes, len);
    elf->rodata.size += len;
}

void elf_add_data(ElfWriter *elf, const unsigned char *bytes, size_t len,
                  int is_zero_init)
{
    if (is_zero_init) {
        elf->bss_size += len;
    } else {
        if (len == 0) return;
        section_grow(&elf->data, len);
        memcpy(elf->data.data + elf->data.size, bytes, len);
        elf->data.size += len;
    }
}

/* ------------------------------------------------------------------ */
/*  symbol table                                                       */
/* ------------------------------------------------------------------ */

void elf_define_symbol(ElfWriter *elf, const char *name, uint64_t addr)
{
    if (elf->sym_count >= elf->sym_cap) {
        size_t nc = elf->sym_cap ? elf->sym_cap * 2 : 64;
        ElfSym *ns = realloc(elf->syms, nc * sizeof(ElfSym));
        if (!ns) {
            fprintf(stderr, "elf: out of memory\n");
            exit(1);
        }
        elf->syms = ns;
        elf->sym_cap = nc;
    }
    ElfSym *s = &elf->syms[elf->sym_count++];
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->addr = addr;
    s->section = 0;
    s->bind = 1;     /* STB_GLOBAL */
    s->type = 0;     /* STT_NOTYPE */
    s->is_extern = 0;
}

int64_t elf_lookup_symbol(ElfWriter *elf, const char *name)
{
    for (size_t i = 0; i < elf->sym_count; i++) {
        if (strcmp(elf->syms[i].name, name) == 0)
            return (int64_t)elf->syms[i].addr;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  object mode helpers                                                */
/* ------------------------------------------------------------------ */

void elf_set_object_mode(ElfWriter *elf, int mode)
{
    elf->object_mode = mode;
    if (mode) {
        elf->entry = 0;  /* no entry point for object files */
    }
}

void elf_define_symbol_obj(ElfWriter *elf,
                           const char *name,
                           uint16_t section,
                           uint64_t offset,
                           uint8_t bind,
                           uint8_t type)
{
    if (elf->sym_count >= elf->sym_cap) {
        size_t nc = elf->sym_cap ? elf->sym_cap * 2 : 64;
        ElfSym *ns = realloc(elf->syms, nc * sizeof(ElfSym));
        if (!ns) {
            fprintf(stderr, "elf: out of memory\n");
            exit(1);
        }
        elf->syms = ns;
        elf->sym_cap = nc;
    }
    ElfSym *s = &elf->syms[elf->sym_count++];
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->addr = offset;
    s->section = section;
    s->bind = bind;
    s->type = type;
    s->is_extern = 0;
}

size_t elf_extern_symbol(ElfWriter *elf, const char *name)
{
    /* Check if already defined */
    for (size_t i = 0; i < elf->sym_count; i++) {
        if (strcmp(elf->syms[i].name, name) == 0 && elf->syms[i].is_extern)
            return i + 1;  /* +1 because null symbol is index 0 */
    }
    /* Add new external symbol */
    if (elf->sym_count >= elf->sym_cap) {
        size_t nc = elf->sym_cap ? elf->sym_cap * 2 : 64;
        ElfSym *ns = realloc(elf->syms, nc * sizeof(ElfSym));
        if (!ns) {
            fprintf(stderr, "elf: out of memory\n");
            exit(1);
        }
        elf->syms = ns;
        elf->sym_cap = nc;
    }
    ElfSym *s = &elf->syms[elf->sym_count++];
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->addr = 0;
    s->section = 0;
    s->bind = 1;     /* STB_GLOBAL */
    s->type = 0;     /* STT_NOTYPE */
    s->is_extern = 1;
    return elf->sym_count;  /* +1 for null symbol */
}

/* ------------------------------------------------------------------ */
/*  relocation helpers                                                 */
/* ------------------------------------------------------------------ */

void elf_add_rela_text(ElfWriter *elf,
                       uint64_t offset,
                       size_t sym_idx,
                       uint32_t type,
                       int64_t addend)
{
    rela_grow(&elf->rela_text, &elf->rela_text_count, &elf->rela_text_cap, 1);
    ElfRela *r = &elf->rela_text[elf->rela_text_count++];
    r->offset = offset;
    r->info = ((uint64_t)sym_idx << 32) | (uint32_t)type;
    r->addend = addend;
}

void elf_add_rela_data(ElfWriter *elf,
                       uint64_t offset,
                       size_t sym_idx,
                       uint32_t type,
                       int64_t addend)
{
    rela_grow(&elf->rela_data, &elf->rela_data_count, &elf->rela_data_cap, 1);
    ElfRela *r = &elf->rela_data[elf->rela_data_count++];
    r->offset = offset;
    r->info = ((uint64_t)sym_idx << 32) | (uint32_t)type;
    r->addend = addend;
}

/* ------------------------------------------------------------------ */
/*  ELF structs (packed to guarantee layout)                           */
/* ------------------------------------------------------------------ */

#pragma pack(push, 1)

typedef struct {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t      sh_name;
    uint32_t      sh_type;
    uint64_t      sh_flags;
    uint64_t      sh_addr;
    uint64_t      sh_offset;
    uint64_t      sh_size;
    uint32_t      sh_link;
    uint32_t      sh_info;
    uint64_t      sh_addralign;
    uint64_t      sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t      p_type;
    uint32_t      p_flags;
    uint64_t      p_offset;
    uint64_t      p_vaddr;
    uint64_t      p_paddr;
    uint64_t      p_filesz;
    uint64_t      p_memsz;
    uint64_t      p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t      st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t      st_shndx;
    uint64_t      st_value;
    uint64_t      st_size;
} Elf64_Sym;

typedef struct {
    uint64_t      r_offset;
    uint64_t      r_info;
    int64_t       r_addend;
} Elf64_Rela;

#pragma pack(pop)

/* ELF constants */
#define EI_NIDENT   16
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EV_CURRENT  1
#define ET_EXEC     2
#define ET_REL      1
#define EM_X86_64   62
#define PT_LOAD     1
#define PF_R        4
#define PF_W        2
#define PF_X        1
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8
#define SHF_WRITE       0x1
#define SHF_ALLOC       0x2
#define SHF_EXECINSTR   0x4
#define SHN_UNDEF       0
#define SHN_ABS         0xfff1

/* Symbol binding and type */
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define ELF_ST_INFO(bind, type)  (((bind) << 4) | ((type) & 0xf))

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

/* ------------------------------------------------------------------ */
/*  write EXEC (existing code, unchanged logic)                        */
/* ------------------------------------------------------------------ */

static int elf_write_exec(ElfWriter *elf, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("elf_write");
        return -1;
    }

    size_t text_sz   = elf->text.size;
    size_t rodata_sz = elf->rodata.size;
    size_t data_sz   = elf->data.size;
    size_t bss_sz    = elf->bss_size;

    /* ---- compute file offsets ---- */

    /* First PT_LOAD segment: ehdr + phdrs + padding + .text */
    uint64_t seg1_start = 0;
    uint64_t phdr_count_offset = sizeof(Elf64_Ehdr);

    /* Count how many PT_LOAD headers we'll have */
    int nphdr = 0;
    nphdr++; /* always have segment 0 (ehdr + text) */
    if (rodata_sz > 0) nphdr++;
    if (data_sz > 0)   nphdr++;

    uint64_t phdrs_size = nphdr * sizeof(Elf64_Phdr);
    uint64_t seg1_before_text = align_up(sizeof(Elf64_Ehdr) + phdrs_size, 0x1000);

    uint64_t text_vaddr = ELF_TEXT_BASE;

    uint64_t seg1_filesz = seg1_before_text + text_sz;
    uint64_t seg1_memsz  = align_up(seg1_filesz, 0x1000);

    /* File offset after segment 1 */
    uint64_t file_off = seg1_filesz;

    /* Segment 2: .rodata (separate segment at ELF_RODATA_BASE) */
    uint64_t rodata_off = 0;
    uint64_t rodata_vaddr = 0;
    if (rodata_sz > 0) {
        file_off = align_up(file_off, 0x1000);
        rodata_off = file_off;
        rodata_vaddr = ELF_RODATA_BASE;
        file_off += rodata_sz;
    }

    /* Segment 3: .data (+ .bss in memory) */
    uint64_t data_off = 0;
    uint64_t data_vaddr = 0;
    uint64_t seg3_memsz = 0;
    if (data_sz > 0) {
        file_off = align_up(file_off, 0x1000);
        data_off = file_off;
        data_vaddr = ELF_DATA_BASE;
        seg3_memsz = data_sz + bss_sz;
        file_off += data_sz;
    }

    /* Section header string table (.shstrtab) */
    file_off = align_up(file_off, 8);
    uint64_t shstrtab_off = file_off;
    const char *shstrtab_str =
        ".text\0"
        ".rodata\0"
        ".data\0"
        ".bss\0"
        ".symtab\0"
        ".strtab\0"
        ".shstrtab\0";
    size_t shstrtab_sz = strlen(shstrtab_str) + 1;
    file_off += shstrtab_sz;
    file_off = align_up(file_off, 8);

    /* Symbol string table (.strtab) */
    uint64_t strtab_off = file_off;
    size_t strtab_sz = 1;
    for (size_t i = 0; i < elf->sym_count; i++) {
        strtab_sz += strlen(elf->syms[i].name) + 1;
    }
    file_off += strtab_sz;
    file_off = align_up(file_off, 8);

    /* Symbol table (.symtab) */
    uint64_t symtab_off = file_off;
    size_t symtab_sz = (elf->sym_count + 1) * sizeof(Elf64_Sym);
    file_off += symtab_sz;
    file_off = align_up(file_off, 8);

    /* Section headers */
    int nshdr = 8;
    uint64_t shdr_off = file_off;

    /* ---- entry point ---- */
    uint64_t entry = elf->entry;
    if (entry == ELF_TEXT_BASE) {
        entry = text_vaddr;
    }

    /* ---- write ELF header ---- */
    {
        Elf64_Ehdr ehdr;
        memset(&ehdr, 0, sizeof(ehdr));
        ehdr.e_ident[0] = 0x7f;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = ELFCLASS64;
        ehdr.e_ident[5] = ELFDATA2LSB;
        ehdr.e_ident[6] = EV_CURRENT;
        ehdr.e_ident[7] = 0;
        ehdr.e_type      = ET_EXEC;
        ehdr.e_machine   = EM_X86_64;
        ehdr.e_version   = 1;
        ehdr.e_entry     = entry;
        ehdr.e_phoff     = phdr_count_offset;
        ehdr.e_shoff     = shdr_off;
        ehdr.e_flags     = 0;
        ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
        ehdr.e_phentsize = sizeof(Elf64_Phdr);
        ehdr.e_phnum     = (uint16_t)nphdr;
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum     = (uint16_t)nshdr;
        ehdr.e_shstrndx  = 7;
        fwrite(&ehdr, sizeof(ehdr), 1, f);
    }

    /* ---- write program headers ---- */
    {
        Elf64_Phdr ph;
        memset(&ph, 0, sizeof(ph));
        ph.p_type   = PT_LOAD;
        ph.p_flags  = PF_R | PF_X;
        ph.p_offset = seg1_start;
        ph.p_vaddr  = 0x400000;
        ph.p_paddr  = 0x400000;
        ph.p_filesz = seg1_filesz;
        ph.p_memsz  = seg1_memsz;
        ph.p_align  = 0x1000;
        fwrite(&ph, sizeof(ph), 1, f);
    }
    if (rodata_sz > 0) {
        Elf64_Phdr ph;
        memset(&ph, 0, sizeof(ph));
        ph.p_type   = PT_LOAD;
        ph.p_flags  = PF_R;
        ph.p_offset = rodata_off;
        ph.p_vaddr  = rodata_vaddr;
        ph.p_paddr  = rodata_vaddr;
        ph.p_filesz = rodata_sz;
        ph.p_memsz  = rodata_sz;
        ph.p_align  = 0x1000;
        fwrite(&ph, sizeof(ph), 1, f);
    }
    if (data_sz > 0) {
        Elf64_Phdr ph;
        memset(&ph, 0, sizeof(ph));
        ph.p_type   = PT_LOAD;
        ph.p_flags  = PF_R | PF_W;
        ph.p_offset = data_off;
        ph.p_vaddr  = data_vaddr;
        ph.p_paddr  = data_vaddr;
        ph.p_filesz = data_sz;
        ph.p_memsz  = seg3_memsz;
        ph.p_align  = 0x1000;
        fwrite(&ph, sizeof(ph), 1, f);
    }

    /* ---- pad to page boundary (between phdrs and .text) ---- */
    {
        uint64_t cur = sizeof(Elf64_Ehdr) + phdrs_size;
        uint64_t pad = seg1_before_text - cur;
        if (pad > 0) {
            static unsigned char zero_page[4096];
            while (pad > 0) {
                size_t chunk = pad > sizeof(zero_page) ? sizeof(zero_page) : (size_t)pad;
                fwrite(zero_page, chunk, 1, f);
                pad -= chunk;
            }
        }
    }

    /* ---- write .text ---- */
    if (text_sz > 0) {
        fwrite(elf->text.data, text_sz, 1, f);
    }

    /* ---- write .rodata ---- */
    if (rodata_sz > 0) {
        if (rodata_off > seg1_filesz) {
            uint64_t gap = rodata_off - seg1_filesz;
            static unsigned char zero_page[4096];
            while (gap > 0) {
                size_t chunk = gap > sizeof(zero_page) ? sizeof(zero_page) : (size_t)gap;
                fwrite(zero_page, chunk, 1, f);
                gap -= chunk;
            }
        }
        fwrite(elf->rodata.data, rodata_sz, 1, f);
    }

    /* ---- write .data ---- */
    if (data_sz > 0) {
        fwrite(elf->data.data, data_sz, 1, f);
    }

    /* ---- pad to section area ---- */
    {
        uint64_t cur_file_pos;
        if (data_sz > 0) {
            cur_file_pos = data_off + data_sz;
        } else if (rodata_sz > 0) {
            cur_file_pos = rodata_off + rodata_sz;
        } else {
            cur_file_pos = seg1_filesz;
        }
        uint64_t target = shstrtab_off;
        uint64_t pad = target - cur_file_pos;
        if (pad > 0) {
            static unsigned char zero_page[4096];
            while (pad > 0) {
                size_t chunk = pad > sizeof(zero_page) ? sizeof(zero_page) : (size_t)pad;
                fwrite(zero_page, chunk, 1, f);
                pad -= chunk;
            }
        }
    }

    /* ---- write .shstrtab ---- */
    fwrite(shstrtab_str, shstrtab_sz, 1, f);
    {
        uint64_t next = align_up(shstrtab_off + shstrtab_sz, 8);
        uint64_t pad = next - (shstrtab_off + shstrtab_sz);
        if (pad > 0) {
            unsigned char zero[8] = {0};
            fwrite(zero, pad, 1, f);
        }
    }

    /* ---- write .strtab ---- */
    {
        unsigned char *buf = calloc(1, strtab_sz);
        size_t pos = 1;
        for (size_t i = 0; i < elf->sym_count; i++) {
            size_t namelen = strlen(elf->syms[i].name);
            memcpy(buf + pos, elf->syms[i].name, namelen + 1);
            pos += namelen + 1;
        }
        fwrite(buf, strtab_sz, 1, f);
        free(buf);
    }
    {
        uint64_t next = align_up(strtab_off + strtab_sz, 8);
        uint64_t pad = next - (strtab_off + strtab_sz);
        if (pad > 0) {
            unsigned char zero[8] = {0};
            fwrite(zero, pad, 1, f);
        }
    }

    /* ---- write .symtab ---- */
    {
        Elf64_Sym null_sym = {0};
        fwrite(&null_sym, sizeof(null_sym), 1, f);

        for (size_t i = 0; i < elf->sym_count; i++) {
            Elf64_Sym sym;
            memset(&sym, 0, sizeof(sym));
            size_t strtab_pos = 1;
            for (size_t j = 0; j < i; j++) {
                strtab_pos += strlen(elf->syms[j].name) + 1;
            }
            sym.st_name  = (uint32_t)(strtab_off + strtab_pos);
            if (elf->syms[i].addr >= text_vaddr &&
                elf->syms[i].addr < ELF_RODATA_BASE) {
                sym.st_info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
            } else {
                sym.st_info = ELF_ST_INFO(STB_GLOBAL, STT_OBJECT);
            }
            sym.st_other  = 0;
            sym.st_shndx  = SHN_ABS;
            sym.st_value  = elf->syms[i].addr;
            sym.st_size   = 0;
            fwrite(&sym, sizeof(sym), 1, f);
        }
    }
    {
        uint64_t next = align_up(symtab_off + symtab_sz, 8);
        uint64_t pad = next - (symtab_off + symtab_sz);
        if (pad > 0) {
            unsigned char zero[8] = {0};
            fwrite(zero, pad, 1, f);
        }
    }

    /* ---- write section headers ---- */
    {
        Elf64_Shdr shdrs[nshdr];
        memset(shdrs, 0, sizeof(shdrs));

        /* 1: .text */
        shdrs[1].sh_name      = 1;
        shdrs[1].sh_type      = SHT_PROGBITS;
        shdrs[1].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
        shdrs[1].sh_addr      = text_vaddr;
        shdrs[1].sh_offset    = seg1_before_text;
        shdrs[1].sh_size      = text_sz;
        shdrs[1].sh_addralign = 16;

        /* 2: .rodata */
        if (rodata_sz > 0) {
            shdrs[2].sh_name      = 7;
            shdrs[2].sh_type      = SHT_PROGBITS;
            shdrs[2].sh_flags     = SHF_ALLOC;
            shdrs[2].sh_addr      = rodata_vaddr;
            shdrs[2].sh_offset    = rodata_off;
            shdrs[2].sh_size      = rodata_sz;
            shdrs[2].sh_addralign = 8;
        }

        /* 3: .data */
        if (data_sz > 0) {
            shdrs[3].sh_name      = 15;
            shdrs[3].sh_type      = SHT_PROGBITS;
            shdrs[3].sh_flags     = SHF_WRITE | SHF_ALLOC;
            shdrs[3].sh_addr      = data_vaddr;
            shdrs[3].sh_offset    = data_off;
            shdrs[3].sh_size      = data_sz;
            shdrs[3].sh_addralign = 8;
        }

        /* 4: .bss */
        if (bss_sz > 0) {
            shdrs[4].sh_name      = 21;
            shdrs[4].sh_type      = SHT_NOBITS;
            shdrs[4].sh_flags     = SHF_WRITE | SHF_ALLOC;
            shdrs[4].sh_addr      = ELF_BSS_BASE;
            shdrs[4].sh_offset    = 0;
            shdrs[4].sh_size      = bss_sz;
            shdrs[4].sh_addralign = 8;
        }

        /* 5: .symtab */
        shdrs[5].sh_name      = 26;
        shdrs[5].sh_type      = SHT_SYMTAB;
        shdrs[5].sh_offset    = symtab_off;
        shdrs[5].sh_size      = symtab_sz;
        shdrs[5].sh_link      = 6;
        shdrs[5].sh_info      = 1;
        shdrs[5].sh_addralign = 8;
        shdrs[5].sh_entsize   = sizeof(Elf64_Sym);

        /* 6: .strtab */
        shdrs[6].sh_name      = 34;
        shdrs[6].sh_type      = SHT_STRTAB;
        shdrs[6].sh_offset    = strtab_off;
        shdrs[6].sh_size      = strtab_sz;
        shdrs[6].sh_addralign = 1;

        /* 7: .shstrtab */
        shdrs[7].sh_name      = 42;
        shdrs[7].sh_type      = SHT_STRTAB;
        shdrs[7].sh_offset    = shstrtab_off;
        shdrs[7].sh_size      = shstrtab_sz;
        shdrs[7].sh_addralign = 1;

        fwrite(shdrs, sizeof(Elf64_Shdr), nshdr, f);
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  write REL (new object file output)                                 */
/* ------------------------------------------------------------------ */

static int elf_write_rel(ElfWriter *elf, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("elf_write");
        return -1;
    }

    size_t text_sz   = elf->text.size;
    size_t rodata_sz = elf->rodata.size;
    size_t data_sz   = elf->data.size;
    size_t bss_sz    = elf->bss_size;

    /*
     * Section layout for ET_REL:
     *   0: NULL
     *   1: .text
     *   2: .rodata (if present)
     *   3: .data   (if present)
     *   4: .bss    (if present)
     *   5: .rela.text (if present)
     *   6: .rela.data (if present)
     *   7: .symtab
     *   8: .strtab
     *   9: .shstrtab
     *
     * No program headers for ET_REL.
     * All sections have sh_addr = 0.
     */

    /* Build shstrtab string */
    /* We need to know if rodata/data/bss/rela exist to build correct strings */
    int has_rodata = (rodata_sz > 0);
    int has_data   = (data_sz > 0);
    int has_bss    = (bss_sz > 0);
    int has_rela_text = (elf->rela_text_count > 0);
    int has_rela_data = (elf->rela_data_count > 0);

    /*
     * Section numbering:
     *   0: NULL
     *   1: .text
     *   2: .rodata (optional)
     *   3: .data   (optional)
     *   4: .bss    (optional)
     *   5+: .rela.* (optional)
     *   then: .symtab, .strtab, .shstrtab
     */

    /* Count sections */
    int nshdr = 1;  /* NULL */
    nshdr++;        /* .text */
    if (has_rodata) nshdr++;
    if (has_data)   nshdr++;
    if (has_bss)    nshdr++;
    if (has_rela_text) nshdr++;
    if (has_rela_data) nshdr++;
    nshdr++;        /* .symtab */
    nshdr++;        /* .strtab */
    nshdr++;        /* .shstrtab */

    int sh_text = 1;
    int sh_rodata = has_rodata ? (sh_text + 1) : 0;
    int sh_data = has_data ? (sh_text + (has_rodata ? 1 : 0) + 1) : 0;
    int sh_bss = has_bss ? (sh_text + (has_rodata ? 1 : 0) + (has_data ? 1 : 0) + 1) : 0;

    int sh_rela_text = has_rela_text ? (sh_text + (has_rodata ? 1 : 0) + (has_data ? 1 : 0) + (has_bss ? 1 : 0) + 1) : 0;
    int sh_rela_data = has_rela_data ? (sh_rela_text ? sh_rela_text + 1 : sh_text + (has_rodata ? 1 : 0) + (has_data ? 1 : 0) + (has_bss ? 1 : 0) + 1) : 0;

    int sh_symtab = nshdr - 2;
    int sh_strtab = nshdr - 1;
    int sh_shstrtab = nshdr - 0;  /* last */
    /* Fix: sh_shstrtab is the last section, index = nshdr - 1 (0-based from 0) */
    /* Actually: sections are 0..nshdr-1, so sh_strtab = nshdr-1, sh_shstrtab = nshdr-1 */
    /* Let me recalculate: */
    /* .symtab = nshdr - 3, .strtab = nshdr - 2, .shstrtab = nshdr - 1 */
    sh_symtab = nshdr - 3;
    sh_strtab = nshdr - 2;
    sh_shstrtab = nshdr - 1;

    /* Build .shstrtab content */
    /* Section names in shstrtab: offset 0 is always a null byte (for NULL section) */
    /* We'll build it dynamically */
    char *shstrtab_buf = calloc(1, 1024);
    size_t shstrtab_pos = 1;  /* start after the null byte */

    /* Record offsets of each section name in shstrtab */
    int name_text = 0, name_rodata = 0, name_data = 0, name_bss = 0;
    int name_rela_text = 0, name_rela_data = 0;
    int name_symtab = 0, name_strtab = 0, name_shstrtab = 0;

    name_text = (int)shstrtab_pos;
    strcpy(shstrtab_buf + shstrtab_pos, ".text");
    shstrtab_pos += 6;  /* ".text\0" */

    if (has_rodata) {
        name_rodata = (int)shstrtab_pos;
        strcpy(shstrtab_buf + shstrtab_pos, ".rodata");
        shstrtab_pos += 8;
    }
    if (has_data) {
        name_data = (int)shstrtab_pos;
        strcpy(shstrtab_buf + shstrtab_pos, ".data");
        shstrtab_pos += 6;
    }
    if (has_bss) {
        name_bss = (int)shstrtab_pos;
        strcpy(shstrtab_buf + shstrtab_pos, ".bss");
        shstrtab_pos += 5;
    }
    if (has_rela_text) {
        name_rela_text = (int)shstrtab_pos;
        strcpy(shstrtab_buf + shstrtab_pos, ".rela.text");
        shstrtab_pos += 11;
    }
    if (has_rela_data) {
        name_rela_data = (int)shstrtab_pos;
        strcpy(shstrtab_buf + shstrtab_pos, ".rela.data");
        shstrtab_pos += 11;
    }
    name_symtab = (int)shstrtab_pos;
    strcpy(shstrtab_buf + shstrtab_pos, ".symtab");
    shstrtab_pos += 8;

    name_strtab = (int)shstrtab_pos;
    strcpy(shstrtab_buf + shstrtab_pos, ".strtab");
    shstrtab_pos += 8;

    name_shstrtab = (int)shstrtab_pos;
    strcpy(shstrtab_buf + shstrtab_pos, ".shstrtab");
    shstrtab_pos += 10;

    size_t shstrtab_sz = shstrtab_pos + 1;  /* +1 for the initial null byte */

    /* Build .strtab for symbol names */
    size_t strtab_sz = 1;  /* null byte */
    for (size_t i = 0; i < elf->sym_count; i++) {
        strtab_sz += strlen(elf->syms[i].name) + 1;
    }

    /* Compute file offsets */
    uint64_t file_off = sizeof(Elf64_Ehdr);

    /* .text */
    uint64_t text_off = file_off;
    file_off += text_sz;

    /* .rodata */
    uint64_t rodata_off = 0;
    if (has_rodata) {
        file_off = align_up(file_off, 8);
        rodata_off = file_off;
        file_off += rodata_sz;
    }

    /* .data */
    uint64_t data_off = 0;
    if (has_data) {
        file_off = align_up(file_off, 8);
        data_off = file_off;
        file_off += data_sz;
    }

    /* .bss: SHT_NOBITS, no file offset */

    /* .rela.text */
    uint64_t rela_text_off = 0;
    size_t rela_text_sz = 0;
    if (has_rela_text) {
        file_off = align_up(file_off, 8);
        rela_text_off = file_off;
        rela_text_sz = elf->rela_text_count * sizeof(Elf64_Rela);
        file_off += rela_text_sz;
    }

    /* .rela.data */
    uint64_t rela_data_off = 0;
    size_t rela_data_sz = 0;
    if (has_rela_data) {
        file_off = align_up(file_off, 8);
        rela_data_off = file_off;
        rela_data_sz = elf->rela_data_count * sizeof(Elf64_Rela);
        file_off += rela_data_sz;
    }

    /* .symtab */
    uint64_t symtab_off = file_off;
    size_t symtab_sz = (elf->sym_count + 1) * sizeof(Elf64_Sym);
    file_off += symtab_sz;

    /* .strtab */
    uint64_t strtab_off = file_off;
    file_off += strtab_sz;

    /* .shstrtab */
    uint64_t shstrtab_off = file_off;
    file_off += shstrtab_sz;

    /* Section headers */
    uint64_t shdr_off = file_off;

    /* ---- write ELF header ---- */
    {
        Elf64_Ehdr ehdr;
        memset(&ehdr, 0, sizeof(ehdr));
        ehdr.e_ident[0] = 0x7f;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = ELFCLASS64;
        ehdr.e_ident[5] = ELFDATA2LSB;
        ehdr.e_ident[6] = EV_CURRENT;
        ehdr.e_ident[7] = 0;
        ehdr.e_type      = ET_REL;
        ehdr.e_machine   = EM_X86_64;
        ehdr.e_version   = 1;
        ehdr.e_entry     = 0;
        ehdr.e_phoff     = 0;
        ehdr.e_shoff     = shdr_off;
        ehdr.e_flags     = 0;
        ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
        ehdr.e_phentsize = 0;
        ehdr.e_phnum     = 0;
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum     = (uint16_t)nshdr;
        ehdr.e_shstrndx  = (uint16_t)sh_shstrtab;
        fwrite(&ehdr, sizeof(ehdr), 1, f);
    }

    /* ---- write .text ---- */
    if (text_sz > 0) {
        fwrite(elf->text.data, text_sz, 1, f);
    }

    /* ---- write .rodata ---- */
    if (has_rodata) {
        /* Pad to alignment */
        uint64_t cur = text_off + text_sz;
        uint64_t pad = rodata_off - cur;
        if (pad > 0) {
            unsigned char zero[8] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zero) ? sizeof(zero) : (size_t)pad;
                fwrite(zero, chunk, 1, f);
                pad -= chunk;
            }
        }
        fwrite(elf->rodata.data, rodata_sz, 1, f);
    }

    /* ---- write .data ---- */
    if (has_data) {
        uint64_t cur;
        if (has_rodata) {
            cur = rodata_off + rodata_sz;
        } else {
            cur = text_off + text_sz;
        }
        uint64_t pad = data_off - cur;
        if (pad > 0) {
            unsigned char zero[8] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zero) ? sizeof(zero) : (size_t)pad;
                fwrite(zero, chunk, 1, f);
                pad -= chunk;
            }
        }
        fwrite(elf->data.data, data_sz, 1, f);
    }

    /* ---- write .rela.text ---- */
    if (has_rela_text) {
        uint64_t cur;
        if (has_data) {
            cur = data_off + data_sz;
        } else if (has_rodata) {
            cur = rodata_off + rodata_sz;
        } else {
            cur = text_off + text_sz;
        }
        uint64_t pad = rela_text_off - cur;
        if (pad > 0) {
            unsigned char zero[8] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zero) ? sizeof(zero) : (size_t)pad;
                fwrite(zero, chunk, 1, f);
                pad -= chunk;
            }
        }
        for (size_t i = 0; i < elf->rela_text_count; i++) {
            Elf64_Rela rela;
            rela.r_offset = elf->rela_text[i].offset;
            rela.r_info = elf->rela_text[i].info;
            rela.r_addend = elf->rela_text[i].addend;
            fwrite(&rela, sizeof(rela), 1, f);
        }
    }

    /* ---- write .rela.data ---- */
    if (has_rela_data) {
        uint64_t cur = has_rela_text ? (rela_text_off + rela_text_sz) :
                        (has_data ? (data_off + data_sz) :
                        (has_rodata ? (rodata_off + rodata_sz) : (text_off + text_sz)));
        uint64_t pad = rela_data_off - cur;
        if (pad > 0) {
            unsigned char zero[8] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zero) ? sizeof(zero) : (size_t)pad;
                fwrite(zero, chunk, 1, f);
                pad -= chunk;
            }
        }
        for (size_t i = 0; i < elf->rela_data_count; i++) {
            Elf64_Rela rela;
            rela.r_offset = elf->rela_data[i].offset;
            rela.r_info = elf->rela_data[i].info;
            rela.r_addend = elf->rela_data[i].addend;
            fwrite(&rela, sizeof(rela), 1, f);
        }
    }

    /* ---- write .symtab ---- */
    {
        Elf64_Sym null_sym = {0};
        fwrite(&null_sym, sizeof(null_sym), 1, f);

        for (size_t i = 0; i < elf->sym_count; i++) {
            Elf64_Sym sym;
            memset(&sym, 0, sizeof(sym));
            /* Compute strtab offset for this symbol name */
            size_t strtab_pos = 1;
            for (size_t j = 0; j < i; j++) {
                strtab_pos += strlen(elf->syms[j].name) + 1;
            }
            sym.st_name  = (uint32_t)(strtab_pos);  /* offset within .strtab */
            sym.st_info  = ELF_ST_INFO(elf->syms[i].bind, elf->syms[i].type);
            sym.st_other = 0;
            if (elf->syms[i].is_extern) {
                sym.st_shndx = SHN_UNDEF;
                sym.st_value = 0;
            } else {
                sym.st_shndx = elf->syms[i].section;
                sym.st_value = elf->syms[i].addr;
            }
            sym.st_size  = 0;
            fwrite(&sym, sizeof(sym), 1, f);
        }
    }

    /* ---- write .strtab ---- */
    {
        /* First byte is null */
        fwrite("\0", 1, 1, f);
        for (size_t i = 0; i < elf->sym_count; i++) {
            size_t namelen = strlen(elf->syms[i].name);
            fwrite(elf->syms[i].name, namelen + 1, 1, f);  /* include null terminator */
        }
    }

    /* ---- write .shstrtab ---- */
    fwrite(shstrtab_buf, shstrtab_sz, 1, f);
    free(shstrtab_buf);

    /* ---- write section headers ---- */
    {
        Elf64_Shdr *shdrs = calloc(nshdr, sizeof(Elf64_Shdr));

        /* 0: NULL (already zeroed) */

        /* .text */
        shdrs[sh_text].sh_name      = (uint32_t)name_text;
        shdrs[sh_text].sh_type      = SHT_PROGBITS;
        shdrs[sh_text].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
        shdrs[sh_text].sh_addr      = 0;
        shdrs[sh_text].sh_offset    = text_off;
        shdrs[sh_text].sh_size      = text_sz;
        shdrs[sh_text].sh_addralign = 16;
        /* sh_info: first local symbol index (1 = after null symbol) */
        shdrs[sh_text].sh_info      = 1;

        if (has_rodata) {
            shdrs[sh_rodata].sh_name      = (uint32_t)name_rodata;
            shdrs[sh_rodata].sh_type      = SHT_PROGBITS;
            shdrs[sh_rodata].sh_flags     = SHF_ALLOC;
            shdrs[sh_rodata].sh_addr      = 0;
            shdrs[sh_rodata].sh_offset    = rodata_off;
            shdrs[sh_rodata].sh_size      = rodata_sz;
            shdrs[sh_rodata].sh_addralign = 8;
        }

        if (has_data) {
            shdrs[sh_data].sh_name      = (uint32_t)name_data;
            shdrs[sh_data].sh_type      = SHT_PROGBITS;
            shdrs[sh_data].sh_flags     = SHF_WRITE | SHF_ALLOC;
            shdrs[sh_data].sh_addr      = 0;
            shdrs[sh_data].sh_offset    = data_off;
            shdrs[sh_data].sh_size      = data_sz;
            shdrs[sh_data].sh_addralign = 8;
        }

        if (has_bss) {
            shdrs[sh_bss].sh_name      = (uint32_t)name_bss;
            shdrs[sh_bss].sh_type      = SHT_NOBITS;
            shdrs[sh_bss].sh_flags     = SHF_WRITE | SHF_ALLOC;
            shdrs[sh_bss].sh_addr      = 0;
            shdrs[sh_bss].sh_offset    = 0;
            shdrs[sh_bss].sh_size      = bss_sz;
            shdrs[sh_bss].sh_addralign = 8;
        }

        if (has_rela_text) {
            shdrs[sh_rela_text].sh_name      = (uint32_t)name_rela_text;
            shdrs[sh_rela_text].sh_type      = SHT_RELA;
            shdrs[sh_rela_text].sh_flags     = 0;
            shdrs[sh_rela_text].sh_addr      = 0;
            shdrs[sh_rela_text].sh_offset    = rela_text_off;
            shdrs[sh_rela_text].sh_size      = rela_text_sz;
            shdrs[sh_rela_text].sh_link      = sh_symtab;
            shdrs[sh_rela_text].sh_info      = sh_text;  /* applies to .text */
            shdrs[sh_rela_text].sh_addralign = 8;
            shdrs[sh_rela_text].sh_entsize   = sizeof(Elf64_Rela);
        }

        if (has_rela_data) {
            shdrs[sh_rela_data].sh_name      = (uint32_t)name_rela_data;
            shdrs[sh_rela_data].sh_type      = SHT_RELA;
            shdrs[sh_rela_data].sh_flags     = 0;
            shdrs[sh_rela_data].sh_addr      = 0;
            shdrs[sh_rela_data].sh_offset    = rela_data_off;
            shdrs[sh_rela_data].sh_size      = rela_data_sz;
            shdrs[sh_rela_data].sh_link      = sh_symtab;
            shdrs[sh_rela_data].sh_info      = sh_data;  /* applies to .data */
            shdrs[sh_rela_data].sh_addralign = 8;
            shdrs[sh_rela_data].sh_entsize   = sizeof(Elf64_Rela);
        }

        /* .symtab */
        shdrs[sh_symtab].sh_name      = (uint32_t)name_symtab;
        shdrs[sh_symtab].sh_type      = SHT_SYMTAB;
        shdrs[sh_symtab].sh_flags     = 0;
        shdrs[sh_symtab].sh_addr      = 0;
        shdrs[sh_symtab].sh_offset    = symtab_off;
        shdrs[sh_symtab].sh_size      = symtab_sz;
        shdrs[sh_symtab].sh_link      = sh_strtab;
        shdrs[sh_symtab].sh_info      = 1;  /* first non-local symbol */
        shdrs[sh_symtab].sh_addralign = 8;
        shdrs[sh_symtab].sh_entsize   = sizeof(Elf64_Sym);

        /* .strtab */
        shdrs[sh_strtab].sh_name      = (uint32_t)name_strtab;
        shdrs[sh_strtab].sh_type      = SHT_STRTAB;
        shdrs[sh_strtab].sh_flags     = 0;
        shdrs[sh_strtab].sh_addr      = 0;
        shdrs[sh_strtab].sh_offset    = strtab_off;
        shdrs[sh_strtab].sh_size      = strtab_sz;
        shdrs[sh_strtab].sh_addralign = 1;

        /* .shstrtab */
        shdrs[sh_shstrtab].sh_name    = (uint32_t)name_shstrtab;
        shdrs[sh_shstrtab].sh_type    = SHT_STRTAB;
        shdrs[sh_shstrtab].sh_flags   = 0;
        shdrs[sh_shstrtab].sh_addr    = 0;
        shdrs[sh_shstrtab].sh_offset  = shstrtab_off;
        shdrs[sh_shstrtab].sh_size    = shstrtab_sz;
        shdrs[sh_shstrtab].sh_addralign = 1;

        fwrite(shdrs, sizeof(Elf64_Shdr), nshdr, f);
        free(shdrs);
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  public write entry point                                           */
/* ------------------------------------------------------------------ */

int elf_write(ElfWriter *elf, const char *path)
{
    if (elf->object_mode) {
        return elf_write_rel(elf, path);
    } else {
        return elf_write_exec(elf, path);
    }
}
