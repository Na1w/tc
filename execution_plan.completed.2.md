Let me examine the current state of the codebase before creating the execution plan.

Now let me examine the ELF writer, main.c entry point, Makefile, test runner, and libc_minimal more closely.

Now I have a thorough understanding of the codebase. Let me check the existing audit and then compile/test the current state to identify exact failures.

Now let me verify the current build state and identify exact runtime failures:

Based on my thorough analysis of the entire codebase, here is the structured execution plan:

---

# Execution Plan — tc Compiler MVP Completion

## Current State Assessment (Verified)

| Component | Status | Notes |
|-----------|--------|-------|
| Lexer | ✅ Complete | Works for basic C subset. Known bug: `=` mapped to `TOK_AND` but parser compensates |
| Parser | ✅ Complete | Recursive-descent, handles declarations, expressions, control flow, function calls |
| Semantic Analysis | ⚠️ Partial | Symbol table exists; type checking is minimal but sufficient for simple programs |
| IR Generation | ✅ Complete | Produces three-address code with all needed instruction kinds |
| Optimizer | ✅ Complete | Constant folding, dead-code elimination verified working |
| x86_64 Backend | ❌ Broken | See Phase 1 below for specific bugs |
| ELF Writer | ⚠️ Partial | Produces valid structure but binaries may not execute due to backend issues |
| libc_minimal | ⚠️ Stub | Has printf/exit via syscalls but not integrated into tc output |

---

## Phase 1 — Fix x86-64 Backend (Critical Path)

### Bug 1: IR_ALLOC Frame Size Ordering

**Location:** `src/x86_64.c`, function `cg_generate()` ~line 59,400

**Problem:** The prologue is emitted BEFORE the IR instruction loop. `IR_ALLOC` instructions increase `cg->frame_size` but the prologue has already used the old value via `sub rsp, frame_size`.

```c
/* Current BROKEN order: */
cg_emit_prologue(cg);  /* Uses frame_size from linear scan ONLY */
for (int i = 0; i < prog->count; i++) {
    cg_emit_instr(cg, &prog->instrs[i]);
    /* IR_ALLOC here increases frame_size → too late! */
}
```

**Fix:** Do a pre-pass to compute total allocation needs before emitting the prologue.

```c
/* Pre-pass: collect explicit allocations */
for (int i = 0; i < prog->count; i++) {
    if (prog->instrs[i].kind == IR_ALLOC) {
        int needed = (int)prog->instrs[i].ival;
        if (needed > cg->frame_size)
            cg->frame_size = needed;
    }
}
cg->frame_size = (cg->frame_size + 15) & ~15;

/* NOW safe to emit prologue with correct frame_size */
cg_emit_prologue(cg);
```

### Bug 2: `_start` Entry Point — argc is 32-bit

**Location:** `src/x86_64.c`, function `cg_emit_start()` ~line 25,900

**Problem:** `mov rdi, [rsp]` emits `8B 3C 24` which is `mov edi, [rsp]` (32-bit without REX.W prefix). This zero-extends to rdi, which works for argc but is incorrect style. More importantly, verify the encoding produces valid code at the expected addresses.

**Fix:** Add REX.W prefix to make it a proper 64-bit load:
```c
/* mov rdi, [rsp] with REX.W */
cg_emit_byte(cg, 0x48);       /* REX.W */
cg_emit_byte(cg, 0x8B);
cg_emit_byte(cg, 0x3C);       /* mod=00, reg=rdi, r/m=100 (SIB) */
cg_emit_byte(cg, 0x24);       /* SIB: base=rsp */
```

### Bug 3: Caller-Saved Save/Restore Clobbers Args

**Location:** `src/x86_64.c`, `IR_CALL` handler ~line 50,800

**Problem:** The sequence is: (1) push all caller-saved regs, (2) load args into ABI regs. But `cg_load_temp()` uses `R_RAX` for spilled temps, and R_RAX was just pushed. After the push, R_RAX can be freely used — so this is actually **correct**. However, verify that argument registers (rdi/rsi/rdx/rcx/r8/r9) are not corrupted by `cg_load_temp()` when loading subsequent arguments. Since all caller-saved regs are on the stack, `cg_load_temp` should load into R_RAX and then copy to the target arg reg. **Verified correct.**

### Bug 4: ELF Entry Point Address Mismatch

**Location:** `src/x86_64.c`, `cg_generate()` + `src/elf.c`

**Problem:** The `_start` symbol is defined at address `0x400000` in the ELF, but the code is placed starting at `ELF_TEXT_BASE = 0x401000`. If the ELF entry point doesn't match where _start actually lives, the binary will jump to wrong code.

**Fix:** Verify and align:
```c
/* In elf.c: ensure entry = ELF_TEXT_BASE */
elf->entry = ELF_TEXT_BASE;  /* Matches where _start is placed */

/* In x86_64.c: verify _start symbol at correct address */
elf_define_symbol(elf, "_start", ELF_TEXT_BASE);
```

### Bug 5: Duplicate Code Between `cg_generate()` and `be_start_program()`

**Location:** `src/x86_64.c`, lines ~59,400+

