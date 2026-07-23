#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/linker.h"

int main(void)
{
    const char *helper_src = "int helper(int x) { return x * 2 + 1; }\n";
    const char *main_src = "extern int helper(int);\nint main(void) { return helper(21); }\n";
    
    FILE *f = fopen("test_linker_helper.c", "w");
    fwrite(helper_src, 1, strlen(helper_src), f);
    fclose(f);
    f = fopen("test_linker_main.c", "w");
    fwrite(main_src, 1, strlen(main_src), f);
    fclose(f);
    
    system("gcc -c test_linker_helper.c -o test_linker_helper.o 2>/dev/null");
    system("gcc -c test_linker_main.c -o test_linker_main.o 2>/dev/null");
    
    /* Check .text bytes of each object */
    puts("=== helper.o .text bytes ===");
    f = fopen("test_linker_helper.o", "rb");
    fseek(f, 64, SEEK_SET); /* .text offset */
    for (int i = 0; i < 21; i++) {
        unsigned char c = fgetc(f);
        printf("%02x ", c);
        if ((i+1) % 16 == 0) printf("\n");
    }
    printf("\n");
    fclose(f);
    
    puts("=== main.o .text bytes ===");
    f = fopen("test_linker_main.o", "rb");
    fseek(f, 64, SEEK_SET); /* .text offset */
    for (int i = 0; i < 20; i++) {
        unsigned char c = fgetc(f);
        printf("%02x ", c);
        if ((i+1) % 16 == 0) printf("\n");
    }
    printf("\n");
    fclose(f);
    
    /* Check .rela.text of main.o */
    puts("=== main.o .rela.text ===");
    f = fopen("test_linker_main.o", "rb");
    fseek(f, 376, SEEK_SET); /* .rela.text offset */
    unsigned char buf[24];
    fread(buf, 1, 24, f);
    fclose(f);
    
    uint64_t r_offset = *(uint64_t*)(buf + 0);
    uint64_t r_info = *(uint64_t*)(buf + 8);
    int64_t r_addend = *(int64_t*)(buf + 16);
    printf("r_offset=0x%lx, r_info=0x%lx, r_addend=%ld\n",
           (unsigned long)r_offset, (unsigned long)r_info, (long)r_addend);
    printf("  sym_idx=%u, rel_type=%u\n",
           (unsigned)(r_info >> 32), (unsigned)(r_info & 0xFFFFFFFF));
    
    Linker *lk = linker_create();
    linker_add_object(lk, "test_linker_helper.o");
    linker_add_object(lk, "test_linker_main.o");
    linker_set_entry(lk, "main");
    
    if (linker_link(lk, "test_linker_output") != 0) {
        printf("FAIL link: %s\n", linker_error(lk));
        linker_destroy(lk); return 1;
    }
    linker_destroy(lk);
    
    /* Dump the output .text section */
    puts("\n=== Our output .text bytes (at file offset 0xb0) ===");
    f = fopen("test_linker_output", "rb");
    fseek(f, 0xb0, SEEK_SET);
    for (int i = 0; i < 41; i++) {
        unsigned char c = fgetc(f);
        printf("%02x ", c);
        if ((i+1) % 16 == 0) printf("\n");
    }
    printf("\n");
    fclose(f);
    
    /* Also dump with objdump */
    puts("\n=== Our output disassembly ===");
    system("objdump -d test_linker_output 2>&1");
    
    /* Compare with gcc */
    system("gcc test_linker_helper.o test_linker_main.o -o test_linker_ref 2>/dev/null");
    puts("\n=== GCC reference disassembly ===");
    system("objdump -d test_linker_ref 2>&1 | grep -A20 'helper\\|main'");
    
    remove("test_linker_helper.c");
    remove("test_linker_main.c");
    remove("test_linker_helper.o");
    remove("test_linker_main.o");
    remove("test_linker_output");
    remove("test_linker_ref");
    
    return 0;
}
