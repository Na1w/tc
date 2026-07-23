#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/linker.h"

int main(void) {
    /* Compile hello.c - uses putstr which is in libc_minimal */
    const char *hello_src = 
        "const char *s = \"Hello, World!\\n\";\n"
        "int main(void) { putstr(s); return 0; }\n";
    
    FILE *f = fopen("test_hello_link.c", "w");
    fwrite(hello_src, 1, strlen(hello_src), f);
    fclose(f);
    
    if (system("gcc -fno-stack-protector -c test_hello_link.c -o test_hello_link.o 2>&1") != 0) {
        fprintf(stderr, "FAIL: gcc compile hello\n");
        return 1;
    }
    
    /* Compile libc_minimal with -fPIC -fno-stack-protector */
    if (system("gcc -std=c99 -fPIC -fno-stack-protector -c runtime/libc_minimal.c -o libc_minimal_test.o 2>&1") != 0) {
        fprintf(stderr, "FAIL: gcc compile libc_minimal\n");
        return 1;
    }
    
    /* Compile syscall.S */
    if (system("gcc -c runtime/syscall.S -o syscall_test.o 2>&1") != 0) {
        fprintf(stderr, "FAIL: gcc compile syscall\n");
        return 1;
    }
    
    /* Use our linker */
    Linker *lk = linker_create();
    linker_add_object(lk, "test_hello_link.o");
    linker_add_object(lk, "libc_minimal_test.o");
    linker_add_object(lk, "syscall_test.o");
    linker_set_entry(lk, "main");
    
    printf("Linking hello + libc_minimal + syscall...\n");
    
    if (linker_link(lk, "test_hello_out") != 0) {
        fprintf(stderr, "FAIL: linker_link: %s\n", linker_error(lk));
        linker_destroy(lk);
        return 1;
    }
    
    linker_destroy(lk);
    
    system("chmod +x test_hello_out");
    
    /* Run and capture output */
    int status = system("./test_hello_out > test_hello_stdout.txt 2>&1");
    int exit_code = status >> 8;
    printf("Exit code: %d (expected 0)\n", exit_code);
    
    FILE *out = fopen("test_hello_stdout.txt", "r");
    char buf[256] = {0};
    if (out) { fread(buf, 1, 255, out); fclose(out); }
    printf("Output: '%s'\n", buf);
    
    /* Compare with gcc/ld */
    system("gcc test_hello_link.o libc_minimal_test.o syscall_test.o -o test_hello_gcc 2>&1");
    int ref_status = system("./test_hello_gcc > test_hello_gcc.txt 2>&1");
    printf("GCC ref exit: %d\n", ref_status >> 8);
    
    if (exit_code == 0 && strstr(buf, "Hello") != NULL) {
        printf("PASS: hello + libc_minimal linked successfully!\n");
    } else {
        printf("FAIL: unexpected output\n");
    }
    
    /* Cleanup */
    remove("test_hello_link.c");
    remove("test_hello_link.o");
    remove("libc_minimal_test.o");
    remove("syscall_test.o");
    remove("test_hello_out");
    remove("test_hello_stdout.txt");
    remove("test_hello_gcc");
    remove("test_hello_gcc.txt");
    
    return 0;
}
