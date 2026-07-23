/* test_stack_scope.c — Stack management & variable scope tests
 *
 * Tests:
 *  1. Local variable declarations in nested blocks
 *  2. Shadowing of outer scope variables
 *  3. Function parameter handling (multiple params)
 *  4. Stack frame allocation for local arrays (via string pointer)
 *  5. Address-of operator with locals
 *  6. Array indexing with variable indices (via string literal)
 *
 * Returns 0 on success, non-zero on failure.
 */

int helper(int x, int y, int z) {
    /* Test 3: multiple function parameters */
    return x + y * z;
}

int main() {
    int result = 0;

    /* --- Test 3: Function parameter handling --- */
    result += helper(2, 3, 4);        /* 2 + 3*4 = 14 */
    if (result != 14) return 1;

    /* --- Test 1: Local variables in nested blocks --- */
    {
        int a = 10;
        int b = 20;
        result += a + b;              /* result = 14 + 30 = 44 */
        {
            int c = 5;
            int d = 7;
            result += c + d;          /* result = 44 + 12 = 56 */
        }
    }
    if (result != 56) return 2;

    /* --- Test 2: Shadowing of outer scope variables --- */
    int shadow_val = 100;
    {
        int shadow_val = 200;         /* shadows outer shadow_val */
        result += shadow_val;         /* uses inner: result = 56 + 200 = 256 */
    }
    result += shadow_val;             /* uses outer: result = 256 + 100 = 356 */
    if (result != 356) return 3;

    /* --- Test 4: Stack frame allocation for local arrays ---
     * Test that locals are properly allocated on the stack by
     * using multiple local variables in a tight block. */
    {
        int v0 = 1;
        int v1 = 2;
        int v2 = 3;
        int v3 = 4;
        int v4 = 5;
        int v5 = 6;
        int v6 = 7;
        int v7 = 8;
        int block_sum = v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7;
        result += block_sum;          /* result = 356 + 36 = 392 */
    }
    if (result != 392) return 4;

    /* --- Test 6: Array indexing with variable indices ---
     * Use string literal pointer which tc supports for indexing. */
    char *str = "Hello";
    result += str[0];                /* 'H' = 72, result = 392 + 72 = 464 */
    result += str[4];                /* 'o' = 111, result = 464 + 111 = 575 */
    int idx = 1;
    result += str[idx];              /* 'e' = 101, result = 575 + 101 = 676 */
    if (result != 676) return 5;

    /* --- Test 5: Address-of operator with locals --- */
    int target = 999;
    int *ptr = &target;
    *ptr = 888;                      /* modify through pointer */
    result += target;                /* target should be 888 */
    /* result = 676 + 888 = 1564 */

    /* Verify pointer dereference with address-of */
    int x = 45;
    int *px = &x;
    result += *px;                   /* result = 1564 + 45 = 1609 */
    *px = 100;
    result += x;                     /* x is now 100, result = 1609 + 100 = 1709 */

    /* --- Final combined check --- */
    {
        int final_check = 1709;
        if (result != final_check) return 6;
    }

    return 0;
}
