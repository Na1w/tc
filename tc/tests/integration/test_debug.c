#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <stdint.h>

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
    uint32_t  p_type;
    uint32_t  p_flags;
    uint64_t  p_offset;
    uint64_t  p_vaddr;
    uint64_t  p_paddr;
    uint64_t  p_filesz;
    uint64_t  p_memsz;
    uint64_t  p_align;
} Elf64_Phdr;
typedef struct {
    uint32_t  sh_name;
    uint32_t  sh_type;
    uint64_t  sh_flags;
    uint64_t  sh_addr;
    uint64_t  sh_offset;
    uint64_t  sh_size;
    uint32_t  sh_link;
    uint32_t  sh_info;
    uint64_t  sh_addralign;
    uint64_t  sh_entsize;
} Elf64_Shdr;
#pragma pack(pop)

#include "src/linker.h"

int main(void) {
    system("gcc -c test_hello_link.c -o test_hello_link.o -fno-stack-protector 2>/dev/null");
    system("gcc -c runtime/libc_minimal.c -o libc_minimal_test.o -fno-stack-protector -DHAVE_STDARG 2>/dev/null");
    system("gcc -c runtime/syscall.S -o syscall_test.o 2>/dev/null");
    
    Linker *lk = linker_create();
    
    int r1 = linker_add_object(lk, "test_hello_link.o");
    printf("add hello.o: %d (error: %s)\n", r1, linker_error(lk));
    
    int r2 = linker_add_object(lk, "libc_minimal_test.o");
    printf("add libc.o: %d (error: %s)\n", r2, linker_error(lk));
    
    int r3 = linker_add_object(lk, "syscall_test.o");
    printf("add syscall.o: %d (error: %s)\n", r3, linker_error(lk));
    
    linker_set_entry(lk, "main");
    
    int r4 = linker_link(lk, "test_hello_output");
    printf("link: %d (error: %s)\n", r4, linker_error(lk));
    
    if (r4 == 0) {
        // Read back and inspect
        FILE *f = fopen("test_hello_output", "rb");
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *data = malloc(sz);
        fread(data, 1, sz, f);
        fclose(f);
        
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
        printf("\nEntry: 0x%lx\n", (unsigned long)ehdr->e_entry);
        
        Elf64_Phdr *ph0 = (Elf64_Phdr *)(data + ehdr->e_phoff);
        Elf64_Phdr *ph1 = (Elf64_Phdr *)(data + ehdr->e_phoff + 56);
        
        printf("PHDR[0] (RX): offset=0x%lx vaddr=0x%lx filesz=%lu memsz=%lu\n",
            (unsigned long)ph0->p_offset, (unsigned long)ph0->p_vaddr,
            (unsigned long)ph0->p_filesz, (unsigned long)ph0->p_memsz);
        printf("PHDR[1] (RW): offset=0x%lx vaddr=0x%lx filesz=%lu memsz=%lu\n",
            (unsigned long)ph1->p_offset, (unsigned long)ph1->p_vaddr,
            (unsigned long)ph1->p_filesz, (unsigned long)ph1->p_memsz);
        
        // Show first 40 bytes of .text (_start stub)
        printf("\n_start stub (first 40 bytes):\n");
        unsigned char *text = data + ph0->p_offset;
        for (int i = 0; i < 40; i++) {
            if (i % 16 == 0) printf("%04x: ", i);
            printf("%02x ", text[i]);
            if (i % 16 == 15) printf("\n");
        }
        printf("\n");
        
        // Show main function (35 bytes from text start)
        printf("\nmain function (35 bytes from text start):\n");
        for (int i = 35; i < 35 + 35; i++) {
            if ((i-35) % 16 == 0) printf("%04x: ", i-35);
            printf("%02x ", text[i]);
            if ((i-35) % 16 == 15) printf("\n");
        }
        printf("\n");
        
        // Show .data
        printf("\n.data (8 bytes):\n");
        unsigned char *dptr = data + ph1->p_offset;
        for (int i = 0; i < 8; i++) printf("%02x ", dptr[i]);
        uint64_t dval;
        memcpy(&dval, dptr, 8);
        printf(" => 0x%lx\n", (unsigned long)dval);
        
        free(data);
        
        system("chmod +x test_hello_output");
        printf("\nRunning our output:\n");
        int status = system("./test_hello_output 2>/dev/null");
        int our_exit = WIFEXITED(status) ? WEXITSTATUS(status) : (WTERMSIG(status) + 128);
        printf("Our exit code: %d\n", our_exit);
        
        // GCC ref
        system("gcc test_hello_link.o libc_minimal_test.o syscall_test.o -o test_hello_ref 2>/dev/null");
        printf("Running GCC ref:\n");
        status = system("./test_hello_ref 2>/dev/null");
        int ref_exit = WIFEXITED(status) ? WEXITSTATUS(status) : (WTERMSIG(status) + 128);
        printf("GCC ref exit code: %d\n", ref_exit);
    }
    
    linker_destroy(lk);
    
    remove("test_hello_link.o");
    remove("libc_minimal_test.o");
    remove("syscall_test.o");
    remove("test_hello_output");
    remove("test_hello_ref");
    
    return 0;
}
