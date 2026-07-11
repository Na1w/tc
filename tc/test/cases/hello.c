int printf(const char *format, ...);

int subfunc(int arg1, int arg2) {
    return arg1 + arg2;
}

int main(void) {
    int b = 50;
    b *= 10;
    int res = subfunc(10, b);
    printf("Sum: %d\n", res);
    return 0;
}