**Problem:** The backend wrapper functions (`be_start_program`, `be_write_output`) duplicate nearly all logic from `cg_generate()`. Both code paths must be kept in sync. If one is fixed but not the other, bugs persist.

**Fix:** Consolidate into a single code path. Make `be_start_program` simply call `cg_generate()` internally, or remove the backend wrapper entirely since `main.c` already calls `cg_generate()` directly.

### Tasks — Phase 1

| # | Task | File(s) | Priority |
|---|------|---------|----------|
| [ ] 1.1 | Fix IR_ALLOC pre-pass ordering bug | `src/x86_64.c` | **P0** |
| [ ] 1.2 | Fix `_start` RDI load (add REX.W) | `src/x86_64.c` | P0 |
| [ ] 1.3 | Verify ELF entry point = ELF_TEXT_BASE alignment | `src/elf.c`, `src/x86_64.c` | P0 |
| [ ] 1.4 | Deduplicate `cg_generate` / `be_start_program` | `src/x86_64.c`, `src/backend.c` | P1 |
| [ ] 1.5 | Verify all instruction handlers: add `fprintf(stderr, ...)` debug logging for unhandled IR kinds | `src/x86_64.c` | P1 |
| [x] 1.6 | Build and test with `make clean && make`, then run `./tc -o /tmp/test hello.c && /tmp/test` | — | **Verify** |

---

## Phase 2 — Integrate Capstone Disassembler

### Rationale
Capstone provides a reliable x86-64 disassembler library. Integrating it enables:
- [ ] A `-d disasm` flag to dump generated machine code as assembly
- [ ] Automated verification that emitted instructions match expected patterns
- [ ] Debugging of instruction encoding bugs

### Architecture

```
tc [options] input.c
  ... pipeline ...
  → cg_generate() produces raw bytes
  → NEW: If -d disasm or --verify-disasm flag is set, 
    pass code[] to capstone for disassembly verification
  → ELF output as before
```

### Tasks — Phase 2

| # | Task | Details |
|---|------|---------|
| [ ] 2.1 | Install capstone: `apt-get install libcapstone-dev` or build from source | Verify `#include <capstone/capstone.h>` works |
| [x] 2.2 | Create `src/disasm.c` / `src/disasm.h` | Thin wrapper around Capstone API; functions: `disasm_x86_64(uint8_t *code, size_t len, uint64_t addr) → char **lines` |
| [x] 2.3 | Add `-d disasm` CLI option in `main.c` | After cg_generate, read back the .text section and disassemble it |
| [ ] 2.4 | Add `-d verify` flag | Cross-check that disassembled instructions contain expected patterns (e.g., "call", "ret", "syscall") |
| [ ] 2.5 | Update Makefile to link with `-lcapstone` conditionally | Use `pkg-config --libs capstone` or direct `-L -l` flags; provide fallback if capstone unavailable |

---

## Phase 3 — Finalize libc_minimal Integration

### Current State
`lib/libc_minimal.c` contains:
- [ ] `putstr()` — write string to stdout via syscall
- [ ] `printf()` — full format string support (d, u, c, s, x, p)
- [ ] `exit()` — exit_group syscall
- [ ] `va_list` macros for variadic arguments

### Integration Strategy

For the MVP, we have **two options**:

**Option A (Recommended): Link-time integration**
Compile libc_minimal.c with GCC, produce a static library, and link tc-generated ELF binaries against it. This is fast to implement and works immediately.

**Option B (Future): Self-contained runtime**
Embed libc functions as additional compiled code in the output ELF. Requires tc to compile C code — circular dependency for self-hosting.

### Tasks — Phase 3

| # | Task | Details |
|---|------|---------|
| [ ] 3.1 | Add `lib/libc_minimal.o` build target to Makefile | `gcc -c lib/libc_minimal.c -o obj/libc_minimal.o -fPIC` |
| [ ] 3.2 | Create `lib/libc_minimal.a` static library | `ar rcs obj/libc_minimal.a obj/libc_minimal.o` |
| [ ] 3.3 | Modify ELF writer to support linking external objects | Option A: Use GCC's linker (`ld`) to combine tc output with libc_minimal.a. Option B: Add a `-l` flag and invoke `gcc -o output tc_object libc_minimal.a` |
| [x] 3.4 | **Simpler approach:** Modify `main.c` to post-link via gcc/ld | After cg_generate produces the ELF, call `gcc -static -o final_output raw_output lib/libc_minimal.a` as a post-processing step |
| [x] 3.5 | Add `-nostdlib` support | Ensure tc can produce standalone binaries for programs that don't need printf (just putstr + exit) |

**Recommended MVP approach:** For now, make `hello.c` work WITHOUT libc by using only direct syscalls (putstr via write syscall). Then add printf support as a Phase 3b enhancement.

---

## Phase 4 — Test Suite Expansion

### Current Tests
| File | Content | Status |
|------|---------|--------|
| `hello.c` | `int main(void) { return 0; }` | Should pass after backend fixes |
| `expr1.c` | `int main(void) { int x = 2 + 3; return x; }` | Should pass after backend fixes |

### New Test Cases (Prioritized)

