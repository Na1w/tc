/*
 * elf.c -- ELF64 executable writer for the tc C compiler
 *
 * Emits a valid ELF64 ET_EXEC (position-dependent executable)
 * targeting Linux x86-64 System V ABI.
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
    elf->entry       = ELF_TEXT_BASE;   /* default */
    return elf;
}

void elf_destroy(ElfWriter *elf)
{
    if (!elf) return;
    free(elf->text.data);
    free(elf->rodata.data);
    free(elf->data.data);
    free(elf->syms);
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

#pragma pack(pop)

/* ELF constants */
#define EI_NIDENT   16
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EV_CURRENT  1
#define ET_EXEC     2
#define EM_X86_64   62
#define PT_LOAD     1
#define PF_R        4
#define PF_W        2
#define PF_X        1
#define SHT_NULL    0
#define SHT_PROGBITS 1
#define SHT_SYMTAB  2
#define SHT_STRTAB  3
#define SHT_NOBITS  8
#define SHF_WRITE   0x1
#define SHF_ALLOC   0x2
#define SHF_EXECINSTR 0x4
#define SHN_UNDEF   0
#define SHN_ABS     0xfff1

/* ------------------------------------------------------------------ */
/*  write                                                              */
/* ------------------------------------------------------------------ */
/*
 * File layout:
 *
 *   offset 0:
 *     ELF header (64 bytes)
 *     Program headers (n * 56 bytes)
 *     <padding to page boundary>
 *     .text data
 *   <padding to page boundary>
 *     .rodata data
 *   <padding to page boundary>
 *     .data data
 *   <padding to 8-byte boundary>
 *     .shstrtab
 *     .strtab
 *     .symtab
 *     Section headers
 *
 * Program headers (PT_LOAD):
 *   PH[0]: offset=0, vaddr=0x400000, covers ehdr+phdrs+.text  (R+X)
 *   PH[1]: offset=<rodata>, vaddr=0x402000, covers .rodata     (R)
 *   PH[2]: offset=<data>,   vaddr=0x600000, covers .data+.bss  (R+W)
 *
 * The first PT_LOAD starts at file offset 0 with vaddr 0x400000,
 * so the ELF header and program headers are mapped into memory too.
 * .text begins at vaddr 0x400000 + text_file_offset_within_segment.
 */

int elf_write(ElfWriter *elf, const char *path)
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

    /* .text virtual address: 0x400000 + offset of text within segment */
    uint64_t text_vaddr = 0x400000 + seg1_before_text;

    uint64_t seg1_filesz = seg1_before_text + text_sz;
    uint64_t seg1_memsz  = align_up(seg1_filesz, 0x1000);

    /* File offset after segment 1 */
    uint64_t file_off = seg1_filesz;

    /* Segment 2: .rodata */
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
    /*
     * Sections:
     *   0: NULL
     *   1: .text
     *   2: .rodata
     *   3: .data
     *   4: .bss
     *   5: .symtab
     *   6: .strtab
     *   7: .shstrtab
     */
    int nshdr = 8;
    uint64_t shdr_off = file_off;

    /* ---- entry point ---- */
    uint64_t entry = elf->entry;
    /* If entry was set to ELF_TEXT_BASE (0x401000), recalculate to actual
     * text vaddr.  For the new layout text starts at text_vaddr. */
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
        /* PH[0]: ehdr + phdrs + .text (R+X) */
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

        /* PH[1]: .rodata (R) */
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

        /* PH[2]: .data + .bss (R+W) */
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

    /* pad segment 1 to memsz */
    {
        uint64_t cur = seg1_filesz;
        uint64_t pad = seg1_memsz - cur;
        /* We don't actually write this padding to file; it's memory-only.
         * But we need to advance file offset for next segment. */
        (void)pad;
    }

    /* ---- write .rodata ---- */
    if (rodata_sz > 0) {
        /* Ensure we're at the right file offset */
        if (rodata_off > seg1_filesz) {
            /* pad gap */
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
        /* Ensure we're at the right file offset */
        uint64_t expected_data_off;
        if (rodata_sz > 0) {
            expected_data_off = align_up(rodata_off + rodata_sz, 0x1000);
        } else {
            expected_data_off = align_up(seg1_filesz, 0x1000);
        }
        /* data_off should already match expected_data_off */
        (void)expected_data_off;
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
                sym.st_info = 0x12; /* STB_GLOBAL | STT_FUNC */
            } else {
                sym.st_info = 0x10; /* STB_GLOBAL | STT_OBJECT */
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
