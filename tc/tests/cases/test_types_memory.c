/* test_types_memory.c - Type system and memory operation tests for tc compiler
 *
 * Tests: integer arithmetic, pointer dereference, array indexing,
 *        char/pointer operations, memory through pointers.
 * Returns 0 on success, nonzero on failure.
 */

int main() {
    int result = 0;

    /* 1. Basic integer type operations */
    {
        int a = 42;
        int b = -42;
        int c = a + b;
        if (c != 0) return 1;
        result += 1;
    }

    /* 2. Pointer dereference */
    {
        int x = 10;
        int *px = &x;
        *px = 20;
        if (x != 20) return 2;
        result += x;
    }

    /* 3. Array indexing */
    {
        int arr[4] = {10, 20, 30, 40};
        int sum = arr[0] + arr[1] + arr[2] + arr[3];
        if (sum != 100) return 3;
        result += sum / 10;
    }

    /* 4. Pointer arithmetic */
    {
        int data[3] = {5, 15, 25};
        int *p = data;
        int v0 = *p;
        p = p + 1;
        int v1 = *p;
        p = p + 1;
        int v2 = *p;
        if (v0 != 5 || v1 != 15 || v2 != 25) return 4;
        result += v0 + v1 + v2;
    }

    /* 5. Char/string access */
    {
        char *s = "test";
        char c0 = s[0];
        char c1 = s[1];
        if (c0 != 't') return 5;
        if (c1 != 'e') return 6;
        result += (int)c0;
    }

    /* 6. Multiple pointer levels */
    {
        int val = 7;
        int *p1 = &val;
        *p1 = 14;
        int *p2 = p1;
        *p2 = 28;
        if (val != 28) return 7;
        result += val;
    }

    /* Final: result should be 1 + 20 + 10 + 45 + 116 + 28 = 220 */
    if (result != 220) return 8;
    return 0;
}
