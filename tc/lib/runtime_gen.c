/*
 * runtime_gen.c -- Minimal C runtime for tc-compiled programs.
 * Compile with: gcc -c -O2 -fno-stack-protector -nostdlib runtime_gen.c -o runtime_gen.o
 * Then extract machine code with: objdump -d runtime_gen.o
 */

// putstr: write null-terminated string to stdout (fd 1)
// SysV ABI: RDI = const char *s
// Returns: int in RAX
int putstr(const char *s) {
    long len = 0;
    while (s[len]) len++;
    if (len == 0) return 0;
    long ret;
    __asm__ __volatile__("syscall"
        : "=a"(ret)
        : "a"(1), "D"(1), "S"(s), "d"(len)
        : "rcx", "r11");
    return (int)ret;
}

// exit: terminate process
// SysV ABI: RDI = int code
void exit(int code) {
    __asm__ __volatile__("syscall"
        :
        : "a"(231), "D"(code)
        : "rcx", "r11");
    for (;;);
}

// printf: minimal printf for tc-compiled programs
// SysV ABI: RDI = const char *fmt, RSI = arg1, RDX = arg2, RCX = arg3, R8 = arg4, R9 = arg5
// Returns: int in RAX
int printf(const char *fmt, ...) {
    char buf[256];
    char *p = buf;
    
    // Read args from registers (SysV ABI calling convention)
    long args[6];
    __asm__ volatile("" 
        : "=S"(args[0]), "=d"(args[1]), "=r"(args[2]), "=r"(args[3]), "=r"(args[4]), "=r"(args[5])
        :
        : "memory");
    
    const char *f = fmt;
    int arg_idx = 0;
    
    while (*f) {
        if (*f == '%') {
            f++;
            if (*f == 'd' || *f == 'i') {
                // Print decimal integer
                long val = args[arg_idx++];
                char numbuf[32];
                char *np = numbuf + 30;
                *np = '\0';
                if (val == 0) {
                    *--np = '0';
                } else {
                    long tmp = val;
                    if (tmp < 0) {
                        *--np = '-';
                        tmp = -tmp;
                    }
                    while (tmp > 0) {
                        *--np = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                }
                while (*np) *p++ = *np++;
            } else if (*f == 's') {
                // Print string
                const char *s = (const char *)args[arg_idx++];
                if (s) while (*s) *p++ = *s++;
            } else if (*f == 'c') {
                *p++ = (char)args[arg_idx++];
            } else if (*f == '%') {
                *p++ = '%';
            } else {
                *p++ = '%';
                if (*f) *p++ = *f;
            }
        } else {
            *p++ = *f;
        }
        f++;
    }
    
    int len = p - buf;
    if (len > 0) {
        __asm__ __volatile__("syscall"
            :
            : "a"(1), "D"(1), "S"(buf), "d"(len)
            : "rcx", "r11", "memory");
    }
    return len;
}