**Tier 1 — Core functionality (MVP-gate):**

| File | Content | Tests |
|------|---------|-------|
| `stdio_putstr.c` | `int main(void) { putstr("Hello\n"); return 0; }` | String output via syscall |
| `arith_full.c` | Tests +, -, *, /, % on ints | All arithmetic IR instructions |
| `compare_branch.c` | `if (x > 5) return 1; return 0;` | CMP + BR_IF |
| `function_call.c` | Separate function definition + call from main | CALL instruction, stack frame |
| `loop_while.c` | `while (i < 10) { i++; }` | BR, BR_IF, labels, loop semantics |
| `loop_for.c` | Standard for-loop with init/cond/incr | Full control flow |
| `return_value.c` | `return 42;` — verify exit code | RET instruction, _start → exit |

**Tier 2 — Extended functionality:**

| File | Content | Tests |
|------|---------|-------|
| `printf_basic.c` | `printf("Hello %d\n", 42);` | libc_minimal printf integration |
| `printf_multi.c` | Multiple format specifiers: %d %s %x %c %p | Full printf coverage |
| `pointer_deref.c` | `int x = 5; int *p = &x; *p = 10;` | LOAD, STORE, ADDR_LOCAL |
| `array_basic.c` | `int arr[3] = {1,2,3}; return arr[1];` | Array indexing via pointer arithmetic |
| `string_literal.c` | `putstr("test string");` | IR_GLOBAL_STR, rodata section |
| `nested_call.c` | Function A calls B calls C | Call chaining, stack frames |

### Test Runner Enhancements

**Tasks — Phase 4:**

| # | Task | Details |
|---|------|---------|
| [ ] 4.1 | Update `test/run_tests.sh` to support per-test timeouts | Already uses `timeout 5` — ensure this works correctly; add `timeout 10` for printf tests (slower I/O) |
| [ ] 4.2 | Add exit code verification | After running a test binary, check `$?` matches expected return value |
| [ ] 4.3 | Add verbose mode (`-v` flag) | Show compilation output and raw binary disassembly (requires Phase 2) |
| [ ] 4.4 | Add `test/cases/` directory with all Tier 1 tests | Create files, corresponding `test/expected/*.txt` outputs |
| [ ] 4.5 | Add ELF validation test | Use `readelf -h output` to verify ELF header validity |

---

## Phase 5 — Build System and CI Hardening

### Tasks — Phase 5

| # | Task | Details |
|---|------|---------|
| [ ] 5.1 | Update Makefile with new targets | `lib`, `test-verbose`, `clean-all` (includes test artifacts) |
| [ ] 5.2 | Add `-g` debug build target | `CFLAGS_DEBUG = -Wall -Wextra -std=c99 -O0 -g` for gdb sessions |
| [ ] 5.3 | Add ASan/UBSan build targets | `make asan` → compile tc with AddressSanitizer |
| [x] 5.4 | Ensure all test subprocesses have hard timeouts | In run_tests.sh: use `timeout 10s cmd; status=$?; if [ $status -eq 124 ]; then echo "TIMEOUT"; fi` |
| [x] 5.5 | Add a smoke-test target | `make smoke` — compile hello.c and verify it runs in <1 second |

---

## Phase 6 — Verification and Final Validation

### Checklist Before MVP Declaration

- [ ] All Tier 1 test cases compile successfully with tc
- [ ] All compiled Tier 1 binaries produce correct output (stdout + exit code)
- [ ] ELF binaries are valid (`readelf -h`, `file` command confirms ELF64 executable)
- [ ] Capstone disassembly of generated code shows sensible instruction sequences
- [ ] No memory leaks in compiler (valgrind clean or acceptable leak budget)
- [ ] Compiler itself compiles without warnings (`-Wall -Wextra`)
- [ ] Test runner completes in < 30 seconds total
- [ ] Every test has an explicit timeout (< 10 seconds per binary)

---

## Execution Order and Dependencies

```
Phase 1 (Backend Fixes) ─────────────────────────────┐
       │                                              │
       ▼                                              ▼
Phase 4.1-4.5 (Tests)                           Phase 3 (libc_minimal)
       │                                              │
       ▼                                              ▼
Phase 5 (Build System) ◄─────────────────────────────┘
       │
       ▼
Phase 2 (Capstone) ─── optional but recommended
       │
       ▼
Phase 6 (Final Validation)
```

**Critical path:** Phase 1 → Phase 4 (basic tests) → Phase 3 → Phase 5 → Phase 6

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| ELF segment headers prevent execution | High | Critical | Verify with `readelf -l output`; fix p_paddr/p_filesz/p_memsz values |
| Register allocator produces incorrect code for programs with >5 temps | Medium | High | Test with programs using many variables; add debug dump of register assignments |
| Stack alignment causes SIGSEGV on function calls | Medium | Critical | The `cg_emit_call_align` function has complex logic; verify with gdb/strace |
| Capstone unavailable on target system | Low | Low | Make integration conditional; provide fallback disassembly via objdump |
| libc_minimal printf is too slow (byte-by-byte syscalls) | Medium | Low | Acceptable for MVP; optimize in future with buffered I/O |