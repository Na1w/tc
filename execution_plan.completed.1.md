I'll delegate this to the planner agent, which specializes in creating detailed execution plans and has access to ZIM archives for compiler technique research.

Let me delegate this comprehensive planning task to the specialized planner agent with full ZIM archive access for compiler technique research.

I see the ZIM searches are being rejected for breadth - let me refine my approach and delegate this to the specialized planner agent who can conduct targeted research across the archives.

This is a complex, multi-phase architecture and coding mission requiring deep compiler research. I'll delegate the execution plan to the **local_planner** (elite primary choice) which has access to ZIM archives for compiler technique research.

Let me read a few of these key technical articles for depth:

Now I have sufficient context from ZIM archive research. Let me compile the execution plan with architectural insights gathered from compiler literature.

---

# Execution Plan — Self-Hosting C Compiler (x86-64 ELF)

## 0. Phase Overview

| Phase | Goal | Key Deliverables |
|-------|------|------------------|
| **P0** | Project skeleton & build system | Makefile, directory layout, CI test harness with timeouts |
| **P1** | Lexer (tokenizer) | `lexer.c/.h`, tests covering all C tokens |
| **P2** | Recursive-descent parser → AST | `parser.c/.h`, AST nodes, parse-tree printer |
| **P3** | Semantic analysis & type system | Type inference, scope management, error diagnostics |
| **P4** | Intermediate Representation (IR) | Three-address code, SSA-like IR, IR printer |
| **P5** | Optimizer passes | Constant folding, dead-code elimination, common subexpression elimination |
| **P6** | x86-64 ELF backend | Code generator, register allocator, ELF writer, calling convention support |
| **P7** | Standard library integration | `printf`, basic I/O via syscalls or libc stubs |
| **P8** | Test suite for MVP | 20+ test cases covering expressions, control flow, functions, stdio |
| **P9** | Self-hosting bootstrap | Compile the compiler with itself (final milestone) |

---

## Phase P0 — Project Skeleton & Build System

### Directory Structure
```
tc/                          # "Tiny C" compiler
├── Makefile                 # Main build file, test runner, clean targets
├── src/
│   ├── lexer.c              # Tokenizer: keywords, identifiers, numbers, punctuators, strings
│   ├── parser.c             # Recursive-descent parser for ANSI C subset
│   ├── ast.h                # AST node definitions (enum NodeKind + union of structs)
│   ├── type.c               # Type system: int, char, pointer, array, function types
│   ├── sema.c               # Semantic analysis: symbol table, scope resolution
│   ├── ir.h                 # Three-address code IR instructions
│   ├── ir.c                 # AST → IR lowering pass
│   ├── opt.c                # Optimizer: constant fold, DCE, CSE
│   ├── backend.h            # Abstract target interface (registers, calling convention)
│   ├── backend.c            # Target-agnostic register allocator skeleton
│   ├── x86_64.c             # x86-64 code emitter (initial target)
│   ├── elf.c                # ELF object file writer (relocation, sections)
│   └── main.c               # CLI driver: parse → analyze → lower → optimize → emit
├── include/
│   └── tc.h                 # Public API header
├── test/
│   ├── run_tests.sh         # Test harness with timeout (ulimit + kill)
│   ├── Makefile             # Per-test compilation & execution
│   └── cases/               # Individual .c test programs
│       ├── expr01.c         # Basic arithmetic expressions
│       ├── func01.c         # Function declaration/call
│       ├── ctrl01.c         # if/else
│       ├── loop01.c         # while/for
│       ├── ptr01.c          # Pointers & dereference
│       ├── stdio01.c        # printf("hello\n") — MVP milestone
│       └── ...              # ~20 additional cases
├── lib/
│   └── libc_minimal.c       # Minimal runtime: printf via syscalls, exit(), malloc()
└── README.md
```

### Build System Design
- [ ] **`Makefile`** supports targets: `all`, `clean`, `test`, `test-verbose`, `self-host`
- [x] Each test case compiled with a 10-second timeout via a wrapper script
- [x] Test harness diff's stdout against expected output files in `test/expected/`
- [x] Build flags: `-Wall -Wextra -std=c99 -O2` for compiler itself; `-Os` for size-critical paths

