CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -pedantic
BUILD_DIR = build

TEST_SRCS = xtest_runner.c $(wildcard tests/test_*.c)

.PHONY: all test clean

all: $(BUILD_DIR)/xtest_runner

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/xtest_runner: $(TEST_SRCS) xtest.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

test: $(BUILD_DIR)/xtest_runner
	@./$(BUILD_DIR)/xtest_runner

clean:
	rm -rf $(BUILD_DIR)

# -------------------------------------------------------------------
#  Sanitizer / Coverage / Parallel targets
# -------------------------------------------------------------------
.PHONY: test-asan test-tsan test-leak test-cov test-parallel help

test-asan: CFLAGS += -fsanitize=address -g
test-asan: LDFLAGS += -fsanitize=address
test-asan: clean test
	@echo "ASAN tests passed"

test-tsan: CFLAGS += -fsanitize=thread -g
test-tsan: LDFLAGS += -fsanitize=thread
test-tsan: clean test
	@echo "TSAN tests passed"

test-leak: CFLAGS += -fsanitize=address -g
test-leak: LDFLAGS += -fsanitize=address
test-leak: clean test
	@echo "Leak check: set ASAN_OPTIONS=detect_leaks=1 when running"

test-cov: CFLAGS += --coverage -g -O0
test-cov: LDFLAGS += --coverage
test-cov: clean test
	lcov --capture --directory . --output-file coverage.info 2>/dev/null || true
	genhtml coverage.info --output-directory coverage 2>/dev/null || true
	@echo "Coverage report: coverage/index.html"

test-parallel: $(BUILD_DIR)/xtest_runner
	@./$(BUILD_DIR)/xtest_runner --parallel=0

help:
	@echo "xtest targets:"
	@echo "  make test           - Build and run tests"
	@echo "  make test-asan      - Run with AddressSanitizer"
	@echo "  make test-tsan      - Run with ThreadSanitizer"
	@echo "  make test-leak      - Run with LeakSanitizer"
	@echo "  make test-cov       - Generate coverage report"
	@echo "  make test-parallel  - Run tests in parallel"
	@echo "  make clean          - Remove build artifacts"
