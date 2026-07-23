/*
 * syscall.h -- Declarations for the hand-written syscall wrappers.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stddef.h>
#include <stdint.h>

/* Syscall numbers (x86-64 Linux) */
#define SYS_WRITE      1
#define SYS_READ       0
#define SYS_EXIT       60
#define SYS_OPEN       2
#define SYS_CLOSE      3
#define SYS_MMAP       9
#define SYS_MUNMAP     11
#define SYS_EXIT_GROUP 231

/* File descriptors */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/*
 * Perform a Linux x86-64 syscall.
 *
 * nr  : syscall number
 * a0-a5: up to 6 syscall arguments
 *
 * Returns the value in RAX from the syscall.
 */
extern long sys_invoke(long nr, long a0, long a1, long a2, long a3, long a4, long a5);

/* Syscall wrapper functions */
extern long sys_write(int fd, const char *buf, size_t count);
extern void sys_exit(int code);

#endif /* SYSCALL_H */
