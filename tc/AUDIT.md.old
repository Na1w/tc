# tc (Tiny C) Compiler — Audit Report

**Date:** 2026-07-05
**Scope:** Full source code review of all modules
**Goal:** Document current state, identify gaps, recommend actions

---

## Executive Summary

The tc compiler implements the classic compiler pipeline (Lexer → Parser → AST → Semantics → IR → Optimization → Codegen → ELF) and can compile extremely trivial programs (essentially `return 0;`). However, **it is not a working compiler for any useful C subset**. The code generation backend is severely incomplete: stack variable allocation, storage, loading, function calls, and parameter passing are all no-ops. The semantic analyzer is a stub. The lexer has a critical token collision bug. The compiler will produce ELF binaries for the simplest programs but will fail or produce incorrect machine code for anything involving variables, function calls, or meaningful control flow.

**Overall maturity: ~15-20% of a working ANSI C subset compiler.**

---

## 1. Lexer (`src/lexer.[ch]`)

### What it does
- Tokenizes identifiers, keywords (18 keywords), integer literals (decimal, hex, octal), string literals, character literals
- Handles `//` and `/* */` comments
- Recognizes all standard C operators including compound assignment, comparison, logical, increment/decrement
- Uses a fixed keyword table with indexed lookup

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **CRITICAL** | `=` is mapped to `TOK_AND` (line: `case '=': return make_token(TOK_AND, lex->line);`). There is no `TOK_ASSIGN` token. The lexer conflates assignment `=` with bitwise AND `&`. The parser compensates by checking for `TOK_AND` where `=` is expected, but this is a fundamental design flaw. |
| **HIGH** | String literal storage uses `static char buf[4096]` — only one string can exist at a time. All string tokens share the same buffer. |
| **HIGH** | Integer literals use `int` (32-bit) for parsing — overflows on any value > 2^31-1. No `long`/`long long` support. |
| **MEDIUM** | No `long`, `short`, `unsigned`, `float`, `double` type keywords. |
| **MEDIUM** | `TOK_DBLAMP` defined in enum but never emitted (presumably for `&&` but `&&` maps to `TOK_ANDAND`). Dead token. |
| **MEDIUM** | `TOK_AT` emitted for `@` — not a C token. |
| **LOW** | `strdup_st` is a hand-rolled strdup (malloc + memcpy). Fine but non-standard. |
| **LOW** | Keyword lookup is O(n) linear scan (n=18). Not a performance issue at this scale. |

### Recommendations
- **REWRITE** the token enum: Add `TOK_ASSIGN` for `=`, separate from `TOK_AMP` for `&`.
- **FIX** string literal: Use `strdup_st` (already available) instead of static buffer.
- **EXTEND** integer parsing: Use `unsigned long long` and handle suffixes (U, L, LL).
- **EXTEND** keyword table: Add `long`, `short`, `unsigned`, `signed`, `float`, `double`, `const`, `static`, `extern`, `register`, `auto`, `volatile`.

---

## 2. Parser (`src/parser.[ch]`)

