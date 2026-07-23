# TC Compiler — Dependencies

All dependencies identified from the [MASTER_AUDIT report](../MASTER_AUDIT.md) and project source analysis.

---

## Required Dependencies

| # | Package (apt) | Component(s) that need it | Purpose |
|---|---------------|--------------------------|---------|
| 1 | `gcc` | **All compiler source files** (`.c`/`.h`) | Primary C compiler for building the TC compiler itself |
| 2 | `clang` | **All compiler source files** (`.c`/`.h`) | Alternative C compiler; listed as required tool in MASTER_AUDIT |
| 3 | `make` | **Build system** (`Makefile`) | Build automation and target management |
| 4 | `binutils` | **Backend: ELF, Linker, Codegen** | Provides `ar`, `ld`, `readelf`, `objdump`, `nm`, `as` for linking, binary inspection, and assembly |
| 5 | `vim-common` (provides `xxd`) | **Backend: Codegen, ELF** | Hex dump utility for inspecting generated binaries and object files |
| 6 | `nasm` | **Backend: Codegen, Disassembly** | Reference assembler for verifying generated assembly output |
| 7 | `python3` | **Test framework** (`test/run_tests.sh`, test harness) | Required for test scripts and automated test execution |

## Optional Dependencies

| # | Package (apt) | Component(s) that need it | Purpose |
|---|---------------|--------------------------|---------|
| 8 | `libcapstone-dev` | **Backend: Disassembly** (`backend/disasm/`) | Capstone disassembly engine for high-quality disassembly output (detected via `pkg-config`) |
| 9 | `pkg-config` | **Build system** (`Makefile`) | Detects Capstone library presence and provides compile flags |
| 10 | `build-essential` | **All components** | Meta-package that pulls in `gcc`, `g++`, `make`, and related tools in one install |

---

## Dependency Matrix by Component

| Component | Required Dependencies | Optional Dependencies |
|-----------|----------------------|-----------------------|
| **Compiler Core** (Lexer, Parser, Sema, IR, Opt, AST) | gcc, clang, make | libcapstone-dev |
| **Backend: Codegen** (x86_64_emit, x86_64_instr) | gcc, clang, make, binutils, vim-common, nasm | libcapstone-dev |
| **Backend: ELF** (elf.c) | gcc, clang, make, binutils, vim-common | libcapstone-dev |
| **Backend: Linker** (linker.c) | gcc, clang, make, binutils | libcapstone-dev |
| **Backend: Disassembly** (disasm.c) | gcc, clang, make | libcapstone-dev |
| **Runtime Library** (libc, stdio, stdlib, syscall) | gcc, clang, make, binutils | — |
| **Test Framework** (run_tests.sh, unit/integration tests) | python3, bash, binutils | — |
| **Build System** (Makefile) | make, gcc, clang, pkg-config | build-essential |

---

## Install Scripts

### Full Install (Required + Optional)
```bash
sudo apt-get update
sudo apt-get install -y \
    gcc \
    clang \
    make \
    binutils \
    vim-common \
    nasm \
    python3 \
    libcapstone-dev \
    pkg-config
```

### Required-Only Install
```bash
sudo apt-get update
sudo apt-get install -y \
    gcc \
    clang \
    make \
    binutils \
    vim-common \
    nasm \
    python3
```

### Meta-Package Shortcut (Recommended)
Installs `gcc`, `g++`, `make`, and related tools via the `build-essential` meta-package, plus project-specific dependencies:
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    clang \
    binutils \
    vim-common \
    nasm \
    python3 \
    libcapstone-dev \
    pkg-config
```
