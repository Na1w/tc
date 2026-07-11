#!/bin/bash
# tc test runner - Tier 1 test suite
# Compiles each .c file with tc, runs with timeout, and compares exit code
# against expected output files in test/expected/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TC="$PROJECT_DIR/tc"
CASES_DIR="$SCRIPT_DIR/cases"
EXPECTED_DIR="$SCRIPT_DIR/expected"

# Optional: disasm verification
DISASM_MODE=false
for arg in "$@"; do
    if [ "$arg" = "-d" ] || [ "$arg" = "--disasm" ]; then
        DISASM_MODE=true
    fi
done

PASS=0
FAIL=0
CRASH=0
TOTAL=0

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

for case_file in "$CASES_DIR"/*.c; do
    name="$(basename "$case_file" .c)"
    out_bin="/tmp/tc_test_${name}_$$"
    expected="$EXPECTED_DIR/${name}.txt"

    TOTAL=$((TOTAL + 1))
    echo -n "Testing ${name}... "

    # Compile with tc
    compile_out=$("$TC" "$case_file" -o "$out_bin" 2>&1) || {
        echo -e "${RED}FAIL${NC} (compile error)"
        echo "  $compile_out"
        FAIL=$((FAIL + 1))
        continue
    }

    # Optional disasm verification
    if [ "$DISASM_MODE" = true ]; then
        disasm_out=$("$TC" -d disasm "$case_file" 2>&1) || {
            echo -e "${RED}FAIL${NC} (disasm verification failed)"
            echo "  $disasm_out"
            FAIL=$((FAIL + 1))
            rm -f "$out_bin"
            continue
        }
    fi

    chmod +x "$out_bin"

    # Run with 5-second timeout, capture exit code
    set +e
    timeout 5 "$out_bin" > /dev/null 2>&1
    exit_code=$?
    set -e

    # Check for timeout (exit 124 from timeout command)
    if [ "$exit_code" -eq 124 ]; then
        echo -e "${YELLOW}CRASH${NC} (timeout after 5s - possible infinite loop)"
        CRASH=$((CRASH + 1))
        rm -f "$out_bin"
        continue
    fi

    # Check for other crashes (SIGSEGV=139, etc.)
    if [ "$exit_code" -ge 128 ]; then
        echo -e "${YELLOW}CRASH${NC} (signal $((exit_code - 128)))"
        CRASH=$((CRASH + 1))
        rm -f "$out_bin"
        continue
    fi

    # Compare exit code against expected
    if [ -f "$expected" ]; then
        expected_val=$(cat "$expected" | tr -d '[:space:]')
        if [ "$exit_code" = "$expected_val" ]; then
            echo -e "${GREEN}PASS${NC} (exit=$exit_code)"
            PASS=$((PASS + 1))
        else
            echo -e "${RED}FAIL${NC} (exit code mismatch)"
            echo "  Expected: $expected_val"
            echo "  Got:      $exit_code"
            FAIL=$((FAIL + 1))
        fi
    else
        echo -e "${GREEN}PASS${NC} (exit=$exit_code, no expected file)"
        PASS=$((PASS + 1))
    fi

    rm -f "$out_bin"
done

echo ""
echo "========================================"
echo "Results: $PASS passed, $FAIL failed, $CRASH crashed (total: $TOTAL)"
echo "========================================"

if [ "$FAIL" -gt 0 ] || [ "$CRASH" -gt 0 ]; then
    exit 1
fi
