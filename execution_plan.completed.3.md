# Execution Plan — tc Compiler (Revised: Modularized Backend + Capstone)

## 0. Completed Phases (No Change)

| Phase | Component | Status | Notes |
|-------|-----------|--------|-------|
| P1 | Lexer (lexer.c/h) | ✅ Done | Tokenization for C subset working |
| P2 | Parser (parser.c/h, ast.c/h) | ✅ Done | Recursive-descent, full AST |
| P3 | Semantic Analysis (sema.c/h) | ✅ Done | Symbol table, type checking |
| P4 | IR Generation (ir.c/h) | ✅ Done | Three-address code emitter |
| P5 | Optimizer (opt.c/h) | ✅ Done | Const-folding, DCE verified |

---

## Phase 6 — Modularize x86-64 Backend (NEW CRITICAL PHASE)

**Current State:** `tc/src/x86_64.c` is a monolithic ~65 KB file (~1700+ lines) containing register allocation, instruction emission, calling conventions, ELF writing glue, and `_start` entry-point logic all in one file. This must be split into discrete modules so each subagent can own, fix, and test independently.

**Target Module Layout:**
```
tc/src/
  x86_64.h            (master header — forward decls only)
  x86_64_reg.h        (Register constants, ABI class definitions)
  x86_64_alloc.c      (Linear-scan register allocator — intervals, live ranges)
  x86_64_callconv.c   (System V ABI calling convention, prologue/epilogue, arg setup)
  x86_64_emit.c       (Raw byte emitter, REX/SIB/modrm helpers)
  x86_64_instr.c      (IR instruction handlers — one function per IR_KIND)
  x86_64_entry.c      (_start runtime entry point generation)
  x86_64_main.c       (cg_create/cg_destroy/cg_generate orchestration — thin glue)
```

*   [x] **Task 6.1: Audit and Catalog Existing Functions** -> **Delegate to:** `local_fast_coder`
    *   *Instructions:* Read `tc/src/x86_64.c`, list every top-level function name with its line range, and categorize each into one of the target modules (reg, alloc, callconv, emit, instr, entry, main). Save the catalog to `/home/coder/workspace/tc/modularization_catalog.txt`.
*   [ ] **Task 6.2: Create Module Headers** -> **Delegate to:** `local_coder`
    *   *Instructions:* Based on the catalog from Task 6.1, create `tc/src/x86_64_reg.h`, `tc/src/x86_64_alloc.h`, `tc/src/x86_64_callconv.h`, `tc/src/x86_64_emit.h`. Each header should have proper include guards and forward-declare only the functions belonging to that module. Also update `tc/src/x86_64.h` to be a single umbrella header that includes all sub-headers.
*   [x] **Task 6.3: Extract Register Allocator Module** -> **Delegate to:** `local_coder`
    *   *Instructions:* Move linear-scan register allocator code (interval struct, live-range computation, coalesce/assign functions) from `x86_64.c` into a new file `tc/src/x86_64_alloc.c`. Ensure it compiles independently with its header. Keep all original function signatures intact to minimize downstream breakage.
*   [ ] **Task 6.4: Extract Calling Convention & Prologue/Epilogue Module** -> **Delegate to:** `local_coder`
    *   *Instructions:* Move prologue emission (`push rbp`, `mov rbp,rsp`, frame-alignment), epilogue emission, callee-saved save/restore, and argument-register loading logic from `x86_64.c` into a new file `tc/src/x86_64_callconv.c`. This includes the `cg_emit_prologue`, `cg_emit_epilogue`, and caller-saved push/pop helpers.
*   [ ] **Task 6.5: Extract Raw Byte Emitter** -> **Delegate to:** `local_coder`
    *   *Instructions:* Move all low-level byte emission helpers (`cg_emit_byte`, REX prefix generation, SIB/modrm encoding, immediate encoding) from `x86_64.c` into a new file `tc/src/x86_64_emit.c`. These are the foundational building blocks used by every other module.
*   [x] **Task 6.6: Extract IR Instruction Handlers** -> **Delegate to:** `local_coder`
    *   *Instructions:* Move the `cg_emit_instr` switch-case body (all per-IR_KIND handlers like LOAD, STORE, ADD, SUB, MUL, DIV, CMP, BR_IF, BR, CALL, RET, etc.) from `x86_64.c` into a new file `tc/src/x86_64_instr.c`. Each handler should remain a distinct static function if possible.
*   [x] **Task 6.7: Extract `_start` Entry Point Generator** -> **Delegate to:** `local_coder`
    *   *Instructions:* Move the `_start` entry point code (argc/argv loading from stack, calling main, exit_group syscall) from `x86_64.c` into a new file `tc/src/x86_64_entry.c`.
*   [ ] **Task 6.8: Create Thin Orchestration Glue** -> **Delegate to:** `local_coder`
    *   *Instructions:* Reduce the remaining `x86_64.c` (renamed conceptually as `x86_64_main.c`) to contain only: `cg_create()`, `cg_destroy()`, and the top-level `cg_generate()` function that calls into alloc, callconv, instr, and entry modules in the correct order.
