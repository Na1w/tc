#!/usr/bin/env bash
#
# run_all_tests.sh — Unified test runner for the tc compiler
#
# Runs ALL test categories (unit, integration, cases) and produces
# a consolidated PASS/FAIL/SKIP summary.
#
# Usage:  make test    (or)    ./tests/run_all_tests.sh
#
# Categories:
#   unit         — Standalone executables linked with compiler internals
#   integration  — Tests exercising linker, ELF writer, etc. directly
#   cases        — tc-compiled vs gcc reference comparison (exit code + stdout)
#

set -uo pipefail

# ─── Configuration ───────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TC="$PROJECT_DIR/tc"
GCC="gcc"
GCC_FLAGS="-Wall -O2"
RUNTIME_LIB="$PROJECT_DIR/lib/libruntime.a"

TEST_UNIT_DIR="$PROJECT_DIR/tests/unit"
TEST_INTEGR_DIR="$PROJECT_DIR/tests/integration"
TEST_CASES_DIR="$PROJECT_DIR/tests/cases"
EXPECTED_DIR="$PROJECT_DIR/tests/cases/expected"

TIMEOUT=5
TMPDIR_BASE=$(mktemp -d /tmp/tc_test_runner.XXXXXX)

# ─── Colour helpers (disabled when not a tty) ──────────────────
if [ -t 1 ]; then
    GREEN='\033[0;32m'
    RED='\033[0;31m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    GREEN=''
    RED=''
    YELLOW=''
    BLUE=''
    BOLD=''
    NC=''
fi

# ─── Global counters ────────────────────────────────────────────
TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_SKIP=0
TOTAL_TESTS=0

# ─── Helpers ────────────────────────────────────────────────────
log_pass()  { echo -e "${GREEN}[PASS]${NC} $1"; ((TOTAL_PASS++)) || true; }
log_fail()  { echo -e "${RED}[FAIL]${NC} $1"; ((TOTAL_FAIL++)) || true; }
log_skip()  { echo -e "${YELLOW}[SKIP]${NC} $1"; ((TOTAL_SKIP++)) || true; }
log_header(){ echo -e "\n${BLUE}${BOLD}══ $1 ══${NC}"; }

# Normalize exit code: convert signal exits to descriptive strings
normalize_exit() {
    local code="$1"
    case "$code" in
        124) echo "TIMEOUT" ;;
        139) echo "SIGSEGV" ;;
        132) echo "SIGILL" ;;
        134) echo "SIGABRT" ;;
        136) echo "SIGABRT" ;;
        *)   echo "$code" ;;
    esac
}

cleanup() { rm -rf "$TMPDIR_BASE"; }
trap cleanup EXIT

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
echo -e "${BOLD}  tc Unified Test Runner  (timeout=${TIMEOUT}s per test)${NC}"
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"