### What it does
- Recursive-descent parser with proper operator precedence
- Parses: function definitions, variable declarations, compound statements
- Control flow: `if/else`, `while`, `for`, `return`, `break`, `continue`
- Expressions: arithmetic (`+`, `-`, `*`, `/`, `%`), comparisons (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`), unary (`+`, `-`, `!`, `~`, `&`, `*`), assignment (`=`, `+=`, `-=` etc.), comma operator
- Primary expressions: integers, chars, strings, identifiers, function calls, member access (`.` and `->`), array subscript (`[]`), parenthesized expressions
- `sizeof` supported (hardcoded to 4)
- Pointer types via `*`

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **CRITICAL** | Assignment detection uses `TOK_AND` (inherited from lexer bug). `parse_assign` checks `p->current.type == TOK_AND` for `=`. This will misfire on actual `&` tokens in expressions. |
| **HIGH** | `parse_var_def` also checks `TOK_AND` for `=` initialization. Same bug. |
| **HIGH** | Local variable declarations are wrapped in `ast_expr_stmt(decl)` instead of a proper `NODE_DECL` statement. This is semantically wrong. |
| **HIGH** | `parse_stmt_list` cannot distinguish local declarations from expression statements robustly. It tries keyword-based detection, which will fail on edge cases. |
| **MEDIUM** | Keywords for `do`, `switch`, `case`, `default`, `struct`, `enum`, `typedef` are defined in lexer but **never parsed**. Dead keywords. |
| **MEDIUM** | `sizeof` is hardcoded to return 4 regardless of operand type. |
| **MEDIUM** | No `do-while` loop parsing. |
| **MEDIUM** | No `switch/case/default` parsing. |
| **MEDIUM** | No `struct` definition or usage parsing. |
| **MEDIUM** | No `enum` parsing. |
| **MEDIUM** | No `typedef` parsing. |
| **MEDIUM** | No function declarations (prototypes) — only function definitions. |
| **MEDIUM** | No `extern` variable declarations. |
| **LOW** | `parse_stmt` has a fallthrough: if a keyword is seen that isn't handled by the switch, it falls through to local-declaration parsing. This can cause incorrect parsing. |
| **LOW** | `parser_free` is a no-op — leaks all parser-allocated memory. |

### Recommendations
- **FIX** all `TOK_AND` → assignment references to use a proper `TOK_ASSIGN`.
- **EXTEND** parser to handle `do-while`, `switch/case/default`, `struct`, `enum`, `typedef`.
- **REWRITE** statement parsing to properly distinguish declarations from expressions (need a lookahead or proper grammar).
- **FIX** local declarations to produce proper `NODE_DECL` nodes, not `NODE_EXPR_STMT`.
- **EXTEND** `sizeof` to compute actual sizes based on type.

---

## 3. AST (`src/ast.[ch]`)

### What it does
- Defines a comprehensive AST node structure with a union for all node types
- Supports: Program, Declaration, Function, Compound statement, Expression statement, If, While, For, Return, Assignment, Binary/Unary ops, Function call, Member access, Identifiers, Integer/Char/String literals, Parameters
- Type system: `void`, `int`, `char`, pointer, array, struct (base types)
- Linked-list chaining for sibling nodes
- Full `ast_free` implementation for memory cleanup

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **HIGH** | Missing node types: `NODE_DO_WHILE`, `NODE_SWITCH`, `NODE_CASE`, `NODE_DEFAULT`, `NODE_TYPEDEF`, `NODE_ENUM`, `NODE_STRUCT_DEF`, `NODE_MEMBER_ACCESS`, `NODE_ARRAY_SUBSCRIPT`. |
| **MEDIUM** | `Type` struct has `struct_name` for struct types but no member list. Cannot represent struct definitions. |
| **MEDIUM** | No `NODE_GOTO`, `NODE_LABEL` for gotos and labeled statements. |
| **MEDIUM** | No `NODE_TERNARY` for `?:` operator (lexer has `TOK_QUESTION` but no AST node). |
| **MEDIUM** | No `NODE_COMPOUND_ASSIGN` — compound assignments use `NODE_ASSIGN` with operator in `op` field. Works but conflates simple and compound assignment. |
| **LOW** | `ast_ident`, `ast_int_lit`, etc. cast `const char *` to `char *` — loses const correctness. |
| **LOW** | `Type` struct allocates with `calloc` but `ast_` functions use `new_node` which also uses `calloc`. Double-zeroing overhead is negligible but inconsistent. |

### Recommendations
- **EXTEND** node types for missing constructs (do-while, switch, ternary, goto, labeled stmts, struct def, enum def).
- **EXTEND** `Type` struct to hold struct member lists.
- **EXTEND** type system: add `long`, `short`, `unsigned`, `signed`, `float`, `double`, function types.
- **MINOR FIX**: Restore const correctness in string parameter casts.

---

## 4. Semantic Analysis (`src/sema.[ch]`)

### What it does
- Recursively walks the AST
- Checks that `main` returns `int`
- Recursively visits all child nodes

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **CRITICAL** | This is a **stub**. It performs essentially zero semantic analysis. |
| **CRITICAL** | No symbol table. No variable declaration tracking. No scope management. |
| **CRITICAL** | No type checking on expressions (e.g., `int + char` is allowed without warning). |
| **CRITICAL** | No function signature validation (parameter count/types not checked against calls). |
| **CRITICAL** | No return type checking (calling a function that returns `void` and using its result is not caught). |
| **CRITICAL** | No undeclared variable detection. |
| **CRITICAL** | No duplicate declaration detection. |
| **CRITICAL** | No `void` type misuse detection. |
| **HIGH** | `check_node` calls `abort()` on the only error it detects — no error recovery. |
| **MEDIUM** | No constant expression evaluation for array sizes, case labels, etc. |
| **MEDIUM** | No `break`/`continue` scope validation (they can appear outside loops). |

### Recommendations
- **REWRITE** entirely. This module needs:
  1. A symbol table (hash map or similar) with scope nesting
  2. Type inference/checking for all expressions
  3. Function declaration and definition matching
  4. Variable declaration and usage tracking
  5. Return statement validation
  6. `break`/`continue` scope validation
  7. Error reporting that doesn't abort

---

## 5. IR Generation (`src/ir.[ch]`)

### What it does
- Translates AST to three-address code (SSA-like) intermediate representation
- IR instruction types: NOP, LOAD, STORE, LOAD_ADDR, CONST, BINOP, UNARY, CALL, RETURN, JMP, JMPZ, JMPNZ, LABEL, ALLOC, PARAM, GLOBAL, STRING_DATA
- Generates IR for: programs, compound statements, declarations, expression statements, returns, if/else, while, for, binary/unary ops, function calls, literals, strings
- IR functions are extracted from AST function nodes
- `ir_print` for debugging
- `ir_free` for cleanup

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **CRITICAL** | `append_inst` is a no-op: `static void append_inst(IRInst **head, IRInst *inst) { (void)head; (void)inst; }`. This function is declared but does nothing. |
| **CRITICAL** | `gen_node` for `NODE_IDENT` returns `NULL`. Identifiers are never resolved to IR loads. Any reference to a variable produces no IR code. |
| **CRITICAL** | `IR_ALLOC` generates an allocation instruction but the backend (x86_64) does nothing with it. Variables are never allocated on the stack. |
| **CRITICAL** | `IR_STORE` generates a store instruction but the backend does nothing with it. Values are never stored. |
| **CRITICAL** | `IR_LOAD` is defined in the enum but never generated by `gen_node`. There is no code path that emits a LOAD. |
| **CRITICAL** | `IR_LOAD_ADDR` is defined but never generated. `&` (address-of) operator produces no usable IR. |
| **HIGH** | `IR_CALL` is generated but the backend does nothing with it. Function calls produce no code. |
| **HIGH** | `IR_PARAM` is defined but never generated. Function parameters are never handled. |
| **HIGH** | `IR_GLOBAL` is defined but never generated. Global variables are not supported. |
| **HIGH** | `NODE_BREAK` and `NODE_CONTINUE` return `NULL` — no IR is generated for them. |
| **MEDIUM** | `gen_node` for `NODE_FUNC` returns `NULL` — functions are handled at the `ir_generate` level, but this means nested function references don't work. |
| **MEDIUM** | Slot numbers (`new_slot`) are global counters, not per-function. Slots will collide across functions. |
| **MEDIUM** | Label numbers (`new_label`) are global counters. Labels will collide across functions. |
| **MEDIUM** | IR chaining is done via linked lists with repeated `while (cur->next) cur = cur->next` — O(n²) for deep expression trees. |
| **LOW** | `ir_generate` generates globals IR via `gen_node(ast)` which walks the entire program, then re-walks to extract functions. Redundant work. |

### Recommendations
- **REWRITE** identifier resolution: `NODE_IDENT` must emit `IR_LOAD` from the resolved slot.
- **FIX** `append_inst` or remove it.
- **FIX** slot/label counters to be per-function or use unique global IDs with proper scoping.
- **EXTEND** IR generation for `break`/`continue` (emit `IR_JMP` to loop exit/start labels).
- **EXTEND** IR generation for function calls (emit `IR_PARAM` for args, `IR_CALL` with function name).
- **EXTEND** IR generation for address-of (`IR_LOAD_ADDR`).
- **EXTEND** IR generation for global variables.
- **REWRITE** IR chaining to use a tail pointer or builder pattern instead of O(n²) list traversal.

---

## 6. Optimizer (`src/opt.[ch]`)

### What it does
- Constant folding: detects adjacent `IR_CONST` + `IR_BINOP` patterns and pre-computes results
- Dead code elimination: removes `IR_NOP` instructions
- `opt_run_all` runs both passes

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **HIGH** | Constant folding only works when both operands are `IR_CONST` instructions that are **immediately adjacent** (prev and pprev). If there's any intervening instruction, folding fails. |
| **HIGH** | Constant folding uses raw token type values (`case 1: /* TOK_PLUS */`) — fragile coupling to lexer enum values. |
| **MEDIUM** | DCE only removes `IR_NOP` instructions. No actual dead code analysis (unused variables, unreachable code, etc.). |
| **MEDIUM** | No copy propagation, common subexpression elimination, or peephole optimization. |
| **MEDIUM** | Constant folding converts `IR_BINOP` to `IR_CONST` but doesn't remove the now-unused `IR_CONST` source instructions. |
| **LOW** | No iteration of optimization passes (one pass only). |

### Recommendations
- **EXTEND** constant folding to track constant values in a map/slot table, not just adjacent instructions.
- **EXTEND** DCE to actually trace live values and eliminate unused computations.
- **EXTEND** with copy propagation and CSE.
- **FIX** use symbolic IR opcodes instead of raw token values.

---

## 7. Backend / x86-64 Codegen (`src/backend.[ch]`, `src/x86_64.[ch]`)

### What it does
- Backend abstraction layer with function pointers for prologue, instruction emission, epilogue, etc.
- x86-64 code emitter with raw byte emission
- Implements: CONST (load to RAX), BINOP (add/sub/mul on RAX/RCX), RETURN (move to RAX + epilogue), LABEL (record offset), JMP/JMPZ/JMPNZ (conditional/unconditional branches), STRING_DATA (copy to rodata)
- Two-pass label resolution
- Emits `_start` entry point that calls `main` and exits via syscall
- Only emits the `main` function; all other functions are silently ignored

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **CRITICAL** | `IR_CALL` handler is empty: `case IR_CALL: break;`. Function calls produce zero machine code. |
| **CRITICAL** | `IR_ALLOC` handler is empty: `case IR_ALLOC: break;`. No stack space is ever allocated for local variables. |
| **CRITICAL** | `IR_STORE` handler is empty: `case IR_STORE: break;`. Values are never stored to memory. |
| **CRITICAL** | `IR_LOAD` handler is missing from the switch. Loads produce nothing. |
| **CRITICAL** | `IR_LOAD_ADDR` handler is missing. Address-of produces nothing. |
| **CRITICAL** | `IR_PARAM` handler is missing. Function arguments are never passed. |
| **CRITICAL** | `ctx` is a global static variable. The backend cannot compile more than one program without explicit reset. |
| **HIGH** | Only `main` function is emitted. `emit_x86_64_program` searches for `main` by name and skips all others. |
| **HIGH** | BINOP only handles `+`, `-`, `*`. Division (`/`), modulo (`%`), subtraction, and all comparison/bitwise/logical operators fall through to the default case which just copies RCX to RAX. |
| **HIGH** | BINOP assumes left operand is in RAX and right operand is in `src2` or copied from RAX. This destroys the left operand before computing — the actual left operand value is lost. |
| **HIGH** | No register allocation. Everything goes through RAX/RCX. Nested expressions will clobber values. |
| **HIGH** | No stack frame management. The prologue pushes rbp and sets up frame pointer, but no space is allocated for locals. |
| **HIGH** | JMPZ/JMPNZ use `cmp rax, rax` (which always sets ZF=1) instead of comparing the actual condition value. This means conditional branches always jump. |
| **MEDIUM** | `emit_mov_r64_imm64` only emits 32-bit immediate (sign-extended) — cannot load full 64-bit immediates. |
| **MEDIUM** | No function call ABI implementation (System V AMD64 calling convention: RDI, RSI, RDX, RCX, R8, R9 for args). |
| **MEDIUM** | `_start` entry point embeds `call +0` (main at offset 5) hardcoded. If `main` is not at offset 5, the call goes to the wrong address. |
| **MEDIUM** | `emit_prologue` uses `push rbp` / `mov rbp, rsp` but `emit_epilogue` reverses the registers (`mov rsp, rbp` / `pop rbp`). Wait — the prologue uses `0xFB` (rbp) and `0xC3` (rsp). The epilogue does `mov rsp, rbp` then `pop rbp`. This is correct. |
| **LOW** | `x86_64_emit_prologue` and `x86_64_emit_epilogue` are no-ops (pass-through functions that don't call the actual emit functions). |
| **LOW** | `x86_64_start_function` and `x86_64_end_function` are no-ops. |

### Recommendations
- **REWRITE** the code emitter entirely. The current implementation is too broken to patch:
  1. Implement proper register allocation (even a simple linear scan).
  2. Implement stack frame management (allocate locals, access via `[rbp - offset]`).
  3. Implement System V AMD64 calling convention.
  4. Implement all IR instruction types (LOAD, STORE, LOAD_ADDR, CALL, PARAM).
  5. Fix BINOP to preserve operands correctly.
  6. Fix JMPZ/JMPNZ to actually check the condition value.
  7. Support all binary operators (div, mod, comparisons, bitwise, logical).
  8. Emit all functions, not just `main`.
  9. Eliminate global `ctx` state — use per-backend context.

---

## 8. ELF Writer (`src/elf.[ch]`)

### What it does
- Writes a valid ELF64 executable file
- Creates program headers for code (RX) and rodata (R) segments
- Page-aligns segments
- Writes code section followed by zero-padded gap, then rodata section

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **HIGH** | No `.bss` section. Global variables with no initializer cannot be supported. |
| **HIGH** | No `.data` section (writable data). Global variables with initializers cannot be supported. |
| **HIGH** | No symbol table. The binary cannot be debugged with `gdb` or inspected with `readelf`. |
| **HIGH** | No dynamic linking support. Cannot link against libc or shared libraries. |
| **MEDIUM** | Entry point is hardcoded to `0x400000`. If the ELF is loaded at a different address, it will crash. |
| **MEDIUM** | No section headers (e_shoff=0, e_shnum=0). `objdump` and similar tools won't work well. |
| **LOW** | `sym_name` parameter is accepted but unused (`(void)sym_name`). |

### Recommendations
- **EXTEND** with `.data` and `.bss` segments.
- **EXTEND** with symbol table and string table sections.
- **EXTEND** with section headers for tool compatibility.
- **EXTEND** with dynamic linking support (`.dynamic`, `.dynsym`, `.dynstr`, `.interp`).

---

## 9. Main (`src/main.c`)

### What it does
- Parses command-line arguments (`-o`, `-S`, `-c`, `-h`)
- Reads source file into memory
- Runs the compiler pipeline: lexer → parser → sema → IR → optimize → backend → ELF
- Cleans up memory

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **MEDIUM** | `-S` flag is accepted but never used. Assembly emission is a stub. |
| **MEDIUM** | `-c` flag is accepted but has no effect. |
| **LOW** | No error handling around pipeline stages (if parser aborts, memory leaks). |
| **LOW** | `parser_free` is a no-op, so no cleanup happens anyway. |

### Recommendations
- **EXTEND** with `-S` assembly output mode.
- **EXTEND** with `-c` object file output mode.
- **FIX** error handling: use return codes instead of `abort()`.

---

## 10. Standard Library (`lib/libc_minimal.c`)

### What it does
- Provides `exit()` via syscall 60
- Provides `write()` via syscall 1
- Provides a stub `printf()` that handles format specifiers but always outputs placeholder values

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **CRITICAL** | `printf` is a stub: `%d` outputs "0", `%s` outputs "", `%c` outputs '\0'. No variadic argument handling. |
| **HIGH** | `parse_int` references `buf` which is not in scope (it references the `buf` parameter of `write`, not the `s` parameter). This is a logic bug. |
| **HIGH** | This file is **not compiled into the tc output**. It's a reference file. The compiler must inline syscall code for `printf`/`write`/`exit`, or link against a separately compiled libc. Currently, neither happens. |
| **MEDIUM** | No `malloc`, `free`, `memcpy`, `memset`, `strcmp`, `strcpy`, `strlen`, etc. |

### Recommendations
- **FIX** `parse_int` bug.
- **IMPLEMENT** proper variadic `printf` (or at least handle one argument).
- **DECIDE**: Either (a) compile libc_minimal.c separately and link, or (b) emit builtin syscall wrappers from the compiler. Currently neither path works.
- **EXTEND** with essential library functions.

---

## 11. Build System (`Makefile`)

### What it does
- Standard gcc build with `-Wall -Wextra -std=c99 -O2`
- Wildcard source discovery
- Object file directory separation
- `make test` target

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **MEDIUM** | `libc_minimal.c` is not built or linked. It's effectively dead code. |
| **LOW** | No install target. |
| **LOW** | No dependency tracking (headers not tracked). |

### Recommendations
- **EXTEND** to compile and link libc_minimal.c (if linking approach is chosen).
- **EXTEND** with dependency generation (`-MMD -MP`).

---

## 12. Test Suite (`test/run_tests.sh`, test cases)

### What it does
- Iterates over `.c` files in `test/cases/`
- Compiles each with `tc`, runs the output, compares against expected output
- Reports pass/fail

### Bugs & Issues

| Severity | Issue |
|----------|-------|
| **HIGH** | Only 2 test cases: `hello.c` (`return 0;`) and `expr1.c` (`int x = 2 + 3; return x;`). |
| **HIGH** | Expected output files are empty (0 bytes). Tests pass trivially. |
| **MEDIUM** | Test suppresses stderr (`2>/dev/null`), hiding compiler errors. |
| **LOW** | No test for function calls, loops, strings, or any non-trivial feature. |

### Recommendations
- **EXTEND** with comprehensive test cases covering every supported feature.
- **FIX** expected output files.
- **EXTEND** with compile-fail tests (should reject invalid code).

---

## Gap Analysis: Current vs Working ANSI C Subset

### What a "working ANSI C subset compiler" needs:
1. ✅ Lexing basic tokens
2. ⚠️ Parsing declarations, statements, expressions (parser exists but has critical bugs)
3. ❌ Semantic analysis (symbol table, type checking) — **MISSING**
4. ⚠️ IR generation (partial — identifiers, calls, alloc, store, load are broken)
5. ⚠️ Optimization (basic constant folding only)
6. ❌ Code generation for variables (alloc/store/load) — **MISSING**
7. ❌ Code generation for function calls — **MISSING**
8. ❌ Code generation for all operators — **INCOMPLETE**
9. ⚠️ ELF output (works for trivial code only)
10. ❌ Standard library linkage — **MISSING**

### Specific missing capabilities:

| Feature | Status |
|---------|--------|
| Compile `int main() { return 0; }` | ⚠️ May work (barely) |
| Compile `int main() { int x = 5; return x; }` | ❌ FAILS (no alloc/load/store) |
| Compile `int main() { return 2 + 3; }` | ⚠️ Partially (constant folding may help, but BINOP is broken) |
| Function calls (including `printf`) | ❌ FAILS (no call codegen) |
| String literals in output | ❌ FAILS (printf stub, no call support) |
| Loops with variables | ❌ FAILS (no variable support) |
| Arrays | ❌ FAILS (no array indexing codegen) |
| Pointers | ❌ FAILS (no address-of or dereference codegen) |
| Structs | ❌ FAILS (not parsed, not codegen'd) |
| Global variables | ❌ FAILS (no global codegen, no .data/.bss) |
| Multiple functions | ❌ FAILS (only main emitted) |

---

## Priority Recommendations: What to Rewrite vs Extend

### REWRITE (Foundational — must be redone)

1. **`src/x86_64.c`** — The code emitter is fundamentally broken. Register allocation, stack frame management, and all variable-related IR instructions are no-ops. The BINOP implementation is incorrect. This needs a complete rewrite with:
   - Proper register allocator (linear scan or even color-based)
   - Stack frame management for locals and parameters
   - System V AMD64 calling convention
   - Complete instruction set coverage
   - Per-backend context (no globals)

2. **`src/sema.[ch]`** — The semantic analyzer is a stub that checks one thing. Needs complete rewrite with:
   - Hash-map-based symbol table
   - Scope nesting (block scope, function scope, global scope)
   - Type inference and checking
   - Function signature validation
   - Variable declaration/usage tracking

3. **`src/lexer.c`** — The `=` → `TOK_AND` conflation is a design flaw that infects the parser. The token enum needs restructuring. The static string buffer is a memory bug.

### FIX (Targeted bug fixes)

4. **`src/ir.c`** — Fix `NODE_IDENT` to emit `IR_LOAD`. Fix `NODE_BREAK`/`NODE_CONTINUE` to emit jumps. Fix slot/label scoping. Fix `append_inst` (or remove it).

5. **`src/parser.c`** — Fix all `TOK_AND` → assignment references. Fix local declaration node type.

### EXTEND (Add missing features)

6. **`src/ast.h`** — Add missing node types (do-while, switch, ternary, goto, struct def, enum).

7. **`src/parser.c`** — Add parsing for do-while, switch/case/default, struct, enum, typedef, function prototypes, extern declarations.

8. **`src/opt.c`** — Add proper constant propagation, CSE, and real DCE.

9. **`src/elf.c`** — Add .data, .bss, symbol table, section headers.

10. **`lib/libc_minimal.c`** — Implement proper printf with variadic args. Add essential library functions.

11. **`test/`** — Add comprehensive test cases.

---

## Recommended Implementation Order

```
Phase 1: Foundation (Make trivial programs work correctly)
  1. Fix lexer token collision (= vs &)
  2. Fix parser assignment detection
  3. Rewrite x86_64.c: stack frame, alloc, load, store
  4. Fix IR: identifier → LOAD, break/continue → JMP
  5. Verify: `int main() { int x = 2 + 3; return x; }` works

Phase 2: Variables and Control Flow
  6. Rewrite x86_64.c: complete BINOP, comparisons, logical ops
  7. Fix JMPZ/JMPNZ to check actual condition
  8. Rewrite sema: symbol table, scope, type checking
  9. Verify: loops, conditionals, multiple variables work

Phase 3: Functions and Calls
  10. Rewrite x86_64.c: System V ABI, function calls, parameters
  11. Fix IR: generate IR_PARAM, IR_CALL properly
  12. ELF: add .data/.bss for global variables
  13. Verify: function calls, global variables work

Phase 4: Library and Ecosystem
  14. Build and link libc_minimal.c
  15. Implement printf properly
  16. Add test cases, fix expected outputs
  17. Verify: `printf("hello\n")` works

Phase 5: Extended C Subset
  18. Add do-while, switch/case, ternary, goto
  19. Add struct, enum, typedef
  20. Add pointer arithmetic, array indexing
  21. Add more library functions
  22. ELF: symbol table, section headers, dynamic linking
```

---

*End of Audit Report*
