CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2
SRCDIR  = src
OBJDIR  = obj
BINDIR  = .
TARGET  = $(BINDIR)/tc

SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all test clean

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	@bash test/run_tests.sh

clean:
	rm -rf $(OBJDIR) $(TARGET) test/cases/*.out test/cases/*.o
