#ifndef TC_STRING_H
#define TC_STRING_H

#define NULL ((void *)0)

long strlen(char *s);
char *strcpy(char *d, char *s);
char *strncpy(char *d, char *s, long n);
char *strcat(char *d, char *s);
int strcmp(char *a, char *b);
int strncmp(char *a, char *b, long n);
char *strchr(char *s, int c);
char *strstr(char *h, char *n);
char *memcpy(char *d, char *s, long n);
char *memset(char *s, int c, long n);
int memcmp(char *a, char *b, long n);

#endif