*   [x] **Task 6.9: Update Makefile for Modular Build** -> **Delegate to:** `local_coder`
    *   *Instructions:* Update `tc/Makefile` so the SRCS wildcard still picks up all new `.c` files under `src/`. Add a specific target `make backend-modular` that only compiles backend modules for quick iteration. Ensure `-lcapstone` is NOT linked yet (will be added in Phase 7).
*   [ ] **Task 6.10: Verify Modular Build** -> **Delegate to:** `local_validator`
    *   *Instructions:* Run `cd /home/coder/workspace/tc && make clean && make` to verify the modularized codebase compiles without errors or warnings. Read all generated `.o` file sizes to confirm no module is unexpectedly empty. Report any compilation failures.

---

## Phase 7 — Capstone Disassembler Integration (NEW PHASE)

**Rationale:** Capstone (`libcapstone-dev`) provides a verified x86-64 disassembler. Integrating it enables automated verification that the tc backend emits correct machine code by round-tripping: IR -> bytes -> disassembly -> verify patterns.

*   [x] **Task 7.1: Research Capstone API** -> **Delegate to:** `local_fast_researcher`
    *   *Instructions:* Search Kiwix ZIM archives and documentation for the Capstone disassembly framework API. Extract key functions: `cs_open()`, `cs_disasm()`, `cs_free()`, register `CS_ARCH_X86`, mode `CS_MODE_64`. Save a concise API cheat-sheet to `/home/coder/workspace/tc/capstone_api_notes.md`.
*   [ ] **Task 7.2: Create Capstone Wrapper Module** -> **Delegate to:** `local_coder`
    *   *Instructions:* Create `tc/src/disasm.c` and `tc/src/disasm.h`. Implement a single function: `void disasm_x86_64(const uint8_t *code, size_t len, uint64_t addr, FILE *out)` that uses Capstone to disassemble machine code to stderr/stdout. Handle cs_open/cs_disasm/cs_free lifecycle properly. Add error handling for missing Capstone library (compile-time `#ifdef HAVE_CAPSTONE`).
*   [x] **Task 7.3: Add CLI `-d disasm` Flag** -> **Delegate to:** `local_coder`
    *   *Instructions:* Modify `tc/src/main.c` to accept a new debug flag `-d disasm`. After `cg_generate()` produces machine code but before writing the ELF file, pass the generated `.text` bytes through `disasm_x86_64()` and print the result. Also add `-d verify` which additionally checks for expected patterns ("ret", "syscall" in `_start`).
*   [x] **Task 7.4: Update Makefile for Capstone** -> **Delegate to:** `local_coder`
    *   *Instructions:* Modify `tc/Makefile` to conditionally link `-lcapstone`. Use `pkg-config --libs capstone` if available, otherwise fallback gracefully without the feature. Add a configure/check step: `HAS_CAPSTONE := $(shell pkg-config --exists capstone 2>/dev/null && echo yes)`. Define `HAVE_CAPSTONE` in CFLAGS when present.
*   [x] **Task 7.5: Build and Verify Disassembly** -> **Delegate to:** `local_validator`
    *   *Instructions:* Run `cd /home/coder/workspace/tc && make clean && make`, then compile a test program with disassembly output: `./tc -d disasm test/cases/hello.c -o /tmp/test_disasm`. Verify the disassembly output contains recognizable x86-64 instructions (e.g., `push rbp`, `mov rbp, rsp`, `ret`). Save disassembly output to `/home/coder/workspace/tc/disasm_output.txt`.

---

## Phase 8 — Backend Bug Fixes (Continued from Original Plan)

*(These tasks remain critical and now operate on the modularized codebase)*

*   [x] **Task 8.1: Fix IR_ALLOC Frame Size Pre-Pass** -> **Delegate to:** `local_coder`
    *   *Instructions:* In the callconv module (`x86_64_callconv.c`), ensure that frame size is computed by scanning all IR instructions BEFORE emitting the prologue. The prologue `sub rsp, N` must use the final total (linear-scan spills + explicit IR_ALLOC needs), aligned to 16 bytes.
*   [x] **Task 8.2: Fix `_start` REX.W Encoding** -> **Delegate to:** `local_coder`
    *   *Instructions:* In the entry module (`x86_64_entry.c`), verify the `mov rdi, [rsp]` instruction for loading argc uses a proper REX.W prefix (0x48 0x8B 0x3C 0x24) to produce a 64-bit load into RDI. Use Capstone disassembly from Phase 7 to verify correctness.
*   [x] **Task 8.3: Fix ELF Entry Point Alignment** -> **Delegate to:** `local_coder`
    *   *Instructions:* In `tc/src/elf.c`, ensure the ELF e_entry field matches exactly where `_start` code is placed (ELF_TEXT_BASE = 0x401000 or equivalent). Cross-check with `readelf -h output` after generation.
*   [x] **Task 8.4: Eliminate Duplicate Code Paths** -> **Delegate to:** `local_coder`
    *   *Instructions:* Ensure `be_start_program()` in `tc/src/backend.c` simply delegates to the new modularized `cg_generate()`. Remove any duplicate logic that exists only in one path. The backend interface should be a thin adapter.
