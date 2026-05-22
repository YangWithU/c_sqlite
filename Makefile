CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g -O2 -I. -Ithird_party/xtest
LDFLAGS =

# Source directories
SRC_DIRS = src/common src/tools src/parser src/storage src/buffer src/index src/catalog src/planner src/executor src/transaction
SRCS     = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJS     = $(SRCS:.c=.o)

# xtest test framework
XTEST_DIR  = third_party/xtest
TEST_SRCS  = $(XTEST_DIR)/xtest_runner.c $(wildcard tests/test_*.c)
TEST_OBJS  = $(TEST_SRCS:.c=.o) $(OBJS)

# Targets
.PHONY: all clean debug test test-debug test-asan test-leak test-cov

all: libminisqlite.a minisqlite

# Debug build for REPL
debug: CFLAGS = -std=c11 -Wall -Wextra -g3 -O0 -I. -Ithird_party/xtest -DDEBUG
debug: clean minisqlite

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Static library
libminisqlite.a: $(OBJS)
	ar rcs $@ $^

# Main executable
minisqlite: src/main.c libminisqlite.a
	$(CC) $(CFLAGS) $< -L. -lminisqlite -o $@

# Test runner
xtest_runner: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) -lm

# Run tests
test: xtest_runner
	./xtest_runner --parallel=0 --verbose

test-debug: CFLAGS = -std=c11 -Wall -Wextra -g3 -O0 -I. -Ithird_party/xtest -DDEBUG
test-debug: clean xtest_runner
	./xtest_runner --parallel=0 --verbose

test-asan: CFLAGS += -fsanitize=address -DDEBUG_MEM
test-asan: clean xtest_runner
	./xtest_runner --no-fork

test-leak: CFLAGS += -fsanitize=address -DDEBUG_MEM
test-leak: clean xtest_runner
	ASAN_OPTIONS=detect_leaks=1 ./xtest_runner --no-fork

test-cov: CFLAGS += --coverage
test-cov: clean xtest_runner
	./xtest_runner
	gcov src/*.c src/*/*.c

clean:
	rm -f $(OBJS) $(TEST_OBJS) libminisqlite.a xtest_runner
	find . -name '*.gcda' -o -name '*.gcno' | xargs rm -f
