# Execution Plan

## Phase 1: Diagnostic & Failure Analysis (Parallel)
**Goal:** Run all tests, capture exact failures, and analyze generated assembly for key test cases.

*   [x] **Task 1.1: Run Full Test Suite** -> **Delegate to:** `local_coder`
    *   *Instructions:* Build the project (`make clean && make`) and run `bash tc/test/run_tests.sh`. Capture all output including exit codes, crash signals, and timeout results. Report which tests pass/fail/crash.

*   [x] **Task 1.2: Analyze Generated Assembly for ret_42** -> **Delegate to:** `local_coder`
    *   *Instructions:* Compile `tc/test/cases/ret_42.c` with the tc compiler and use `-d disasm` flag (or read generated assembly) to inspect the actual binary output. Examine the `_start` entry point, the jump to `main`, and the return value handling in RAX. Check if syscall number 60 is correct and if 42 ends up in RDI before the syscall.

*   [x] **Task 1.3: Analyze Generated Assembly for function_call** -> **Delegate to:** `local_coder`
    *   *Instructions:* Compile `tc/test/cases/function_call.c` and inspect the generated assembly. Verify: (1) function prologue/epilogue structure, (2) argument passing via RDI/RSI per System V ABI, (3) return value handling in RAX after call instruction.

*   [x] **Task 1.4: Review Current x86_64 Backend Code** -> **Delegate to:** `local_fast_coder`
    *   *Instructions:* Read the key files: `tc/src/x86_64.c`, `tc/src/x86_64_emit.c`, `tc/src/x86_64_instr.c`. Identify the entry point generation code (`_start`), the `emit_call`, return value handling, and syscall implementation. Report any obvious bugs in control flow, register allocation for returns, or instruction encoding.

*   [x] **Task 1.5: Review ELF Writer** -> **Delegate to:** `local_fast_coder`
    *   *Instructions:* Read `tc/src/elf.c` and verify the ELF header generation, program headers (PT_LOAD segments), section layout, and relocation handling. Report any issues with entry point address or memory segment alignment.

## Phase 2: Atomic Backend Fixes (Sequential Dependencies)
**Goal:** Fix each identified issue one by one, testing after each fix.

*   [x] **Task 2.1: Fix Entry Point (_start) Jump Logic** -> **Delegate to:** `local_coder`
    *   *Instructions:* Based on diagnostic results, fix the `_start` entry point in x86_64 code generation. Ensure: (1) proper jump/call instruction to main function, (2) correct instruction encoding with relative offsets, (3) alignment before branching. Test by compiling ret_42.c and checking if execution reaches main.

*   [x] **Task 2.2: Fix Return Value/Exit Syscall** -> **Delegate to:** `local_coder`
    *   *Instructions:* Fix the return value handling in the exit syscall sequence. The sequence should be: MOV RDI, <return_value>, MOV RAX, 60 (sys_exit), INT 0x80 or SYSCALL. Verify that the temp register holding the return value is correctly loaded into RDI before the syscall instruction.

*   [x] **Task 2.3: Fix Function Call Convention** -> **Delegate to:** `local_coder`
    *   *Instructions:* Fix the function call generation including: (1) argument setup in ABI_PARAM_REGS (RDI, RSI, RDX, RCX, R8, R9), (2) CALL instruction encoding with correct target address, (3) return value capture in RAX after call returns. Test with function_call.c.

*   [x] **Task 2.4: Fix Stack Frame Management** -> **Delegate to:** `local_coder`
    *   *Instructions:* Fix function prologue/epilogue generation including: (1) proper stack alignment, (2) frame pointer setup if used, (3) callee-saved register preservation, (4) correct local variable offset calculations. Test with all remaining test cases.

*   [x] **Task 2.5: Fix Immediate Value Encoding** -> **Delegate to:** `local_coder`
    *   *Instructions:* Verify immediate value encoding in MOV instructions - ensure sign-extension and proper byte/word/dword/qword sizing. This is critical for values like 42, string addresses, etc. Test with arith_full.c.

## Phase 3: Capstone Validation (Parallel)
**Goal:** Use Capstone disassembler to verify critical instruction sequences.

*   [x] **Task 3.1: Validate _start Section** -> **Delegate to:** `local_coder`
    *   *Instructions:* Enable Capstone debugging (`-DHAVE_CAPSTONE`) and disassemble the generated binary for ret_42.c. Verify the first 20 bytes contain valid instructions (should include jump/call to main). Compare with expected assembly pattern: `JMP/Call main`, `MOV RDI, <val>`, `MOV RAX, 60`, `SYSCALL`.

*   [x] **Task 3.2: Validate Function Call Sequence** -> **Delegate to:** `local_coder`
    *   *Instructions:* Disassemble the function_call.c output using Capstone. Verify: (1) CALL instruction with correct offset, (2) MOV instructions for argument setup use correct registers per System V ABI, (3) return value is properly moved from RAX to appropriate destination.

*   [x] **Task 3.3: Validate Return Value Handling** -> **Delegate to:** `local_coder`
    *   *Instructions:* Disassemble the return_value.c output. Trace the complete execution path: main() calls helper(41), helper returns x+1 (should be 42), main returns that value. Verify each MOV/ADD sequence and the final exit syscall with correct RDI value.

## Phase 4: Final Verification
**Goal:** Complete test suite run and comprehensive validation.

*   [x] **Task 4.1: Run Full Test Suite** -> **Delegate to:** `local_coder`
    *   *Instructions:* Run `make clean && make && bash tc/test/run_tests.sh` again. Report final pass/fail counts. All tests should now pass. If any still fail, document the exact failure modes for further investigation.

*   [x] **Task 4.2: Verify Test-Driven Fixes** -> **Delegate to:** `local_coder`
    *   *Instructions:* For each fixed test case, verify with Capstone that the generated machine code is syntactically valid and semantically correct. Run each test binary manually to confirm exit codes match expected values in tc/test/expected/*.txt files.