#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TC="$PROJECT_DIR/tc"
CASES_DIR="$SCRIPT_DIR/cases"
EXPECTED_DIR="$SCRIPT_DIR/expected"

PASS=0
FAIL=0
TOTAL=0

for case_file in "$CASES_DIR"/*.c; do
    name="$(basename "$case_file" .c)"
    out_bin="$CASES_DIR/${name}.out"
    expected="$EXPECTED_DIR/${name}.txt"

    TOTAL=$((TOTAL + 1))
    echo -n "Testing $name... "

    # Compile with tc
    if ! "$TC" -o "$out_bin" "$case_file" 2>/dev/null; then
        echo "FAIL (compile error)"
        FAIL=$((FAIL + 1))
        continue
    fi

    chmod +x "$out_bin"

    # Run with timeout, capture output
    output=$(timeout 5 "$out_bin" 2>/dev/null) || true

    # Compare
    if [ -f "$expected" ]; then
        expected_content=$(cat "$expected")
        if [ "$output" = "$expected_content" ]; then
            echo "PASS"
            PASS=$((PASS + 1))
        else
            echo "FAIL (output mismatch)"
            echo "  Expected: $(echo "$expected_content" | head -1)"
            echo "  Got:      $(echo "$output" | head -1)"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "PASS (no expected output)"
        PASS=$((PASS + 1))
    fi

    rm -f "$out_bin"
done

echo ""
echo "Results: $PASS/$TOTAL passed, $FAIL failed"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
