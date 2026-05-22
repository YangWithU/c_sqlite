---
name: xtest-integration
description: 将 xtest 集成到现有 C 项目并改造 Makefile 的标准流程。只要用户提到“接入测试框架”“新增单元测试”“改 Makefile 跑测试”“加 ASAN/TSAN/覆盖率”“把现有 C 项目接入 xtest”，都应优先使用本技能，即使用户没有明确说出 xtest。
compatibility:
  tools: [view, apply_patch, rg, glob, bash]
---

# xtest 集成技能（ZH-CN）

你要帮助其他大模型把本仓库的 `xtest` 测试能力接入到用户现有的 C 项目中，重点是：
1. 正确引入 `xtest.h` 与 `xtest_runner.c`
2. 正确改造 `Makefile`
3. 给出可直接执行的测试命令与最小示例
4. 尽量不破坏用户现有构建流程

---

## 何时触发

当用户出现以下意图时触发本技能：
1. 想在 C 项目里加测试框架
2. 想把测试命令接入 Makefile / CI
3. 想增加并行测试、超时、XML 报告、Sanitizer、覆盖率
4. 想从零写一个 `tests/test_*.c` 并跑起来

---

## 先收集的输入（不全就先询问）

1. 现有项目目录结构（是否已有 `tests/`）
2. 现有 `Makefile`（特别是 `CC/CFLAGS/LDFLAGS/all/clean`）
3. 是否已有同名目标（如 `test`, `clean`）
4. 是否需要保留当前测试框架并并存
5. CI 期望输出（是否需要 JUnit XML）

---

## 标准集成步骤

### 步骤 1：落位 xtest 文件

最小需要两个文件：
1. `xtest.h`
2. `xtest_runner.c`

推荐放置方式（二选一）：
1. 项目根目录（最简单）
2. `third_party/xtest/`（更清晰，适合已有工程）

> 若使用子目录，测试源码里的 `#include` 路径要同步调整。

---

### 步骤 2：新增测试目录与最小用例

创建 `tests/test_smoke.c`（示例）：

```c
#include "../xtest.h"

TEST(smoke, add) {
    EXPECT_EQ(1 + 1, 2);
}
```

如果 `xtest.h` 不在上级目录，请按实际路径改 `#include`。

---

### 步骤 3：改造 Makefile（核心）

优先复用用户已有变量，不要硬覆盖。若无相关定义，可采用以下基线模板：

```makefile
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
```

#### Makefile 改造规则

1. 若用户已有 `test` 目标，不要直接覆盖：可新增 `xtest` / `xtest-test` 目标并在 `help` 中说明。
2. 若用户已有 `BUILD_DIR`，复用它，避免重复产物路径。
3. 若用户已有统一 `LDFLAGS`，把 sanitizer/cov 链接选项追加进去。
4. 只在必要时引入 `lcov/genhtml`，否则标注为可选。

---

### 步骤 4：本地验证命令

```sh
make test
make test-asan
make test-parallel
./build/xtest_runner --help
./build/xtest_runner --list
```

需要 XML（CI 常见）时：

```sh
./build/xtest_runner --output=xml:test-report.xml
```

---

## 与现有工程并存的策略

1. **最小侵入**：优先新增目标，避免改动 `all` 的主产物逻辑。
2. **命名隔离**：用 `xtest_runner`、`xtest-test`，避免与旧脚本冲突。
3. **逐步迁移**：先接入 smoke test，再按模块扩展 `tests/test_*.c`。

---

## 常见问题与处理

1. `undefined reference`  
   - 检查 `xtest_runner.c` 是否进入链接输入（`TEST_SRCS`）。

2. 找不到测试  
   - 检查文件命名是否匹配 `tests/test_*.c`。

3. `#include "../xtest.h"` 报错  
   - 按实际目录修正 include 路径，或统一使用 `-I` 头文件搜索路径。

4. `lcov/genhtml` 不存在  
   - 保留 `test-cov` 为可选目标，或提示安装后再启用覆盖率报告。

5. 并行不生效  
   - 使用 `--parallel=0`（自动核数）或显式 `--parallel=N`。

---

## 输出要求（你给用户的最终结果）

执行本技能后，你的输出至少要包含：
1. 改动说明（改了哪些文件、为什么）
2. 可直接粘贴的 Makefile 片段或补丁
3. 最小测试样例
4. 一组可执行命令（构建、测试、并行、ASAN、XML）
5. 若信息不足，明确列出缺失项，不要臆造

---

## 禁止事项

1. 不要删除用户现有构建目标，除非用户明确要求。
2. 不要假设用户一定有 `lcov/genhtml`。
3. 不要在未确认路径的情况下硬写 `#include "../xtest.h"` 到所有文件。
4. 不要把调试参数（如 `--no-fork`）默认固化到常规 `make test` 中。
