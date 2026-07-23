CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -fno-stack-protector
OBJDIR  = obj
LIBDIR  = lib
BINDIR  = .
TARGET  = $(BINDIR)/tc
RUNTIME_LIB = $(LIBDIR)/libruntime.a

# -------------------------------------------------------------------
# Include paths for all subdirectories
# -------------------------------------------------------------------
INCDIR  = -Icompiler/lexer \
          -Icompiler/parser \
          -Icompiler/sema \
          -Icompiler/ir \
          -Icompiler/opt \
          -Icompiler/ast \
          -Ibackend/codegen \
          -Ibackend/elf \
          -Ibackend/linker \
          -Ibackend/disasm \
          -Ibackend/core

# -------------------------------------------------------------------
# Compiler source files (recursive wildcard via shell)
# -------------------------------------------------------------------
SRCS    = $(shell find compiler/ backend/ -name '*.c')
OBJS    = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

# Object files WITHOUT compiler/main.o — used for unit/integration tests
# (compiler/main.c defines main() which conflicts with test main())
TEST_OBJS = $(filter-out $(OBJDIR)/compiler/main.o,$(OBJS))

# Conditional Capstone support
HAS_CAPSTONE := $(shell pkg-config --exists capstone 2>/dev/null && echo yes)
ifeq ($(HAS_CAPSTONE),yes)
CFLAGS  += -DHAVE_CAPSTONE $(shell pkg-config --cflags capstone)
LDFLAGS += $(shell pkg-config --libs capstone)
endif

# -------------------------------------------------------------------
# Runtime library sources (modular files in runtime/)
# -------------------------------------------------------------------
RT_DIR     = runtime
RT_C_SRCS  = $(filter-out $(RT_DIR)/libc_minimal.c,$(wildcard $(RT_DIR)/*.c))
RT_C_OBJS  = $(patsubst $(RT_DIR)/%.c,$(LIBDIR)/%.o,$(RT_C_SRCS))
RT_ASM_OBJS = $(LIBDIR)/crt0.o $(LIBDIR)/syscall.o

# -------------------------------------------------------------------
# Test directories
# -------------------------------------------------------------------
TESTDIR     = tests
TEST_UNIT   = $(TESTDIR)/unit
TEST_INTEGR = $(TESTDIR)/integration
TEST_CASES  = $(TESTDIR)/cases
TEST_REGRESS = $(TESTDIR)/regression

# -------------------------------------------------------------------
# Test executables
# -------------------------------------------------------------------
# Unit test sources and targets
UNIT_SRCS   = $(wildcard $(TEST_UNIT)/*.c)
UNIT_TESTS  = $(patsubst $(TEST_UNIT)/%.c,$(TEST_UNIT)/%,$(UNIT_SRCS))

# Integration test sources and targets
INTEGR_SRCS = $(wildcard $(TEST_INTEGR)/*.c)
INTEGR_TESTS = $(patsubst $(TEST_INTEGR)/%.c,$(TEST_INTEGR)/%,$(INTEGR_SRCS))

# Cases test sources
CASES_SRCS  = $(wildcard $(TEST_CASES)/*.c)

# Test runner script
TEST_RUNNER = tests/run_all_tests.sh

# -------------------------------------------------------------------
# Phony targets
# -------------------------------------------------------------------
.PHONY: all clean test test-unit test-integration test-cases runtime hello

all: $(TARGET) runtime

# -------------------------------------------------------------------
# Compiler build rules
# -------------------------------------------------------------------
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCDIR) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# -------------------------------------------------------------------
# Runtime library rules
# -------------------------------------------------------------------

# Compile each modular runtime .c file separately (non-PIC for linker compat)
$(LIBDIR)/%.o: $(RT_DIR)/%.c
	$(CC) -Wall -Wextra -std=c99 -O2 -fPIE -fno-pic -fno-stack-protector -mcmodel=small -c $< -o $@

# Compile crt0.S (assembly startup)
$(LIBDIR)/crt0.o: $(RT_DIR)/crt0.S
	$(CC) -c $< -o $@

# Compile syscall.S (assembly syscall wrapper)
$(LIBDIR)/syscall.o: $(RT_DIR)/syscall.S
	$(CC) -c $< -o $@

# Archive all .o files into libruntime.a
$(RUNTIME_LIB): $(RT_C_OBJS) $(RT_ASM_OBJS)
	ar rcs $@ $^

runtime: $(RUNTIME_LIB)

# -------------------------------------------------------------------
# Hello target: compile test_hello_link.c with ./tc, linked with
# libruntime.a (tc automatically finds lib/libruntime.a)
# -------------------------------------------------------------------
hello: $(TARGET) $(RUNTIME_LIB)
	./tc $(TEST_CASES)/test_hello_link.c -o hello

# -------------------------------------------------------------------
# Unit test build rules
# Each unit test compiles as a standalone executable linked with
# the compiler's internal libraries (excluding compiler/main.o which
# defines main() and would conflict with the test's own main()).
# -------------------------------------------------------------------
$(TEST_UNIT)/%: $(TEST_UNIT)/%.c $(TEST_OBJS)
	$(CC) $(CFLAGS) $(INCDIR) -I. -o $@ $< $(TEST_OBJS) $(LDFLAGS)

test-unit: $(TARGET) $(UNIT_TESTS)
	@echo "========================================="
	@echo "  Running Unit Tests"
	@echo "========================================="
	@FAIL=0; PASS=0; \
	for t in $(UNIT_TESTS); do \
		echo "--- $$t ---"; \
		./$$t 2>&1 || true; \
		echo ""; \
	done

# -------------------------------------------------------------------
# Integration test build rules
# Integration tests also link against the compiler's internal objects
# (excluding main.o) since they test linker, ELF writer, etc. directly.
# -------------------------------------------------------------------
$(TEST_INTEGR)/%: $(TEST_INTEGR)/%.c $(TEST_OBJS)
	$(CC) $(CFLAGS) $(INCDIR) -I. -o $@ $< $(TEST_OBJS) $(LDFLAGS)

test-integration: $(TARGET) $(INTEGR_TESTS)
	@echo "========================================="
	@echo "  Running Integration Tests"
	@echo "========================================="
	@FAIL=0; PASS=0; \
	for t in $(INTEGR_TESTS); do \
		echo "--- $$t ---"; \
		./$$t 2>&1 || true; \
		echo ""; \
	done

# -------------------------------------------------------------------
# Cases test runner
# Uses the existing tc compiler + gcc reference comparison approach.
# -------------------------------------------------------------------
test-cases: $(TARGET) runtime
	@echo "========================================="
	@echo "  Running Cases Tests"
	@echo "========================================="
	@bash test/run_tests.sh

# -------------------------------------------------------------------
# Unified test target: runs ALL test categories
# -------------------------------------------------------------------
test: $(TARGET) runtime
	@bash $(TEST_RUNNER)

# -------------------------------------------------------------------
# Clean
# -------------------------------------------------------------------
clean:
	rm -rf $(OBJDIR) $(TARGET) $(TEST_CASES)/*.out $(TEST_CASES)/*.o
	rm -f $(LIBDIR)/*.o $(RUNTIME_LIB) hello
	rm -f *.o.tmp a.out
	rm -f $(UNIT_TESTS) $(INTEGR_TESTS)
	rm -f tests/cases/*.tc_out tests/cases/*.gcc_out