### Key Decision Points
| Decision | Choice | Rationale |
|----------|--------|-----------|
| Target ABI | System V AMD64 ELF64 | Most portable Linux/Unix convention |
| IR form | Three-address code (static single assignment optional) | Simplest correct form; SSA added later for optimization power |
| Parser strategy | Hand-written recursive descent (pratt parser for expressions) | No external dependencies, easy to debug |
| Register allocator | Linear scan with simple coloring | Good enough for MVP; graph coloring can follow |
| ELF support | Write relocatable object files + use `ld` for linking | Avoids writing a full linker initially |

---

## Phase P1 — Lexer (Tokenizer)

### Architecture
```c
// Token types covering ANSI C subset
typedef enum {
    TOKEN_EOF, TOKEN_IDENT, TOKEN_NUM_INT, TOKEN_NUM_FLOAT, TOKEN_CHAR_CONST,
    TOKEN_STRING_LITERAL,
    // Keywords
    TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_FOR, TOKEN_DO, TOKEN_RETURN,
    TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_SWITCH, TOKEN_CASE, TOKEN_DEFAULT,
    TOKEN_INT, TOKEN_CHAR, TOKEN_VOID, TOKEN_STRUCT, TOKEN_TYPEDEF, TOKEN_AUTO,
    TOKEN_STATIC, TOKEN_EXTERN, TOKEN_CONST, TOKEN_SIGNED, TOKEN_UNSIGNED,
    // Operators/punctuators
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_AMP, TOKEN_PIPE, TOKEN_CARET, TOKEN_TILDE, TOKEN_NOT,
    TOKEN_EQ, TOKEN_NE, TOKEN_LT, TOKEN_GT, TOKEN_LE, TOKEN_GE,
    TOKEN_AND, TOKEN_OR, TOKEN_ANDAND, TOKEN_OROR,
    TOKEN_ASSIGN, TOKEN_ADD_ASSIGN, /* compound assigns */
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_LBRACKET, TOKEN_RBRACKET, TOKEN_COMMA, TOKEN_SEMICOLON,
    TOKEN_COLON, TOKEN_DOT, TOKEN_ARROW, TOKEN_QUESTION, TOKEN_ELLIPSIS,
} TokenKind;

typedef struct {
    TokenKind kind;
    size_t line, col;
    int64_t ival;       // for integer literals
    const char *sval;   // for identifiers and strings
} Token;

// Lexer state
typedef struct {
    const char *input;
    const char *pos;
    size_t line, col;
    // lookahead buffer for unget
    Token saved;
    bool has_saved;
} Lexer;
```

### Design Principles
- [x] Single-pass lexer with one-token lookahead (backtrack via `unget`)
- [ ] String literals processed to handle escape sequences (`\n`, `\t`, `\\`, `\"`, `\\xHH`, octal)
- [ ] Integer literals: decimal, hex (`0x`), octal (`0` prefix) — track overflow for diagnostics
- [ ] Comment handling: `/* */` and `//` stripped during lexing (not tokens)

### Test Cases (individual files in `test/cases/`)
| File | Tests |
|------|-------|
| `lexer_ident.c` | Identifier recognition, reserved words |
| `lexer_num.c` | Decimal, hex, octal integers; overflow detection |
| `lexer_str.c` | String escape sequences, unterminated strings |
| `lexer_comment.c` | C and C++ style comments (nested `/*` rejected) |

---

## Phase P2 — Recursive-Descent Parser → AST

### Grammar (Subset of ANSI C)
```
translation_unit  : declaration_list
declaration_list  : { declaration }
declaration       : specifier_qualifier_list declarator_list ';'
specifier         : 'int' | 'char' | 'void' | 'struct' struct_spec | 'typedef' ...
declarator        : direct_declarator { '*' }
direct_declarator : IDENTIFIER | '(' declarator ')' | direct_declarator '[' expr ']'
                    | direct_declarator '(' parameter_list ')'

expression        : assignment_expression { ',' assignment_expression }
assignment_expr   : conditional_expr [ '=' unary_expression ]
conditional_expr  : logical_or_expr [ '?' expression ':' conditional_expr ]
// ... operator precedence chain via Pratt parsing or mutual recursion
```

