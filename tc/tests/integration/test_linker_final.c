#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "src/linker.h"

static int run_test(const char *name, int our_exit, int ref_exit) {
    if (our_exit == ref_exit) {
        printf("PASS: %s (exit code %d)\n", name, our_exit);
        return 0;
    }
    printf("FAIL: %s (ours=%d, ref=%d)\n", name, our_exit, ref_exit);
    return 1;
}

static int test_basic(void) {
    printf("=== TEST 1: Basic helper+main ===\n");
    
    const char *helper_src = "int helper(int x) { return x * 2 + 1; }\n";
    const char *main_src = "extern int helper(int);\nint main(void) { return helper(21); }\n";
    
    FILE *f = fopen("test_basic_helper.c", "w");
    fwrite(helper_src, 1, strlen(helper_src), f);
    fclose(f);
    f = fopen("test_basic_main.c", "w");
    fwrite(main_src, 1, strlen(main_src), f);
    fclose(f);
    
    if (system("gcc -c test_basic_helper.c -o test_basic_helper.o 2>/dev/null") != 0) {
        printf("FAIL: gcc helper\n"); return 1;
    }
    if (system("gcc -c test_basic_main.c -o test_basic_main.o 2>/dev/null") != 0) {
        printf("FAIL: gcc main\n"); return 1;
    }
    
    Linker *lk = linker_create();
    if (linker_add_object(lk, "test_basic_helper.o") != 0) {
        printf("FAIL add helper: %s\n", linker_error(lk)); linker_destroy(lk); return 1;
    }
    if (linker_add_object(lk, "test_basic_main.o") != 0) {
        printf("FAIL add main: %s\n", linker_error(lk)); linker_destroy(lk); return 1;
    }
    linker_set_entry(lk, "main");
    
    if (linker_link(lk, "test_basic_output") != 0) {
        printf("FAIL link: %s\n", linker_error(lk));
        linker_destroy(lk); return 1;
    }
    linker_destroy(lk);
    
    system("chmod +x test_basic_output");
    int status = system("./test_basic_output");
    int our_exit = WEXITSTATUS(status);
    
    system("gcc test_basic_helper.o test_basic_main.o -o test_basic_ref 2>/dev/null");
    status = system("./test_basic_ref");
    int ref_exit = WEXITSTATUS(status);
    
    int result = run_test("basic", our_exit, ref_exit);
    
    remove("test_basic_helper.c"); remove("test_basic_main.c");
    remove("test_basic_helper.o"); remove("test_basic_main.o");
    remove("test_basic_output"); remove("test_basic_ref");
    
    return result;
}

static int test_hello(void) {
    printf("\n=== TEST 2: hello + libc_minimal + syscall ===\n");
    
    // Compile hello.c
    if (system("gcc -c test_hello_link.c -o test_hello_link.o -fno-stack-protector 2>/dev/null") != 0) {
        printf("FAIL: gcc hello\n"); return 1;
    }
    
    // Compile libc_minimal.c
    if (system("gcc -c runtime/libc_minimal.c -o libc_minimal_test.o -fno-stack-protector -DHAVE_STDARG 2>/dev/null") != 0) {
        printf("FAIL: gcc libc_minimal\n"); return 1;
    }
    
    // Compile syscall.S
    if (system("gcc -c runtime/syscall.S -o syscall_test.o 2>/dev/null") != 0) {
        printf("FAIL: gcc syscall\n"); return 1;
    }
    
    Linker *lk = linker_create();
    if (linker_add_object(lk, "test_hello_link.o") != 0) {
        printf("FAIL add hello: %s\n", linker_error(lk)); linker_destroy(lk); return 1;
    }
    if (linker_add_object(lk, "libc_minimal_test.o") != 0) {
        printf("FAIL add libc: %s\n", linker_error(lk)); linker_destroy(lk); return 1;
    }
    if (linker_add_object(lk, "syscall_test.o") != 0) {
        printf("FAIL add syscall: %s\n", linker_error(lk)); linker_destroy(lk); return 1;
    }
    linker_set_entry(lk, "main");
    
    linker_print_status(lk);
    
    if (linker_link(lk, "test_hello_output") != 0) {
        printf("FAIL link: %s\n", linker_error(lk));
        linker_destroy(lk); return 1;
    }
    linker_destroy(lk);
    
    system("chmod +x test_hello_output");
    
    // Run our output
    printf("\nRunning our output:\n");
    int status = system("./test_hello_output 2>/dev/null");
    int our_exit = WIFEXITED(status) ? WEXITSTATUS(status) : (WTERMSIG(status) + 128);
    printf("Our exit code: %d\n", our_exit);
    
    // GCC reference
    system("gcc test_hello_link.o libc_minimal_test.o syscall_test.o -o test_hello_ref 2>/dev/null");
    printf("Running GCC ref:\n");
    status = system("./test_hello_ref 2>/dev/null");
    int ref_exit = WIFEXITED(status) ? WEXITSTATUS(status) : (WTERMSIG(status) + 128);
    printf("GCC ref exit code: %d\n", ref_exit);
    
    int result = run_test("hello", our_exit, ref_exit);
    
    // Cleanup
    remove("test_hello_link.o");
    remove("libc_minimal_test.o");
    remove("syscall_test.o");
    remove("test_hello_output");
    remove("test_hello_ref");
    
    return result;
}

int main(void) {
    int failed = 0;
    failed += test_basic();
    failed += test_hello();
    printf("\n=== SUMMARY: %d test(s) failed ===\n", failed);
    return failed;
}
