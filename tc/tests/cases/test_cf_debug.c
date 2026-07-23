/* test_cf_debug.c - Debug-level control flow tests for tc compiler
 *
 * Tests basic control flow constructs that the tc compiler supports.
 * Covers: if/else branches, while loops with break, for loops with
 * continue, do-while, and simple ternary expressions.
 * Returns 0 on success, nonzero on failure.
 */

int main() {
    int result = 0;

    /* 1. Simple if/else */
    {
        int a = 10;
        int b = 20;
        if (a < b) {
            result = 1;
        } else {
            result = 100;
        }
    }
    if (result != 1) return 1;

    /* 2. while loop with early break */
    {
        int sum = 0;
        int i = 0;
        while (i < 10) {
            i++;
            sum += i;
            if (i == 5) break;
        }
        if (sum != 15) return 2;
        result += sum;
    }

    /* 3. for loop with continue */
    {
        int count = 0;
        int j;
        for (j = 0; j < 10; j++) {
            if (j % 2 == 0) continue;
            count++;
        }
        if (count != 5) return 3;
        result += count;
    }

    /* 4. do-while */
    {
        int n = 0;
        int iterations = 0;
        do {
            n++;
            iterations++;
        } while (n < 3);
        if (iterations != 3) return 4;
        result += iterations;
    }

    /* 5. Ternary expression */
    {
        int a = 10;
        int b = 20;
        int max = (a > b) ? a : b;
        if (max != 20) return 5;
        result += max;
    }

    /* Final: result should be 1 + 15 + 5 + 3 + 20 = 44 */
    if (result != 44) return 6;
    return 0;
}