*   [x] **Task 8.5: Add Unhandled IR Kind Logging** -> **Delegate to:** `local_coder`
    *   *Instructions:* In the instruction handler module (`x86_64_instr.c`), add a default case in the switch statement that emits `fprintf(stderr, "WARNING: unhandled IR kind %d\n", instr->kind)` so missing instructions are visible during testing.

---

## Phase 9 — Test Suite Expansion and Automated Verification

*   [x] **Task 9.1: Create Tier 1 Test Cases** -> **Delegate to:** `local_coder`
    *   *Instructions:* Create the following test files under `tc/test/cases/`: `arith_full.c`, `compare_branch.c`, `function_call.c`, `loop_while.c`, `loop_for.c`, `return_value.c`, `stdio_putstr.c`. Each should be minimal and target a specific backend feature.
*   [x] **Task 9.2: Update Test Runner with Disassembly Verification** -> **Delegate to:** `local_coder`
    *   *Instructions:* Modify `tc/test/run_tests.sh` to optionally run each test through `-d verify` mode before execution. Add exit code checking and timeout support. If Capstone verification fails, skip the binary execution but flag the test as "DISASM_FAIL".
*   [x] **Task 9.3: Create Tier 2 Extended Test Cases** -> **Delegate to:** `local_coder`
    *   *Instructions:* Create extended test files: `printf_basic.c`, `pointer_deref.c`, `array_basic.c`, `string_literal.c`, `nested_call.c`. These require libc_minimal integration.
*   [ ] **Task 9.4: Run Full Test Suite** -> **Delegate to:** `local_validator`
    *   *Instructions:* Execute `cd /home/coder/workspace/tc && make test`. Capture full output. Categorize each test as PASS, FAIL (wrong output), CRASH (segfault/timeout), or DISASM_FAIL. Save results to `/home/coder/workspace/tc/test_results.txt`.

---

## Phase 10 — libc_minimal Integration

*   [x] **Task 10.1: Build libc_minimal Static Library** -> **Delegate to:** `local_coder`
    *   *Instructions:* Add a Makefile target to compile `tc/lib/libc_minimal.c` with GCC (`-fPIC`, `-static`) and archive it as `tc/obj/libc_minimal.a`. Update `tc/Makefile`.
*   [ ] **Task 10.2: Implement Post-Link Step** -> **Delegate to:** `local_coder`
    *   *Instructions:* In `tc/src/main.c`, after generating the raw ELF binary, optionally invoke `gcc -static -o final_output raw_elf obj/libc_minimal.a` as a post-processing step when the `-l` flag is used. This provides printf/exit support for tc-compiled programs.
*   [ ] **Task 10.3: Test libc Integration** -> **Delegate to:** `local_validator`
    *   *Instructions:* Compile `tc/test/cases/printf_basic.c` with libc linking and verify the output matches expectations. Save results to `/home/coder/workspace/tc/libc_test_results.txt`.

---

## Phase 11 — Final Verification and Workspace Delivery

*   [x] **Task 11.1: End-to-End Smoke Test** -> **Delegate to:** `local_validator`
    *   *Instructions:* Run the complete pipeline: `make clean && make && ./tc test/cases/hello.c -d disasm -o /tmp/smoke_test && /tmp/smoke_test`. Verify exit code 0, correct stdout, and valid disassembly output. Confirm all backend modules are present and non-empty by reading file contents of each `tc/src/x86_64_*.c`.
*   [ ] **Task 11.2: Final Content Validation** -> **Delegate to:** `local_validator`
    *   *Instructions:* Read the FIRST 50 lines and LAST 20 lines of EVERY backend module file (`x86_64_reg.h`, `x86_64_alloc.c`, `x86_64_callconv.c`, `x86_64_emit.c`, `x86_64_instr.c`, `x86_64_entry.c`, `x86_64_main.c`, `disasm.c`) to verify they contain real code and not just stubs. Do NOT rely on file size alone.
*   [ ] **Task 11.3: Orchestrator Final Delivery**
    *   *Instructions:* Zip the entire workspace (`/home/coder/workspace`) and provide the download link `[Download Workspace](kvaser-file://workspace.zip)`.

---

## Dependency Graph Summary

```
Phase 6 (Modularize) --> Phase 8 (Fix Bugs) --> Phase 9 (Test Suite)
      |                      |                       |
Phase 7 (Capstone) ---------+-----------------------+
      |                                              |
Phase 10 (libc_minimal) <----------------------------+
                                                       |
Phase 11 (Final Verification + Delivery) <-------------+
```

**Parallelism Opportunities:**
- [ ] Phase 6 tasks 6.2 through 6.8 can execute in parallel after Task 6.1 completes.
- [ ] Phase 7 Tasks 7.1 and 7.2 can run alongside Phase 6 modularization.
- [ ] Phase 9 Tasks 9.1 and 9.3 (test case creation) can run in parallel with each other.