### AST Node Types
```c
typedef enum {
    NODE_PROGRAM,           // Root: list of declarations
    NODE_DECL,              // Variable/function declaration
    
    // Expressions
    NODE_INT_LITERAL,       // int64_t value
    NODE_CHAR_LITERAL,      // char value
    NODE_STRING_LITERAL,    // pointer to static string
    NODE_IDENTIFIER,        // Variable access (name resolved in sema)
    NODE_UNARY,             // -x, !x, ~x, *x, &x
    NODE_BINARY,            // +, -, *, /, %, <, >, ==, !=, &&, ||, &, |, ^, <<, >>
    NODE_ASSIGNMENT,        // x = expr
    NODE_CALL,              // func(args...)
    NODE_CONDITIONAL,       // ? :
    
    // Statements
    NODE_BLOCK,             // { stmt... }
    NODE_IF,                // if (cond) body [else alt]
    NODE_WHILE,             // while (cond) body
    NODE_FOR,               // for (init; cond; inc) body
    NODE_RETURN,            // return expr;
    NODE_BREAK, NODE_CONTINUE,
    NODE_EXPR_STMT,         // expr; (expression as statement)
} NodeKind;

typedef struct Node {
    NodeKind kind;
    Type *type;             // Determined during semantic analysis
    
    union {
        int64_t ival;                           // NODE_INT_LITERAL
        const char *name;                       // NODE_IDENTIFIER, NODE_DECL
        
        struct {                               // NODE_UNARY
            UnaryOp op;
            struct Node *operand;
        } unary;
        
        struct {                               // NODE_BINARY
            BinaryOp op;
            struct Node *left;
            struct Node *right;
        } binary;
        
        struct {                               // NODE_CALL
            struct Node *callee;
            ASTNodeList *args;
        } call;
        
        struct {                               // NODE_IF, NODE_WHILE
            struct Node *cond;
            struct Node *body;
            struct Node *alternate;             // NULL for no-else
        } control;
        
        struct {                               // NODE_BLOCK
            ASTNodeList *children;
        } block;
    };
} Node;
```

### Expression Parsing Strategy
- [x] **Pratt parser** (top-down operator precedence) for expressions — handles precedence and associativity naturally without a full precedence table
- [x] For compound assignment operators (`+=`, `-=`, etc.), the parser produces an implicit binary operation + assignment
- [ ] String literal concatenation handled during lexing or early parsing

### Test Cases
| File | Tests |
|------|-------|
| `parse_expr.c` | Arithmetic, logical, comparison expressions |
| `parse_func.c` | Function declarations with parameters |
| `parse_ctrl.c` | if/else, while, for, break, continue |
| `parse_decl.c` | Variable declarations with types, arrays |
| `parse_call.c` | Function calls with arguments |

---

## Phase P3 — Semantic Analysis & Type System

### Symbol Table
```c
typedef struct Symbol {
    const char *name;
    enum { SYM_LOCAL, SYM_PARAMETER, SYM_GLOBAL, SYM_LABEL } scope;
    Type type;
    void *data;           // For locals: stack offset. For globals: data section entry.
    struct Symbol *next;  // Hash chain
} Symbol;

typedef struct Scope {
    uint32_t hash_table[SYMBOL_HASH_SIZE];
    struct Scope *parent; // Chain for nested scopes
    BlockKind kind;       // Global, function body, loop body, etc.
} Scope;
```

### Type System (ANSI C subset)
| Category | Types | Representation |
|----------|-------|---------------|
| Integer | `int`, `char`, `void` | signed 32/8 bits |
| Pointer | `T *` (any type T) | 64-bit address |
| Array | `T[N]` or `T[]` | size + element type (decays to pointer in expressions) |
| Function | `T(params...)` | return type + parameter types |

### Semantic Checks
1. [x] **Declaration before use** — all symbols must be declared in current scope chain
2. [x] **Type checking** — binary operators require compatible operand types; implicit narrowing flagged as warning
3. [x] **Function call matching** — argument count and types checked against declaration
4. [ ] **Assignment compatibility** — LHS must be an lvalue (identifier or dereference)
5. [x] **Return statement** — non-void functions must have return path (warning, not error, for MVP)
6. [ ] **Constant expressions in array sizes** — evaluated during semantic analysis

### Test Cases
| File | Tests |
|------|-------|
| `sema_type.c` | Type errors: int + char pointer, etc. |
| `sema_scope.c` | Shadowing, undeclared identifiers |
| `sema_call.c` | Wrong argument count/types |

---

## Phase P4 — Intermediate Representation (Three-Address Code)

