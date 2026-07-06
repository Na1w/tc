# tc — Tiny C Compiler

A minimal native C compiler targeting Linux x86-64 ELF executables. Written in ANSI C99 with no external dependencies.

## Architecture

```
Source .c → Lexer → Parser → AST → Semantic Analysis → IR → Optimization → x86-64 Backend → ELF Binary
```

| Stage | Files | Description |
|---|---|---|
| Lexer | `src/lexer.[ch]` | Tokenizer: keywords, identifiers, integers, strings, operators. Strips `//` and `/* */` comments. |
| Parser | `src/parser.[ch]` | Recursive-descent parser producing an AST. Supports a C subset (functions, control flow, expressions). |
| AST | `src/ast.h` | Node definitions for declarations, statements, expressions, types. |
| Semantic Analysis | `src/sema.[ch]` | Basic type checking (main return type, expression validity). |
| IR | `src/ir.[ch]` | Three-address code intermediate representation. |
| Optimizer | `src/opt.[ch]` | Constant folding and dead code elimination. |
| Backend | `src/backend.[ch]`, `src/x86_64.[ch]` | x86-64 code emitter using System V ABI. |
| ELF Writer | `src/elf.[ch]` | Writes valid ELF64 executable files. |
| Runtime | `lib/libc_minimal.c` | Minimal runtime (printf via syscalls, exit). |

## Building

```bash
make          # Build the tc binary
make test     # Run test suite (with 5s timeouts)
make clean    # Remove build artifacts
```

Compiler flags: `gcc -Wall -Wextra -std=c99 -O2`

## Usage

```bash
./tc -o output input.c
```

Options:
- `-o <file>` — Output file (default: `a.out`)
- `-S` — Emit assembly (stub)
- `-c` — Compile only (stub)

## Supported C Subset

- Types: `int`, `char`, `void`, pointers
- Control flow: `if/else`, `while`, `for`, `break`, `continue`, `return`
- Expressions: arithmetic (`+`, `-`, `*`, `/`, `%`), comparisons, logical ops
- Function calls, global/local variable declarations
- String and character literals

## License

Public domain.