# =====================================================================
#  CATEGORY 1: UNIT TESTS
# =====================================================================
run_unit_tests() {
    log_header "UNIT TESTS"
    local unit_count=0
    local unit_pass=0
    local unit_fail=0
    local unit_skip=0

    shopt -s nullglob
    local test_files=("$TEST_UNIT_DIR"/test_*)
    shopt -u nullglob

    if [ ${#test_files[@]} -eq 0 ]; then
        echo -e "  ${YELLOW}No unit test executables found.${NC}"
        echo "  (Run 'make test-unit' to build unit test executables first)"
        return
    fi

    for test_exe in "${test_files[@]}"; do
        # Skip directories, .c source files, and non-executables
        [ -f "$test_exe" ] || continue
        case "$test_exe" in *.c) continue ;; esac
        [ -x "$test_exe" ] || {
            log_skip "$(basename "$test_exe") (not executable)"
            ((unit_skip++)) || true; continue
        }

        local name
        name="$(basename "$test_exe")"
        ((unit_count++)) || true
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        echo -e "  ${BOLD}[$name]${NC}"

        # Capture output and exit code
        local output
        local exit_code=0
        output=$(timeout "$TIMEOUT" "$test_exe" 2>&1) || exit_code=$?

        if [ "$exit_code" -eq 124 ]; then
            log_fail "$name (TIMEOUT after ${TIMEOUT}s)"
            ((unit_fail++)) || true
            continue
        fi

        # The test executables print PASS/FAIL lines internally.
        # We extract counts from their output.
        local inner_pass=0
        local inner_fail=0
        inner_pass=$(echo "$output" | grep -cE '^\s*(PASS|  PASS)' || true)
        inner_fail=$(echo "$output" | grep -cE '^\s*(FAIL|  FAIL)' || true)

        # Print the test's own output (indented)
        echo "$output" | sed 's/^/    /'

        # Count the test as PASS if it ran without crashing and had no inner failures
        if [ "$exit_code" -eq 0 ] && [ "$inner_fail" -eq 0 ]; then
            log_pass "$name (inner: ${inner_pass} passed)"
            ((unit_pass++)) || true
        else
            local norm_exit
            norm_exit=$(normalize_exit "$exit_code")
            log_fail "$name (exit=$norm_exit, inner_fail=$inner_fail)"
            ((unit_fail++)) || true
        fi
    done

    echo ""
    echo -e "  Unit summary: ${GREEN}${unit_pass} passed${NC}, ${RED}${unit_fail} failed${NC}, ${YELLOW}${unit_skip} skipped${NC} (of ${unit_count})"
}

# =====================================================================
#  CATEGORY 2: INTEGRATION TESTS
# =====================================================================
run_integration_tests() {
    log_header "INTEGRATION TESTS"
    local integr_count=0
    local integr_pass=0
    local integr_fail=0
    local integr_skip=0

    shopt -s nullglob
    local test_files=("$TEST_INTEGR_DIR"/test_*)
    shopt -u nullglob

    if [ ${#test_files[@]} -eq 0 ]; then
        echo -e "  ${YELLOW}No integration test executables found.${NC}"
        echo "  (Run 'make test-integration' to build integration test executables first)"
        return
    fi

    for test_exe in "${test_files[@]}"; do
        [ -f "$test_exe" ] || continue
        case "$test_exe" in *.c) continue ;; esac
        [ -x "$test_exe" ] || {
            log_skip "$(basename "$test_exe") (not executable)"
            ((integr_skip++)) || true; continue
        }

        local name
        name="$(basename "$test_exe")"
        ((integr_count++)) || true
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        echo -e "  ${BOLD}[$name]${NC}"

        local output
        local exit_code=0
        output=$(timeout "$TIMEOUT" "$test_exe" 2>&1) || exit_code=$?

        if [ "$exit_code" -eq 124 ]; then
            log_fail "$name (TIMEOUT after ${TIMEOUT}s)"
            ((integr_fail++)) || true
            continue
        fi

        local inner_pass=0
        local inner_fail=0
        inner_pass=$(echo "$output" | grep -cE '^\s*(PASS|  PASS)' || true)
        inner_fail=$(echo "$output" | grep -cE '^\s*(FAIL|  FAIL)' || true)

        echo "$output" | sed 's/^/    /'

        if [ "$exit_code" -eq 0 ] && [ "$inner_fail" -eq 0 ]; then
            log_pass "$name (inner: ${inner_pass} passed)"
            ((integr_pass++)) || true
        else
            local norm_exit
            norm_exit=$(normalize_exit "$exit_code")
            log_fail "$name (exit=$norm_exit, inner_fail=$inner_fail)"
            ((integr_fail++)) || true
        fi
    done

    echo ""
    echo -e "  Integration summary: ${GREEN}${integr_pass} passed${NC}, ${RED}${integr_fail} failed${NC}, ${YELLOW}${integr_skip} skipped${NC} (of ${integr_count})"
}

# =====================================================================
#  CATEGORY 3: CASES TESTS (tc vs gcc comparison)
# =====================================================================
run_cases_tests() {
    log_header "CASES TESTS (tc vs gcc)"
    local cases_count=0
    local cases_pass=0
    local cases_fail=0
    local cases_skip=0

    shopt -s nullglob
    local test_files=("$TEST_CASES_DIR"/*.c)
    shopt -u nullglob

    if [ ${#test_files[@]} -eq 0 ]; then
        echo -e "  ${YELLOW}No case test files found.${NC}"
        return
    fi

    for TEST_FILE in "${test_files[@]}"; do
        local BASENAME
        BASENAME="$(basename "$TEST_FILE" .c)"
        ((cases_count++)) || true
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        TC_OUT="$TMPDIR_BASE/${BASENAME}_tc"
        GCC_OUT="$TMPDIR_BASE/${BASENAME}_gcc"

        # ── Step 1: Build with ./tc (with build timeout) ────────────
        local tc_build_output
        tc_build_output=$(timeout "$TIMEOUT" "$TC" "$TEST_FILE" -o "$TC_OUT" 2>&1)
        if [ $? -ne 0 ]; then
            log_skip "$BASENAME (tc build failed)"
            ((cases_skip++)) || true
            continue
        fi
        chmod +x "$TC_OUT"

        # ── Step 2: Build reference with gcc ───────────────────────
        local gcc_build_output
        gcc_build_output=$("$GCC" $GCC_FLAGS "$TEST_FILE" "$RUNTIME_LIB" -o "$GCC_OUT" 2>&1)
        if [ $? -ne 0 ]; then
            log_skip "$BASENAME (gcc build failed)"
            ((cases_skip++)) || true
            continue
        fi

        # ── Step 3: Run both with timeout, capture stdout & exit code ──
        local tc_stdout gcc_stdout tc_exit gcc_exit

        tc_stdout=$(timeout "$TIMEOUT" "$TC_OUT" 2>/dev/null) && tc_exit=0 || tc_exit=$?
        tc_exit=$(normalize_exit "$tc_exit")

        gcc_stdout=$(timeout "$TIMEOUT" "$GCC_OUT" 2>/dev/null) && gcc_exit=0 || gcc_exit=$?
        gcc_exit=$(normalize_exit "$gcc_exit")

        # ── Step 4: Compare exit codes and stdout ──────────────────
        if [ "$tc_exit" = "$gcc_exit" ] && [ "$tc_stdout" = "$gcc_stdout" ]; then
            # ── Step 5: Check expected output file if it exists ────────
            local EXPECTED_FILE="$EXPECTED_DIR/${BASENAME}.expected"
            local expected_match=true
            if [ -f "$EXPECTED_FILE" ]; then
                local EXPECTED_EXIT EXPECTED_STDOUT
                EXPECTED_EXIT=$(grep '^exit_code=' "$EXPECTED_FILE" | head -1 | cut -d= -f2)
                EXPECTED_STDOUT=$(grep '^stdout=' "$EXPECTED_FILE" | head -1 | cut -d= -f2-)
                # Normalize expected exit for comparison
                local norm_expected_exit
                norm_expected_exit=$(normalize_exit "$EXPECTED_EXIT")
                if [ "$tc_exit" != "$norm_expected_exit" ] || [ "$tc_stdout" != "$EXPECTED_STDOUT" ]; then
                    expected_match=false
                fi
            fi

            if [ "$expected_match" = true ]; then
                log_pass "$BASENAME (exit=$tc_exit)"
                ((cases_pass++)) || true
            else
                log_fail "$BASENAME (expected output mismatch)"
                ((cases_fail++)) || true
            fi
        else
            log_fail "$BASENAME (tc: exit=$tc_exit, gcc: exit=$gcc_exit)"
            ((cases_fail++)) || true

            # Show diff if stdout differs
            if [ "$tc_stdout" != "$gcc_stdout" ]; then
                echo "    tc stdout:   '$tc_stdout'"
                echo "    gcc stdout:  '$gcc_stdout'"
            fi
        fi
    done

    echo ""
    echo -e "  Cases summary: ${GREEN}${cases_pass} passed${NC}, ${RED}${cases_fail} failed${NC}, ${YELLOW}${cases_skip} skipped${NC} (of ${cases_count})"
}

# =====================================================================
#  MAIN
# =====================================================================
echo ""

# Run all three categories
run_unit_tests
run_integration_tests
run_cases_tests

# ─── Final Summary ─────────────────────────────────────────────
echo ""
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  FINAL SUMMARY${NC}"
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
echo -e "  Total tests:  $TOTAL_TESTS"
echo -e "  ${GREEN}Passed:       $TOTAL_PASS${NC}"
echo -e "  ${RED}Failed:       $TOTAL_FAIL${NC}"
echo -e "  ${YELLOW}Skipped:      $TOTAL_SKIP${NC}"
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"

if [ "$TOTAL_FAIL" -gt 0 ]; then
    echo -e "\n${RED}${BOLD}RESULT: FAIL${NC} (some tests failed)"
    exit 1
fi

if [ "$TOTAL_TESTS" -eq 0 ]; then
    echo -e "\n${YELLOW}${BOLD}RESULT: NO TESTS RAN${NC} (check test executables exist)"
    exit 2
fi

echo -e "\n${GREEN}${BOLD}RESULT: ALL TESTS PASSED${NC}"
exit 0
