Based on extensive reconnaissance of the workspace, I now have a clear picture of the codebase structure and the failing tests. Let me create the execution plan.

```markdown
# Execution Plan — tc Compiler Test Suite Repair (Priority Fixes)

## Current Status & Diagnosis

| Test | Status | Root Cause | Target File(s) |
|------|--------|------------|----------------|
| `ret_42` | PASS | Baseline works | N/A |
| `compare_branch` | FAIL (SIGSEGV) | RSP/RBP used as GPR, or wrong branch offsets | `x86_64.c`, `x86_64_instr.c` |
| `function_call` | FAIL (SIGBUS) | Stack misalignment or invalid call target | `x86_64.c`, `x86_64_emit.c` |
| `hello` | FAIL (Parser) | Lexer doesn't support `...` (variadic) token | `lexer.c`, `parser.c` |
| `loop_while` | FAIL | Incorrect relative jump offsets for loop boundaries | `x86_64_instr.c` |

**Already Verified:** `ALLOC_REGS[]` in `x86_64.c` already excludes RSP (4) and RBP (5). The allocatable pool is {RAX, RCX, RDX, RBX, RSI, RDI, R8-R15} = 14 registers. Register reservation appears correct at definition time — the issue is likely that instructions still reference RSP/RBP as operand registers during emission.

---

## Phase 1: Deep Diagnostics & Root Cause Analysis (PARALLEL)

*   [x] **[t-a1b2]** Disassemble `compare_branch` binary and trace crash -> **Delegate to:** `local_coder`
    *   *Instructions:* Run `make clean && make` in `tc/`. Then compile `tc/test/cases/function_call.c` with `./tc`, capture the SIGSEGV/SIGBUS output. Use Capstone (if available via `pkg-config --exists capstone`) to disassemble the generated binary at the crash address. Focus on: (1) Are RSP or RBP used as operands in mov/cmp/jcc? (2) Is stack 16-byte aligned before `call` instructions? (3) Are branch displacements calculated relative to the *next* instruction byte? Save full analysis + disassembly to `/home/coder/workspace/diag_phase1.txt`.

*   [x] **[t-c3d4]** Read and audit ALL branch/jump emission code in `x86_64_instr.c` -> **Delegate to:** `local_fast_coder`
    *   *Instructions:* Read the full contents of `tc/src/x86_64_instr.c`. Focus on every use of `emit_jmp_rel32`, `emit_jcc_rel32`, branch label lookup, displacement calculation (`target_offset - current_offset - 5`), and the `cg_patch_branches` function. Identify if relative offsets account for the 4-byte displacement field itself (i.e., RIP-relative encoding requires `displacement = target - (rip_after_instruction)`). Save findings to `/home/coder/workspace/diag_branch.txt`.

*   [x] **[t-e5f6]** Audit call instruction emission in `x86_64_emit.c` -> **Delegate to:** `local_fast_coder`
    *   *Instructions:* Read the full contents of `tc/src/x86_64_emit.c` focusing on: (1) `emit_call_rel32` or equivalent function, (2) `emit_push_r` / `emit_pop_r`, (3) stack alignment logic in `cg_emit_call_align`. Also check if `call` instruction encoding uses correct opcode (E8 for near rel32). Verify 16-byte stack alignment before call: the ABI requires RSP % 16 == 0 at function entry. Save findings to `/home/coder/workspace/diag_call.txt`.

*   [x] **[t-g7h8]** Inspect parser for `hello` test failure -> **Delegate to:** `local_fast_coder`
    *   *Instructions:* Read `tc/src/lexer.c` and `tc/src/parser.c`. The `hello.c` test declares `int printf(const char *format, ...);` — the lexer must tokenize `...` as a distinct token. Check if the lexer handles three consecutive dots as a single `TOKEN_ELLIPSIS` or similar. Also check parser's parameter list handling for variadic parameters. Save findings to `/home/coder/workspace/diag_parser.txt`.

---

## Phase 2: Register Safety Verification (Sequential, depends on Phase 1)

*   [x] **[t-i9j0]** Audit ALL register references in emission helpers -> **Delegate to:** `local_coder`
    *   *Instructions:* Read `tc/src/x86_64.c` (full file, including all offsets). Perform a grep for every instance of `R_RSP`, `R_RBP`, or register constants `4` and `5` used as operands (NOT in prologue/epilogue where they're legitimate). Check: (1) Does the register allocator ever assign RSP/RBP to temps? The `ALLOC_REGS[]` array should prevent this, but verify the allocator logic uses it. (2) In `cg_load_temp` and `temp_get_reg`, is there any path that could return 4 or 5? (3) Check prologue/epilogue in `x86_64.c` to ensure RBP setup (`push rbp; mov rbp, rsp`) is correct. Cross-reference with findings from Phase 1 diagnostics. Save audit report to `/home/coder/workspace/audit_reg_safety.txt`.

---

## Phase 3: Apply Fixes (PARALLEL where possible)

*   [x] **[t-k1l2]** Fix Branch Displacement Calculation -> **Delegate to:** `local_coder`
    *   *Instructions:* Based on diagnostics from [t-c3d4] and [t-a1b2], fix relative branch displacement in `tc/src/x86_64_instr.c`. The x86-64 RIP-relative encoding for jumps requires: `displacement = target_offset - (current_instruction_start + instruction_length)`. For a 5-byte jump instruction (opcode + 4-byte disp), this is typically `target - (offset + 5)`. If the current code uses `target - offset` (without subtracting instruction length), it will jump 5 bytes too far. Also fix `cg_patch_branches` if patching logic has the same bug. After applying fixes, run `make clean && make` and test with `compare_branch` and `loop_while`. Save applied patches to `/home/coder/workspace/patch_branch.diff`.

*   [x] **[t-m3n4]** Fix Function Call / Stack Alignment -> **Delegate to:** `local_coder`
    *   *Instructions:* Based on diagnostics from [t-e5f6] and [t-a1b2], fix the call instruction emission. Tasks: (1) Ensure `call` opcode is E8 with correct 32-bit relative displacement (same RIP-relative logic as jumps). (2) Verify stack is 16-byte aligned before `call`: System V ABI requires RSP % 16 == 0 at function entry. Since `call` pushes an 8-byte return address, the caller must ensure RSP % 16 == 8 before executing `call`. Check and fix alignment padding in `cg_emit_call_align`. (3) Verify callee-saved register preservation/restore around calls. After applying fixes, test with `function_call`. Save applied patches to `/home/coder/workspace/patch_call.diff`.

*   [x] **[t-o5p6]** Implement Variadic Function (`...`) Parser Support -> **Delegate to:** `local_coder`
    *   *Instructions:* Based on diagnostics from [t-g7h8], add variadic function support. Tasks: (1) In `tc/src/lexer.c`: Add a token type for `...` (e.g., `TOKEN_ELLIPSIS`). Update the lexer to recognize three consecutive dots as this token. (2) In `tc/src/parser.c`: Update parameter list parsing to accept `...` as the last parameter in a function declaration. The parser should store this appropriately in the AST/function symbol table. (3) Ensure semantic analysis (`sema.c`) handles variadic functions without type errors. After applying fixes, test with `hello`. Save applied patches to `/home/coder/workspace/patch_variadic.diff`.

*   [x] **[t-q7r8]** Fix any remaining register usage issues -> **Delegate to:** `local_coder`
    *   *Instructions:* Based on audit from [t-i9j0], if RSP/RBP are still being used as general-purpose operands, fix the specific code paths. This may involve: (1) Spilling to stack when registers are exhausted rather than using reserved regs, (2) Fixing temp-to-register mapping to skip reserved indices. Apply only after confirming the specific issue from Phase 2 audit. Save patches to `/home/coder/workspace/patch_reg.diff`.

---

## Phase 4: Build Verification & Iteration

*   [x] **[t-s9t0]** Full build + test run (Iteration 1) -> **Delegate to:** `local_coder`
    *   *Instructions:* Run `cd /home/coder/workspace/tc && make clean && make`. Then run the full test suite: `bash test/run_tests.sh`. Also run with disasm mode if Capstone is available: `bash test/run_tests.sh --disasm`. Record exact results for each test (PASS/FAIL, exit code, any error messages). If `compare_branch` or `loop_while` still fail, generate a fresh disassembly of the binary and compare branch offsets manually against expected targets. Save full test report to `/home/coder/workspace/test_report_iter1.txt`.

---

## Phase 5: Capstone Verification (Conditional on build success)

*   [x] **[t-u1v2]** Verify generated code correctness via Capstone -> **Delegate to:** `local_coder`
    *   *Instructions:* For each test case that now compiles successfully, use the `-d disasm` flag or manual Capstone disassembly to verify: (1) No RSP/RBP in non-prologue/epilogue instructions, (2) All branch displacements point to valid instruction addresses, (3) Stack is 16-byte aligned before every `call`, (4) Callee-saved registers are properly preserved. Generate a verification report per test case. Save to `/home/coder/workspace/verification_report.txt`.

---

## Phase 6: Final Validation & Synthesis

*   [x] **[t-w3x4]** Final comprehensive test suite run -> **Delegate to:** `local_coder`
    *   *Instructions:* Run the complete test suite one final time. If all 5 tests pass, document the complete set of changes. If any still fail, provide a detailed breakdown of remaining issues with specific line numbers and suggested fixes. Save final report to `/home/coder/workspace/final_test_report.txt`.

*   [x] **[t-y5z6]** Synthesize final fix report -> **Delegate to:** `local_fallback`
    *   *Instructions:* Read all diagnostic, patch, test report, and verification files from `/home/coder/workspace/`. Compile a comprehensive English report summarizing: (1) Each bug found with root cause analysis, (2) Each fix applied with code snippets, (3) Final test suite status, (4) Any remaining issues or recommendations. Save to `/home/coder/workspace/FIX_REPORT.md`.

---

## Phase 7: Workspace Delivery

*   **Final Delivery:** Orchestrator zips the entire workspace and provides `[Download Workspace](kvaser-file://workspace.zip)`. Also provide individual patch files via `terminal__export_file` with `[Download Patch](kvaser-file://...)` links.
```