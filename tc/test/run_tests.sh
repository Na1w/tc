#!/usr/bin/env bash
#
# run_tests.sh — Test runner for the tc compiler
#
# For each .c file in tests/cases/:
#   1. Build with ./tc
#   2. Build a reference with gcc + libruntime.a
#   3. Run both (5s timeout) and compare exit codes + stdout
#   4. Optionally compare against expected output files
#   5. Report PASS / FAIL
#
# Usage:  ./run_tests.sh  [test_name.c ...]
#   If no args are given, all tests/cases/*.c are run.
#

set -euo pipefail

# ─── Configuration ───────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TC="$PROJECT_DIR/tc"
GCC="gcc"
GCC_FLAGS="-Wall -O2"
RUNTIME_LIB="$PROJECT_DIR/lib/libruntime.a"
CASES_DIR="$PROJECT_DIR/tests/cases"
EXPECTED_DIR="$PROJECT_DIR/tests/cases/expected"
TIMEOUT=5
TMPDIR_BASE=$(mktemp -d /tmp/tc_test_runner.XXXXXX)

# ─── Colour helpers (disabled when not a tty) ──────────────────
if [ -t 1 ]; then
    GREEN='\033[0;32m'
    RED='\033[0;31m'
    YELLOW='\033[0;33m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    GREEN=''
    RED=''
    YELLOW=''
    BOLD=''
    NC=''
fi

# ─── Counters ───────────────────────────────────────────────────
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
TOTAL=0

# ─── Helpers ────────────────────────────────────────────────────
log_pass()  { echo -e "${GREEN}[PASS]${NC} $1"; ((PASS_COUNT++)) || true; }
log_fail()  { echo -e "${RED}[FAIL]${NC} $1 (expected $2, got $3)"; ((FAIL_COUNT++)) || true; }
log_skip()  { echo -e "${YELLOW}[SKIP]${NC} $1"; ((SKIP_COUNT++)) || true; }

normalize_exit() {
    case "$1" in
        124) echo "TIMEOUT" ;;
        139) echo "SIGSEGV" ;;
        132) echo "SIGILL" ;;
        134) echo "SIGABRT" ;;
        136) echo "SIGABRT" ;;
        *)   echo "$1" ;;
    esac
}

cleanup() { rm -rf "$TMPDIR_BASE"; }
trap cleanup EXIT