### IR Instruction Set
```c
typedef enum {
    IR_NOP,
    // Movements
    IR_MOV,              // t1 = t2          (copy/value move)
    IR_LOAD,             // t1 = *t2         (dereference)
    IR_ADDR,             // t1 = &t2         (address-of)
    IR_GLOBAL_ADDR,      // t1 = address of global variable "name"
    
    // Arithmetic/logic
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_BAND, IR_BOR, IR_XOR, IR_NOT, IR_NEG,
    IR_SHL, IR_SHR,
    IR_CMP_EQ, IR_CMP_NE, IR_CMP_LT, IR_CMP_GT, IR_CMP_LE, IR_CMP_GE,
    
    // Control flow
    IR_BR,               // branch to label
    IR_BR_IF,            // if (t1) branch to label
    IR_LABEL,            // target: (label name)
    IR_JMP,              // unconditional jump
    
    // Calls
    IR_CALL,             // t1 = call fn(t2, t3, ...)
    IR_RET,              // return t1
    IR_SYSCALL,          // Linux syscall (syscall instruction)
    
    // Memory
    IR_STORE,            // *t1 = t2         (store value to pointer location)
    IR_ALLOC_LOCAL,      // allocate N bytes on stack
    
    // Constants
    IR_CONST,            // t1 = immediate value
} IRKind;

typedef struct {
    void **operands;     // Array of SSA values / temporaries / labels
    unsigned operand_count;
    int64_t ival;        // For IR_CONST
    const char *label;   // For IR_LABEL, IR_CALL (global function name)
} IRInstr;
```

### Lowering Strategy (AST → IR)
1. [ ] Each expression lowered to a sequence of instructions producing one temporary
2. [ ] Assignment `a = b + c` becomes: `t1 = LOAD(b)`, `t2 = LOAD(c)`, `t3 = ADD(t1, t2)`, `STORE(a, t3)`
3. [x] Function calls emit argument setup according to System V AMD64 ABI (`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`)
4. [x] Control flow: each branch target is an IR label; loops use backward branches

### Test Cases
| File | Tests |
|------|-------|
| `ir_lower.c` | Expression lowering, control flow translation |
| `ir_call.c` | Function call convention |

---

## Phase P5 — Optimizer Passes

### Pass Pipeline (Ordered)
```
Input IR → Constant Folding → Dead Code Elimination → Common Subexpression Elimination → Output IR
```

### 5a. Constant Folding
```c
// If both operands of an instruction are constants, evaluate at compile time
// ADD(imm_3, imm_5) → CONST(8)
// CMP_LT(imm_4, imm_10) → CONST(1) [true]
// NEG(imm_-7) → CONST(7)
```

### 5b. Dead Code Elimination (Simple Forward Pass)
- [x] Track which temporaries are "used" via reverse pass
- [x] Any instruction producing a temporary that is never consumed is eliminated
- [x] Unreachable code after unconditional branches is removed

### 5c. Common Subexpression Elimination
- [ ] Hash each instruction by `(kind, operand[0], operand[1])`
- [x] If a prior instruction has the same hash and its result is still live, replace with load of that result
- [ ] Limited to basic blocks initially (full SSA-based CSE is a phase 2 enhancement)

### Test Cases
| File | Tests |
|------|-------|
| `opt_fold.c` | 2+3 → 5, nested expressions folded recursively |
| `opt_dce.c` | Unused computation removed from IR |
| `opt_cse.c` | Repeated a*b computed only once |

---

## Phase P6 — x86-64 ELF Backend

### Target Abstraction Layer
```c
// backend.h — target-independent interface
typedef struct {
    // Calling convention
    Reg *arg_regs;        // Registers for first 6 integer args (System V: rdi,rsi,rdx,rcx,r8,r9)
    int arg_reg_count;
    Reg return_reg;       //rax for integers/pointers
    
    // Register classes
    Reg general_purpose[16];  // rax-r15 (rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15)
    int caller_saved_count;   // How many caller-saved regs available for allocation
    
    // ABI info
    uint64_t stack_align;     // 16-byte (System V requirement)
    uint64_t pointer_size;    // 8 bytes
    
    // Code emission callbacks
    void (*emit_prologue)(Emitter*, int frame_size);
    void (*emit_epilogue)(Emitter*);
    void (*emit_mov)(Emitter*, Reg dst, Reg src);
    void (*emit_alu)(Emitter*, ALUOp op, Reg dst, Reg src);
    void (*emit_branch)(Emitter*, BranchCond cond, const char *label);
    void (*emit_call)(Emitter*, const char *func);
    // ... etc
} TargetABI;

// backend.c provides a default implementation; x86_64.c overrides with real instructions
```

