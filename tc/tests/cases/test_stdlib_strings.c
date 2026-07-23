#include <string.h>

int main() {
    // Test strlen
    if (strlen("Hello") != 5) return 1;
    
    // Test strcmp
    if (strcmp("abc", "abc") != 0) return 2;
    if (strcmp("abc", "abd") >= 0) return 3;
    if (strcmp("abd", "abc") <= 0) return 4;
    
    // Test strcpy
    char buf[16];
    strcpy(buf, "test");
    if (strcmp(buf, "test") != 0) return 5;
    
    // Test strcat
    strcpy(buf, "Hello");
    strcat(buf, " World");
    if (strcmp(buf, "Hello World") != 0) return 6;
    
    // Test strchr
    if (strchr("hello", 'l') == NULL) return 7;
    
    // Test strstr
    if (strstr("hello world", "world") == NULL) return 8;
    
    // Test memset
    char arr[4];
    memset(arr, 'A', 4);
    if (arr[0] != 'A' || arr[3] != 'A') return 9;
    
    // Test memcpy
    char dst[4];
    memcpy(dst, "ABC", 4);
    if (strcmp(dst, "ABC") != 0) return 10;
    
    // Test memcmp
    if (memcmp("abc", "abc", 3) != 0) return 11;
    if (memcmp("abc", "abd", 3) >= 0) return 12;
    
    return 0;
}
