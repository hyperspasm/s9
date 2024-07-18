# Variables
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lSDL2 -lSDL2_ttf -lm -lfftw3 
SRCDIR = src
OBJDIR = obj
BINDIR = .
TARGET = $(BINDIR)/s9

# Source and object files
SOURCES := $(wildcard $(SRCDIR)/*.c)
OBJECTS := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Test variables
TEST_SRCDIR = tests
TEST_OBJDIR = test_obj
TEST_BINDIR = .
TEST_TARGET = $(TEST_BINDIR)/s9_tests
TEST_SOURCES := $(wildcard $(TEST_SRCDIR)/*.c)
TEST_OBJECTS := $(TEST_SOURCES:$(TEST_SRCDIR)/%.c=$(TEST_OBJDIR)/%.o)
TEST_LDFLAGS = -lcunit -lSDL2 -lSDL2_ttf -lm -lfftw3

# Rules
all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJECTS) $(OBJDIR)/ring_buffer.o $(OBJDIR)/s9_log.o
	@mkdir -p $(TEST_BINDIR)
	$(CC) $(TEST_OBJECTS) $(OBJDIR)/ring_buffer.o $(OBJDIR)/s9_log.o -o $@ $(TEST_LDFLAGS)

$(TEST_OBJDIR)/%.o: $(TEST_SRCDIR)/%.c
	@mkdir -p $(TEST_OBJDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)/s9 $(TEST_OBJDIR) $(TEST_BINDIR)/s9_tests

.PHONY: all clean test
