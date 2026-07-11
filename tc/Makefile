CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2
SRCDIR  = src
OBJDIR  = obj
BINDIR  = .
TARGET  = $(BINDIR)/tc

SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

# Conditional Capstone support
HAS_CAPSTONE := $(shell pkg-config --exists capstone 2>/dev/null && echo yes)
ifeq ($(HAS_CAPSTONE),yes)
CFLAGS  += -DHAVE_CAPSTONE $(shell pkg-config --cflags capstone)
LDFLAGS += $(shell pkg-config --libs capstone)
endif

.PHONY: all test clean

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TARGET)
	@bash test/run_tests.sh

clean:
	rm -rf $(OBJDIR) $(TARGET) test/cases/*.out test/cases/*.o
