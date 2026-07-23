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
    
    system("gcc -c test_linker_helper.c -o test_linker_helper.o 2>&1");
    system("gcc -c test_linker_main.c -o test_linker_main.o 2>&1");
    
    Linker *lk = linker_create();
    linker_add_object(lk, "test_linker_helper.o");
    linker_add_object(lk, "test_linker_main.o");
    linker_set_entry(lk, "main");
    
    if (linker_link(lk, "test_linker_output") != 0) {
        printf("FAIL link: %s\n", linker_error(lk));
        linker_destroy(lk); return 1;
    }
    linker_destroy(lk);
    
    /* Also create gcc reference */
    system("gcc test_linker_helper.o test_linker_main.o -o test_linker_ref 2>&1");
    
    /* Disassemble both */
    puts("\n=== OUR OUTPUT ===");
    system("objdump -d test_linker_output 2>&1");
    
    puts("\n=== GCC REFERENCE ===");
    system("objdump -d test_linker_ref 2>&1");
    
    puts("\n=== OUR ELF HEADER ===");
    system("readelf -h test_linker_output 2>&1");
    
    puts("\n=== GCC ELF HEADER ===");
    system("readelf -h test_linker_ref 2>&1");
    
    puts("\n=== OUR SYMTAB ===");
    system("readelf -s test_linker_output 2>&1");
    
    puts("\n=== GCC SYMTAB ===");
    system("readelf -s test_linker_ref 2>&1");
    
    puts("\n=== OUR PROGRAM HEADERS ===");
    system("readelf -l test_linker_output 2>&1");
    
    puts("\n=== GCC PROGRAM HEADERS ===");
    system("readelf -l test_linker_ref 2>&1");
    
    remove("test_linker_helper.c");
    remove("test_linker_main.c");
    remove("test_linker_helper.o");
    remove("test_linker_main.o");
    remove("test_linker_output");
    remove("test_linker_ref");
    
    return 0;
}
