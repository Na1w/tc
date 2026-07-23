/* test_expressions.c - Comprehensive expression and operator coverage test */

int helper(int a, int b, int c) {
    return a + b * c;
}

int main() {
    int result = 0;

    /* === BINARY OPERATORS: +, -, *, / === */
    int a = 100;
    int b = 30;
    int sum = a + b;          /* 130 */
    int diff = a - b;         /* 70 */
    int prod = a * b;         /* 3000 */
    int quot = prod / b;      /* 100 */
    result = sum + diff + quot; /* 130 + 70 + 100 = 300 */

    /* === COMPOUND ASSIGNMENT: +=, -=, *= === */
    int x = 10;
    x += 5;   /* 15 */
    x -= 3;   /* 12 */
    x *= 4;   /* 48 */
    result += x; /* 300 + 48 = 348 */

    /* === UNARY OPERATORS: negation, logical NOT, bitwise NOT === */
    int neg = -42;
    int logical_not = !0;       /* 1 */
    int logical_not2 = !1;      /* 0 */
    int bitwise_not = ~0;       /* -1 */
    int bitwise_not2 = ~7;      /* -8 */
    result += (bitwise_not + 1);     /* 349 + 0 = 349 */
    result += (bitwise_not2 + 8);    /* 349 + 0 = 349 */
    result += logical_not;      /* 348 + 1 = 349 */
    result -= logical_not2;     /* 349 - 0 = 349 */
    result += (neg + 42);       /* 349 + 0 = 349 */

    /* === COMPARISON OPERATORS: ==, !=, <, >, <=, >= === */
    int eq = (5 == 5);          /* 1 */
    int neq = (5 != 3);         /* 1 */
    int lt = (3 < 5);           /* 1 */
    int gt = (5 > 3);           /* 1 */
    int leq = (5 <= 5);         /* 1 */
    int geq = (5 >= 3);         /* 1 */
    result += eq + neq + lt + gt + leq + geq; /* 349 + 6 = 355 */

    /* === LOGICAL AND/OR: &&, || === */
    int and_true = (1 && 1);        /* 1 */
    int and_false = (1 && 0);       /* 0 */
    int or_true = (0 || 1);         /* 1 */
    int or_false = (0 || 0);        /* 0 */
    result += and_true + or_true;   /* 355 + 2 = 357 */
    result -= and_false;            /* 357 - 0 = 357 */
    result -= or_false;             /* 357 - 0 = 357 */

    /* === TERNARY NESTING === */
    int t1 = (1 > 0) ? 10 : 20;            /* 10 */
    int t2 = (0 > 1) ? 30 : 40;            /* 40 */
    int t3 = (1) ? ((0) ? 1 : 2) : 3;      /* 2 (nested) */
    result += t1 + t2 + t3;                /* 357 + 52 = 409 */

    /* === TYPE CASTS === */
    int cast_int = (int)42;
    int cast_from_long = (int)100L;
    int cast_neg = (int)(-5);
    int cast_result = cast_int + cast_from_long + cast_neg;
    result += cast_result;

    /* === FUNCTION CALLS WITH MULTIPLE ARGUMENTS === */
    int f1 = helper(10, 20, 30);       /* 10 + 20*30 = 610 */
    int f2 = helper(1, 2, 3);          /* 1 + 2*3 = 7 */
    result += f1 % 100;                /* 451 + 10 = 461 */
    result += f2;                      /* 461 + 7 = 468 */

    /* === COMBINED COMPLEX EXPRESSION === */
    int combined = (a + b) * (quot / 10) - (x / 12);
    /* (100+30) * (100/10) - (48/12) = 130*10 - 4 = 1296 */
    result += combined / 100;          /* 468 + 12 = 480 */

    /* === FINAL VERIFICATION EXPRESSION === */
    int verified = ((result == 480) && (a == 100)) ? 1 : 0;
    result -= verified;                /* 480 - 1 = 479 */

    return result; /* Expected: 479 */
}