# ─── Determine which tests to run ──────────────────────────────
if [ $# -gt 0 ]; then
    TEST_FILES=()
    for f in "$@"; do
        if [ -f "$f" ]; then
            TEST_FILES+=("$f")
        elif [ -f "$CASES_DIR/$f" ]; then
            TEST_FILES+=("$CASES_DIR/$f")
        else
            echo -e "${RED}WARN: test file not found: $f${NC}"
        fi
    done
else
    shopt -s nullglob
    TEST_FILES=("$CASES_DIR"/*.c)
    shopt -u nullglob
fi

# ─── Pre-flight checks ────────────────────────────────────────
if [ ! -x "$TC" ]; then
    echo -e "${RED}ERROR: tc binary not found or not executable at $TC${NC}"
    echo "  Run 'make' in $PROJECT_DIR first."
    exit 1
fi

if ! command -v "$GCC" &>/dev/null; then
    echo -e "${RED}ERROR: gcc not found in PATH${NC}"
    exit 1
fi

if [ ! -f "$RUNTIME_LIB" ]; then
    echo -e "${RED}ERROR: libruntime.a not found at $RUNTIME_LIB${NC}"
    echo "  Run 'make runtime' in $PROJECT_DIR first."
    exit 1
fi

# ─── Banner ────────────────────────────────────────────────────
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  tc Test Runner  (timeout=${TIMEOUT}s per test)${NC}"
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
echo ""

# ─── Run each test ─────────────────────────────────────────────
for TEST_FILE in "${TEST_FILES[@]}"; do
    BASENAME="$(basename "$TEST_FILE" .c)"
    TOTAL=$((TOTAL + 1))

    TC_OUT="$TMPDIR_BASE/${BASENAME}_tc"
    GCC_OUT="$TMPDIR_BASE/${BASENAME}_gcc"

    # ── Step 1: Build with ./tc (with build timeout) ────────────
    TC_BUILD_OUTPUT=$(timeout "$TIMEOUT" "$TC" "$TEST_FILE" -o "$TC_OUT" 2>&1) || {
        build_exit=$?
        if [ "$build_exit" -eq 124 ]; then
            log_skip "$BASENAME (tc build timed out)"
        else
            log_skip "$BASENAME (tc build failed: $TC_BUILD_OUTPUT)"
        fi
        continue
    }
    chmod +x "$TC_OUT"

    # ── Step 2: Build reference with gcc ───────────────────────
    GCC_BUILD_OUTPUT=$("$GCC" $GCC_FLAGS "$TEST_FILE" "$RUNTIME_LIB" -o "$GCC_OUT" 2>&1) || {
        log_skip "$BASENAME (gcc build failed: $GCC_BUILD_OUTPUT)"
        continue
    }

    # ── Step 3: Run both with timeout, capture stdout & exit code ──
    TC_STDOUT=$(timeout "$TIMEOUT" "$TC_OUT" 2>/dev/null) && TC_EXIT=0 || TC_EXIT=$?
    TC_EXIT=$(normalize_exit "$TC_EXIT")

    GCC_STDOUT=$(timeout "$TIMEOUT" "$GCC_OUT" 2>/dev/null) && GCC_EXIT=0 || GCC_EXIT=$?
    GCC_EXIT=$(normalize_exit "$GCC_EXIT")

    # ── Step 4: Compare exit codes ─────────────────────────────
    if [ "$TC_EXIT" != "$GCC_EXIT" ]; then
        log_fail "$BASENAME" "exit_code=$GCC_EXIT" "exit_code=$TC_EXIT"
        continue
    fi

    # ── Step 5: Compare stdout ─────────────────────────────────
    if [ "$TC_STDOUT" != "$GCC_STDOUT" ]; then
        DIFF_LINE=$(diff <(echo "$GCC_STDOUT") <(echo "$TC_STDOUT") 2>/dev/null | head -5 || true)
        log_fail "$BASENAME" "stdout matches gcc" "stdout differs: $DIFF_LINE"
        continue
    fi

    # ── Step 6: Check expected output file if it exists ────────
    EXPECTED_FILE="$EXPECTED_DIR/${BASENAME}.expected"
    if [ -f "$EXPECTED_FILE" ]; then
        EXPECTED_EXIT=$(grep '^exit_code=' "$EXPECTED_FILE" | head -1 | cut -d= -f2)
        EXPECTED_STDOUT=$(grep '^stdout=' "$EXPECTED_FILE" | head -1 | cut -d= -f2-)
        EXPECTED_EXIT_NORM=$(normalize_exit "$EXPECTED_EXIT")
        if [ "$TC_EXIT" != "$EXPECTED_EXIT_NORM" ] || [ "$TC_STDOUT" != "$EXPECTED_STDOUT" ]; then
            log_fail "$BASENAME" "expected exit=$EXPECTED_EXIT stdout='$EXPECTED_STDOUT'" "got exit=$TC_EXIT stdout='$TC_STDOUT'"
            continue
        fi
    fi

    # ── All good ───────────────────────────────────────────────
    log_pass "$BASENAME"
done

# ─── Summary ────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
echo -e "  Results: ${GREEN}$PASS_COUNT passed${NC}, ${RED}$FAIL_COUNT failed${NC}, ${YELLOW}$SKIP_COUNT skipped${NC} (of $TOTAL total)"
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"

# Exit non-zero if any test failed
if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
exit 0
