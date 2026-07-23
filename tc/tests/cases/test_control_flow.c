/* test_control_flow.c - Advanced control flow tests for tc compiler
 *
 * Covers: nested if/else, while+break, for+continue, do-while,
 *         ternary, deeply nested blocks (5 levels), break/continue.
 * Returns 0 on success, nonzero on failure.
 */

int main() {
    int result = 0;

    /* 1. Nested if/else with complex conditions */
    {
        int a = 10;
        int b = 20;
        int c = 30;
        if (a < b && b < c) {
            if ((a + b) < c || a == 10) {
                result += 1;
            } else {
                result += 100;
            }
        } else {
            result += 200;
        }
        int x = 5;
        int y = 15;
        if (x > 0 && x < 10 && y > 10 && y < 20) {
            result += 2;
        } else {
            result += 300;
        }
        int z = 7;
        if (z < 3) {
            result += 400;
        } else if (z < 5) {
            result += 500;
        } else if (z < 10) {
            result += 3;
        } else {
            result += 600;
        }
    }
    if (result != 6) return 1;

    /* 2. while loop with early break */
    {
        int sum = 0;
        int i = 0;
        while (i < 100) {
            i++;
            sum += i;
            if (i == 10) break;
        }
        if (sum != 55) return 2;
        result += sum;
    }

    /* 3. for loop with continue */
    {
        int odd_sum = 0;
        int j;
        for (j = 0; j < 20; j++) {
            if (j % 2 == 0) continue;
            odd_sum += j;
        }
        if (odd_sum != 100) return 3;
        result += odd_sum;
    }

    /* 4. do-while loops */
    {
        int count = 0;
        int n = 0;
        do { n++; count++; } while (n < 5);
        if (count != 5) return 4;
        result += count;
        int single = 0;
        do { single = 42; } while (0);
        if (single != 42) return 5;
        result += single;
    }

    /* 5. Ternary operators */
    {
        int a = 10;
        int b = 20;
        int max = (a > b) ? a : b;
        if (max != 20) return 6;
        int val = (a > 5) ? 100 : 0;
        result += val;
        int grade = 85;
        int letter = (grade >= 90) ? 1 : ((grade >= 80) ? 2 : 3);
        if (letter != 2) return 7;
        result += letter;
        int x = -5;
        int abs_x = (x < 0) ? (-x) : x;
        if (abs_x != 5) return 8;
        result += abs_x;
    }

    /* 6. Deeply nested blocks (5 levels) */
    {
        int deep_val = 0;
        {
            int l1 = 10;
            {
                int l2 = 20;
                {
                    int l3 = 30;
                    {
                        int l4 = 40;
                        {
                            int l5 = 50;
                            deep_val = l1 + l2 + l3 + l4 + l5;
                        }
                    }
                }
            }
        }
        if (deep_val != 150) return 9;
        result += deep_val;
    }

    /* 7. for loop with break on condition */
    {
        int found = 0;
        int k;
        for (k = 0; k < 50; k++) {
            if (k * 2 == 42) { found = k; break; }
        }
        if (found != 21) return 10;
        result += found;
    }

    /* 8. Nested loops with inner break */
    {
        int outer_sum = 0;
        int i;
        for (i = 0; i < 5; i++) {
            int inner = 0;
            int j;
            for (j = 0; j < 10; j++) {
                inner += j;
                if (j == 3) break;
            }
            outer_sum += inner;
        }
        if (outer_sum != 30) return 12;
        result += outer_sum;
    }

    /* 9. do-while with break */
    {
        int counter = 0;
        do { counter++; if (counter == 7) break; } while (counter < 100);
        if (counter != 7) return 13;
        result += counter;
    }

    /* Final: result should be 533 */
    if (result != 533) return 14;
    return 0;
}