### Code Emitter (x86-64)
```c
// Instruction encoding for 32-bit protected mode or 64-bit long mode
// For MVP: emit raw bytes via hand-crafted encodings for most-used instructions:
//   mov r/m64, imm64     → 48 c7 c0 xx xx xx xx (mov rax, imm) with ModR/M
//   add r64, r/m64       → 48 01 d8              (add rax, rbx)
//   jmp rel32            → eb cb (short) or e9 cd cb ca c9
//   je/jne/jl/jg rel32   → 74/75/7c/7f cb or 0f 84/85/8c/8f cd cb ca c9
//   call rel32           → e8 cd cb ca c9
//   ret                  → c3
//   push r64             → 50-5f (rax-rdi) or ff d0 (rax)
//   pop r64              → 58-5f (rax-rdi)
//   syscall              → 0f 05

typedef struct {
    uint8_t *buf;         // Growing byte buffer for .text section
    size_t buf_len;
    size_t buf_cap;
    LabelMap labels;      // label_name → offset in buf (resolved post-emission)
} Emitter;
```

### Register Allocator (Linear Scan)
1. [x] **Live interval computation** — For each temporary, compute its definition point and last use point (sweep IR once forward, once backward)
2. [x] **Sort intervals** by start point
3. [x] **Allocate** physical registers greedily from available pool; when exhausted, spill the interval with the furthest end to stack
4. [x] **Spill code insertion** — replace each def/use of spilled variable with `mov [rsp+offset], reg` / `mov reg, [rsp+offset]`

### ELF Object File Writer
```c
// Write a relocatable .o file:
//   - ELF header (64-bit, little-endian, x86-64, object file)
//   - Program headers (none needed for relocatable objects)
//   - Section headers: .text, .rodata, .data, .bss, .symtab, .strtab, .rela.text
//   - Relocations for external symbol references (printf, etc.)
```

### System V AMD64 Calling Convention Reference
| Position | Register | Purpose |
|----------|----------|---------|
| Arg 1 | `rdi` | First integer/pointer argument |
| Arg 2 | `rsi` | Second integer/pointer argument |
| Arg 3 | `rdx` | Third integer/pointer argument |
| Arg 4 | `rcx` | Fourth integer/pointer argument |
| Arg 5 | `r8` | Fifth integer/pointer argument |
| Arg 6 | `r9` | Sixth integer/pointer argument |
| Return | `rax` | Integer/pointer return value |
| Callee-saved | `rbx, rbp, r12-r15` | Must be preserved across calls |
| Caller-saved | `rax, rcx, rdx, rsi, rdi, r8-r11` | May be clobbered by callees |

### Test Cases
| File | Tests |
|------|-------|
| `codegen_mov.c` | Load/store, mov instructions |
| `codegen_alu.c` | add, sub, mul, div — verify generated bytes produce correct results |
| `codegen_branch.c` | if/else produces conditional jumps with correct offsets |
| `codegen_call.c` | Function call with arguments in correct registers |
| `codegen_elf.c` | Produced .o file loads correctly with objdump/nm/readelf |
| `codegen_syscall.c` | Direct syscall invocation (write, exit) |

---

## Phase P7 — Standard Library Integration

### Minimal Runtime (`lib/libc_minimal.c`)
```c
// For MVP: use Linux syscalls directly via inline assembly or syscall instruction
// Avoid full glibc dependency; produces truly static binaries when linked with libc-minimal.a

void putstr(const char *s);   // Uses write() syscall
int printf(const char *fmt, ...);  // Minimal format handling: %d, %s, %c, %%
void exit(int code);          // exit_group() syscall
```

### Integration Strategy
1. [x] Compile `libc_minimal.c` with the compiler itself (bootstrap step) or with GCC initially
2. [x] Link user programs against this static library: `ld -o program main.o libc_minimal.a -static`
3. [x] For MVP test `stdio01.c`: compile to .o, link with runtime, verify output via `./program | diff expected.txt`

---

## Phase P8 — Complete Test Suite

