#include <stdio.h>

int main() {
    // Test basic puts
    puts("test line");
    
    // Test printf with %s
    printf("%s\n", "string test");
    
    // Test printf with %d
    printf("%d\n", 42);
    printf("%d\n", -100);
    
    // Test printf with %x
    printf("%x\n", 255);
    
    // Test printf with %c
    printf("%c\n", 'A');
    
    // Test printf with %p
    printf("%p\n", (void *)0);
    
    // Test printf with %%
    printf("%% done\n");
    
    // Test combined
    printf("Count: %d, Name: %s\n", 10, "test");
    
    return 0;
}
