#!/bin/bash
# Debug script to trace function_call compilation
cd /home/coder/workspace/tc

echo "=== ORIGINAL IR ==="
./tc test/cases/function_call.c -d ir 2>&1

echo ""
echo "=== COMPILING ==="
./tc test/cases/function_call.c -o /tmp/fc_out 2>&1

echo ""
echo "=== RUNNING ==="
/tmp/fc_out
echo "Exit code: $?"

echo ""
echo "=== HEX DUMP OF DOUBLE FUNCTION ==="
./tc -d disasm test/cases/function_call.c 2>&1

echo ""
echo "=== GCC REFERENCE ==="
gcc -O0 -o /tmp/fc_gcc test/cases/function_call.c 2>&1
/tmp/fc_gcc
echo "GCC Exit code: $?"
objdump -d /tmp/fc_gcc 2>&1 | head -60