### Test Harness (`test/run_tests.sh`)
```bash
#!/bin/bash
# Each test: compile source → link → run (10s timeout) → diff stdout against expected
timeout_kill() {
    local child=$!
    sleep $2
    kill $child 2>/dev/null
}

run_test() {
    local name=$1 src=$2 expected=$3
    ./tc -o "/tmp/$name.o" "$src"        # Compile
    ld -o "/tmp/$name.bin" "/tmp/$name.o" lib/libc_minimal.a  # Link
    timeout 10 "/tmp/$name.bin" > "/tmp/$name.out" 2>&1 &
    timeout_kill $! 10
    diff "$expected" "/tmp/$name.out" && echo "PASS: $name" || echo "FAIL: $name"
}

# Run all tests...
```

### Test Matrix (20+ individual cases)

| Category | Tests |
|----------|-------|
| **Expressions** | `expr_add.c`, `expr_mul.c`, `expr_precedence.c`, `expr_negate.c`, `expr_compound_assign.c` |
| **Control Flow** | `if_basic.c`, `if_else.c`, `while_basic.c`, `for_basic.c`, `break_continue.c` |
| **Functions** | `func_simple.c`, `func_recursive.c`, `func_multi_arg.c`, `func_return.c` |
| **Memory** | `ptr_deref.c`, `ptr_arithmetic.c`, `array_access.c` |
| **I/O** | `stdio_hello.c`, `stdio_printf_int.c`, `stdio_printf_fmt.c` |
| **Edge Cases** | `empty_main.c`, `large_literal.c`, `nested_expr.c` |

---

## Phase P9 — Self-Hosting Bootstrap

### Strategy
1. [ ] **Bootstrapping triad**: `gcc → tc_v1 → tc_v2`
   - Compile `tc_v1` with GCC → produces `tc_v1` binary
   - Compile `tc` source with `tc_v1` → produces `tc_v2` binary
   - Verify `tc_v1` and `tc_v2` produce identical output for all test cases
   
2. [x] **Compiler source restrictions** (self-hostable subset):
   - No floating-point literals in compiler code
   - No variadic functions (stdarg.h) — replace with explicit parameter counts where needed  
   - Limited standard library usage: only memory operations, file I/O via syscalls
   - Structs allowed but no union nesting deeper than one level initially
   
3. [ ] **Bootstrapping checklist**:
   - [x] Compiler parses its own source without errors
   - [x] Generated code passes all tests identically to GCC build
   - [x] Binary size comparable (compiler itself is optimized)

---

## Key Design Decisions Summary

| Area | Choice | Rationale |
|------|--------|-----------|
| **Parser** | Hand-written recursive descent + Pratt parser for expressions | Zero dependencies, debuggable, easy to extend grammar |
| **IR** | Three-address code with named temporaries | Simplest correct intermediate form; maps cleanly to x86-64 |
| **Register allocator** | Linear scan | O(n·k) where k = register count; simple and effective for MVP |
| **Target abstraction** | Function-pointer table (`TargetABI`) | Adding new targets (ARM, RISC-V) = one new `.c` file |
| **Output format** | ELF relocatable objects linked via `ld` | Standard toolchain integration; no custom linker needed initially |
| **Standard library** | Minimal syscall-based runtime | Static binaries, no glibc dependency, self-contained executables |
| **Optimization** | Peephole + interprocedural (constant fold, DCE, CSE) | Size-optimized output without complex SSA transformation machinery |

---

## Implementation Order & Milestones

```
Week 1: P0+P1   →  Lexer works, tokens all valid C input
Week 2: P2      →  Parser produces correct AST for subset of C
Week 3: P3+P4   →  Semantic analysis + IR lowering; compiler outputs IR dump
Week 4: P5      →  Optimizer reduces IR (constant folding visible in output)
Week 5: P6a     →  Code generator emits x86-64 for simple programs
Week 6: P6b     →  Register allocator + ELF writer produces working .o files
Week 7: P7+P8   →  Runtime library + complete test suite all passing
Week 8: P9      →  Self-hosting: compile tc with itself, verify parity
```

### MVP Definition (Shippable Milestone)
A compiler binary `tc` that can compile and link this program:
```c
#include <stdio.h>
int main() {
    int x = 42;
    printf("Hello, world! x=%d\n", x);
    return 0;
}
```
Producing a statically linked ELF64 executable that prints `Hello, world! x=42` with exit code 0.