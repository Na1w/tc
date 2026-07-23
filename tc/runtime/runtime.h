#ifndef TC_RUNTIME_H
#define TC_RUNTIME_H

typedef unsigned long size_t;
#define NULL ((void *)0)

/* Syscall module */
long sys_write(int fd, const char *buf, size_t count);
void sys_exit(int code);

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_EXIT_GROUP 231
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* stdio module */
int printf(const char *fmt, ...);
int puts(const char *str);
void putstr(const char *s);

/* string module */
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

/* stdlib module */
int abs(int x);
int atoi(const char *str);
char *itoa(int value, char *str, int radix);
void *malloc(size_t n);
void free(void *p);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t n);

/* crt0 module */
extern void (*_start)(int argc, char **argv);

#endif
