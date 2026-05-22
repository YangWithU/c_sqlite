# MiniSQLite (C 版) - 详细执行步骤

> 每个 Step 包含：目标、输入、输出、验证标准、C 语言关键实现细节。

---

## Phase 0：基础设施搭建

### Step 0.1 — Makefile 项目骨架

**目标**：搭建可编译的 C11 项目结构。

**输入**：无。

**输出**：
```
c_sqlite/
├── Makefile                    # 顶层 Makefile (CC=gcc, CFLAGS=-std=c11 -Wall -Wextra -g -O2)
├── src/
│   ├── common/                 # 公共工具
│   │   ├── error.h             # 错误码定义 (enum + 错误信息函数)
│   │   ├── types.h             # 基础类型别名 (page_id_t, txn_id_t 等 typedef)
│   │   ├── config.h            # 全局配置常量 (PAGE_SIZE=4096 等宏)
│   │   ├── macros.h            # 通用宏 (MIN/MAX/ARRAY_SIZE/CONTAINER_OF 等)
│   │   └── platform.h          # 平台抽象 (字节序、编译器特性)
│   ├── storage/                # 存储引擎
│   ├── buffer/                 # Buffer Pool
│   ├── index/                  # B+ 树索引
│   ├── catalog/                # 系统目录
│   ├── parser/                 # SQL 解析器
│   ├── planner/                # 查询计划
│   ├── executor/               # 查询执行
│   ├── transaction/            # 事务管理
│   └── tools/                  # REPL、SqlRunner
├── third_party/                 # 第三方库
│   └── xtest/                   # xtest 测试框架
│       ├── xtest.h
│       └── xtest_runner.c
├── tests/
│   └── test_smoke.c             # 基础冒烟测试（后续逐步添加各模块测试）
└── sql/                        # 测试用 .sql 文件
```

**C 语言关键点**：
- 使用 `typedef` 定义类型别名，不用 `using`
- 头文件使用 `#pragma once` 或传统 include guard
- 函数声明在 .h，实现在 .c
- Makefile 使用模式规则：`%.o: %.c $(CC) $(CFLAGS) -c $< -o $@`

**验证**：`make` 成功，`make test` 运行 xtest 测试通过。

---

### Step 0.2 — 跨平台抽象层

**目标**：封装平台相关操作，使核心代码跨平台。

**输入**：Step 0.1。

**输出**：
- `src/common/file_io.h/c` — 文件读写封装
- `src/common/platform.h` — 字节序转换、平台宏

**file_io 接口**：
```c
typedef struct {
    FILE* fp;
    char file_name[256];
    uint32_t page_count;
    int32_t free_list_head;
} FileIO;

int  file_io_open(FileIO* io, const char* path, const char* mode);
void file_io_close(FileIO* io);
int  file_io_read_page(FileIO* io, page_id_t page_id, void* buf);
int  file_io_write_page(FileIO* io, page_id_t page_id, const void* buf);
void file_io_flush(FileIO* io);
```

**C 语言关键点**：
- 用 FILE* 而非文件描述符，跨平台兼容性好
- 字节序：检测系统端序，提供 `swap_le/be_16/32/64()` 宏
- 页面读写使用 `fread/fwrite` + `fseek`，每次 4KB

**验证**：创建临时文件、写入 4KB 页面、读回验证内容一致。

---

### Step 0.3 — 错误处理框架

**目标**：统一错误码 + 错误信息。

**输入**：Step 0.1。

**输出**：
- `src/common/error.h` — ErrorCode 枚举 + 错误描述函数
- `src/common/result.h` — Result 结构体（值或错误码）

**错误码定义**：
```c
typedef enum {
    DB_OK = 0,
    DB_IO_ERROR = 1001,
    DB_PAGE_NOT_FOUND,
    DB_BUFFER_POOL_FULL,
    DB_TXN_ABORTED = 2001,
    DB_DEADLOCK,
    DB_LOCK_CONFLICT,
    DB_SYNTAX_ERROR = 3001,
    DB_UNKNOWN_TABLE,
    DB_UNKNOWN_COLUMN,
    DB_DUPLICATE_KEY,
    DB_NULL_VIOLATION,
    DB_TYPE_MISMATCH,
    DB_OUT_OF_MEMORY = 4001,
    DB_INTERNAL_ERROR = 9001,
} ErrorCode;

const char* error_code_to_string(ErrorCode code);
```

**C 语言 Result 模式**：
```c
// 方案 A：出参 + 返回错误码（推荐，C 惯用法）
int tuple_get_value(const Tuple* tuple, column_id_t col, Value* out_value);

// 方案 B：通用 Result 包装
typedef struct {
    ErrorCode error;
    union {
        int64_t int_val;
        void*   ptr_val;
    };
} Result;
```

**取舍**：方案 A 更符合 C 语言习惯，被调用者返回错误码，结果通过出参返回。每个函数的返回值约定在注释中标明。

**验证**：测试错误码转字符串、出参模式正确性。

---

### Step 0.4 — 日志系统

**目标**：简易日志，便于开发调试。

**输入**：Step 0.1。

**输出**：
- `src/common/logger.h/c` — 日志宏 + 配置

**实现**：
```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

void logger_set_level(LogLevel level);
void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...);

#define LOG_DEBUG(fmt, ...) logger_log(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logger_log(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logger_log(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logger_log(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
```

**C 语言关键点**：
- 使用 `__VA_ARGS__` + `##` 处理可变参数宏（C99）
- 日志输出到 stderr，使用 `fprintf + va_list`
- 日志级别可运行时配置

**验证**：各级别日志正确输出，级别过滤生效。

---

### Step 0.5 — 集成 xtest 测试框架

**目标**：集成 xtest_c_testinglib 作为项目的单元测试框架。

**输入**：Step 0.1 + xtest_c_testinglib 库。

**输出**：
- 将 `xtest_c_testinglib/xtest.h` 和 `xtest_c_testinglib/xtest_runner.c` 复制到 `third_party/xtest/` 目录
- Makefile 中添加测试构建目标
- 创建 `tests/test_smoke.c` 作为第一个测试文件

**xtest 核心用法**：
```c
#include "xtest.h"

// 基本测试（自动注册，无需 main）
TEST(disk_manager, read_write_page) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(some_condition);
    ASSERT_NOT_NULL(some_ptr);  // 致命断言：失败后立即返回
}

// 带 Fixture 的测试（setup/teardown）
static void disk_setup(void) {
    // 初始化磁盘管理器
}
static void disk_teardown(void) {
    // 清理资源
}
TEST_DEFINE_FIXTURE(disk_fxt, disk_setup, disk_teardown);

TEST_F(disk_manager, allocate_page, disk_fxt) {
    // setup 已调用，teardown 会在测试结束后自动调用
    EXPECT_EQ(result, DB_OK);
}

// 预期失败的测试
FAIL_TEST(disk_manager, full_page_insert) {
    // 预期此测试会失败（测试错误路径）
    EXPECT_EQ(should_fail, 0);
}

// 禁用的测试（跳过执行）
DISABLED_TEST(disk_manager, concurrent_access) {
    // 暂不运行
}

// 基准测试
TEST_BENCH(b_plus_tree, insert_performance, 1000000) {
    // 性能基准，迭代 1000000 次
}

// 测试内日志
TEST(parser, basic_select) {
    TEST_LOG("parsing SELECT statement");
    EXPECT_STR_EQ(ast->type_name, "SELECT");
}
```

**xtest 断言宏一览**：
| 宏 | 类别 | 说明 |
|---|------|------|
| `EXPECT_EQ(a, b)` / `ASSERT_EQ(a, b)` | 整数相等 | 非致命 / 致命 |
| `EXPECT_NE(a, b)` / `ASSERT_NE(a, b)` | 整数不等 | |
| `EXPECT_TRUE(cond)` / `ASSERT_TRUE(cond)` | 布尔为真 | |
| `EXPECT_FALSE(cond)` / `ASSERT_FALSE(cond)` | 布尔为假 | |
| `EXPECT_NULL(ptr)` / `ASSERT_NULL(ptr)` | 指针为 NULL | |
| `EXPECT_NOT_NULL(ptr)` / `ASSERT_NOT_NULL(ptr)` | 指针非 NULL | |
| `EXPECT_STR_EQ(s1, s2)` / `ASSERT_STR_EQ` | 字符串相等（NULL 安全） | |
| `EXPECT_STR_NE(s1, s2)` / `ASSERT_STR_NE` | 字符串不等 | |
| `EXPECT_FLOAT_EQ(e, a)` / `ASSERT_FLOAT_EQ` | 浮点近似（epsilon 1e-6） | |
| `EXPECT_DOUBLE_EQ(e, a)` / `ASSERT_DOUBLE_EQ` | 双精度近似（epsilon 1e-15） | |
| `EXPECT_ARRAY_EQ_INT(a, b, n)` | int 数组相等 | |
| `EXPECT_ARRAY_EQ_FLOAT(a, b, n)` | float 数组近似 | |
| `EXPECT_ARRAY_EQ_CHAR(a, b, n)` | char 数组相等 | |

**xtest 特性**：
- **进程隔离**：每个测试在 fork 的子进程中运行，崩溃不影响其他测试
- **并行执行**：`--parallel=N` 多进程并行运行测试
- **超时支持**：`--timeout=N` 秒超时，SIGALRM 自动终止
- **崩溃捕获**：SIGSEGV/SIGABRT/SIGFPE 自动捕获，打印信号和地址
- **JUnit XML 输出**：`--output=xml` 可用于 CI 集成
- **Sanitizer 支持**：Makefile 提供 test-asan/test-tsan/test-leak 目标
- **覆盖率支持**：`test-cov` 目标生成 gcov/lcov 报告

**Makefile 集成**：
```makefile
# xtest 测试目标
XTEST_DIR = third_party/xtest
TEST_SRCS = $(XTEST_DIR)/xtest_runner.c $(wildcard tests/test_*.c)
TEST_OBJS = $(TEST_SRCS:.c=.o)

xtest_runner: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

test: xtest_runner
	./xtest_runner --parallel=0 --verbose

test-asan: CFLAGS += -fsanitize=address
test-asan: clean xtest_runner
	./xtest_runner --no-fork

test-leak: CFLAGS += -fsanitize=address
test-leak: clean xtest_runner
	ASAN_OPTIONS=detect_leaks=1 ./xtest_runner --no-fork

test-cov: CFLAGS += --coverage
test-cov: clean xtest_runner
	./xtest_runner
	gcov src/*.c src/*/*.c
	lcov -c -d . -o coverage.info
	genhtml coverage.info -o coverage
```

**C 语言关键点**：
- xtest 使用 GCC `__attribute__((section("xtest_suites")))` 实现自动注册，无需手动注册测试
- `EXPECT_*` 失败后继续执行，`ASSERT_*` 失败后立即返回——适合在测试中先做致命检查再验证细节
- 不需要手写 `main()`，`xtest_runner.c` 提供了完整的 CLI runner
- xtest 依赖 POSIX API（fork/sigaction/clock_gettime），仅支持 Linux/macOS

**验证**：
1. `make test` 成功编译并运行 test_smoke.c
2. `./xtest_runner --list` 列出已注册的测试
3. `./xtest_runner --parallel=0` 并行执行测试
4. 故意写一个 `FAIL_TEST`，验证 XFAIL 标记正确
5. `./xtest_runner --output=xml` 生成 JUnit XML

---

### Step 0.6 — 通用数据结构

**目标**：实现 C 语言版的 vector、hashmap、双向链表。

**输入**：Step 0.1 + 0.7。

**输出**：

**动态数组 (vector)**：
```c
typedef struct {
    void*  data;       // 元素数组
    size_t size;       // 当前元素数
    size_t capacity;   // 容量
    size_t elem_size;  // 单个元素大小
} vector_t;

void  vector_init(vector_t* v, size_t elem_size);
void  vector_destroy(vector_t* v);
void  vector_push(vector_t* v, const void* elem);        // 尾部追加
void* vector_at(vector_t* v, size_t index);                // 按索引访问
void  vector_pop(vector_t* v);                            // 尾部删除
void  vector_clear(vector_t* v);
```

**哈希表 (hashmap)**：
```c
typedef struct hashmap_entry {
    void* key;
    void* value;
    struct hashmap_entry* next;  // 链地址法
} hashmap_entry_t;

typedef struct {
    hashmap_entry_t* buckets;
    size_t bucket_count;
    size_t size;
    size_t (*hash_fn)(const void* key);
    int    (*equal_fn)(const void* a, const void* b);
} hashmap_t;

void   hashmap_init(hashmap_t* map, size_t (*hash_fn)(const void*), int (*eq_fn)(const void*, const void*));
void   hashmap_destroy(hashmap_t* map);
int    hashmap_put(hashmap_t* map, void* key, void* value);   // 插入/更新
void*  hashmap_get(hashmap_t* map, const void* key);          // 查找
int    hashmap_remove(hashmap_t* map, const void* key);        // 删除
```

**双向链表**：
```c
typedef struct list_node {
    struct list_node* prev;
    struct list_node* next;
} list_node_t;

typedef struct {
    list_node_t sentinel;  // 哨兵节点
    size_t size;
} list_t;

void list_init(list_t* list);
void list_push_front(list_t* list, list_node_t* node);
void list_push_back(list_t* list, list_node_t* node);
list_node_t* list_pop_front(list_t* list);
list_node_t* list_pop_back(list_t* list);
void list_remove(list_node_t* node);
```

**C 语言关键点**：
- 泛型通过 void* + elem_size + 回调函数实现
- 哈希表使用链地址法（比开放寻址更简单，删除方便）
- 链表使用侵入式设计（list_node_t 嵌入到用户结构体中，类似 Linux kernel list）
- 所有结构体提供 init/destroy 生命周期函数

**验证**：
1. vector：push 100 个 int，遍历验证、pop 验证
2. hashmap：put/get/remove 1000 个键值对
3. list：push_front/push_back/pop 验证

---

### Step 0.7 — 内存管理工具

**目标**：统一内存分配、泄漏检测。

**输入**：Step 0.1。

**输出**：
- `src/common/mem.h/c` — 内存分配包装

```c
// Debug 模式：记录分配位置和大小
#ifdef DEBUG_MEM
  #define db_malloc(size)       mem_debug_malloc(size, __FILE__, __LINE__)
  #define db_calloc(nm, sz)    mem_debug_calloc(nm, sz, __FILE__, __LINE__)
  #define db_realloc(ptr, sz)  mem_debug_realloc(ptr, sz, __FILE__, __LINE__)
  #define db_free(ptr)         mem_debug_free(ptr, __FILE__, __LINE__)
  void mem_debug_report(void);  // 输出未释放的分配
#else
  #define db_malloc(size)       malloc(size)
  #define db_calloc(nm, sz)     calloc(nm, sz)
  #define db_realloc(ptr, sz)   realloc(ptr, sz)
  #define db_free(ptr)         free(ptr)
#endif

// 页面缓冲区分配器（预分配大块内存用于 Page 数组）
void* page_alloc(size_t count);  // 分配 count 个 PAGE_SIZE
void  page_free(void* ptr);
```

**验证**：Debug 模式下故意泄漏，调用 mem_debug_report() 验证能检测到。

---

### Step 0.8 — .sql 文件执行器

**目标**：原生支持读取 .sql 文件并执行。

**输入**：Step 0.1 + 0.5。

**输出**：
- `src/tools/sql_runner.h/c` — 读取 .sql 文件，按 `;` 分割语句，逐条解析执行
- 错误处理：某条语句失败时输出行号和错误信息，可选择继续或中止
- 支持 `--` 单行注释和 `/* */` 块注释的跳过

**接口**：
```c
typedef struct {
    int echo;           // 是否输出每条语句
    int stop_on_error;  // 遇错停止
    int error_count;    // 错误计数
} sql_runner_t;

void sql_runner_init(sql_runner_t* runner);
void sql_runner_destroy(sql_runner_t* runner);
int  sql_runner_execute_file(sql_runner_t* runner, const char* path);
int  sql_runner_execute_string(sql_runner_t* runner, const char* sql);
```

> 注意：此步骤需在 Phase 5（SQL 解析器）完成后才能完全测试。此处先建立框架接口。

---

## Phase 1：磁盘与存储管理

### Step 1.1 — Disk Manager

**目标**：实现页面级文件读写，页面分配与释放。

**输入**：Step 0.2。

**输出**：
- `src/storage/disk_manager.h/c`

**接口**：
```c
typedef struct {
    FileIO db_io;          // 数据库文件
    FileIO wal_io;         // WAL 日志文件
    uint32_t page_count;
    page_id_t free_list_head;
} disk_manager_t;

int         disk_manager_init(disk_manager_t* dm, const char* db_file);
void        disk_manager_destroy(disk_manager_t* dm);
page_id_t   disk_manager_allocate_page(disk_manager_t* dm);
void        disk_manager_deallocate_page(disk_manager_t* dm, page_id_t page_id);
int         disk_manager_read_page(disk_manager_t* dm, page_id_t page_id, char* buf);
int         disk_manager_write_page(disk_manager_t* dm, page_id_t page_id, const char* buf);
void        disk_manager_shutdown(disk_manager_t* dm);
```

**数据库文件格式**：
```
Page 0: 文件头 (Header Page)
  magic:        "MINISQLITE\0" (10 bytes)
  version:      uint32_t
  page_count:   uint32_t
  free_list_head: page_id_t
  reserved:     4074 bytes

Page 1+: 数据页面 (4KB each)
```

**页面分配**：优先从 free_list_head 取空闲页；空则扩展文件。

**C 语言关键点**：
- 结构体直接嵌入 FileIO，不用指针间接
- free_list 在空闲页面的前几个字节存储 next_free_page_id
- 错误通过返回值传递，出参通过指针

**验证**：
1. 创建数据库文件，分配 10 个页面
2. 写入不同内容到各页面
3. 关闭文件，重新打开，读回所有页面验证内容一致
4. 释放中间页面，重新分配，验证复用

---

### Step 1.2 — Slotted Page 页面布局

**目标**：定义页面内部的 tuple 存储格式。

**输入**：Step 1.1。

**输出**：
- `src/storage/page.h` — Page 结构和常量
- `src/storage/slotted_page.h/c` — Slotted Page 操作

**Page 结构**：
```c
#define PAGE_SIZE 4096
#define PAGE_HEADER_SIZE 24

typedef struct {
    page_id_t page_id;           // 4 bytes
    lsn_t     page_lsn;          // 8 bytes
    int32_t   num_tuples;        // 4 bytes
    int32_t   free_space_offset; // 4 bytes (从页尾算，向下增长)
    page_id_t next_page_id;      // 4 bytes (链表/兄弟指针)
} page_header_t;  // 24 bytes

// Slot: 4 bytes each
typedef struct {
    uint16_t tuple_offset;  // tuple 在页面中的起始偏移
    uint16_t tuple_size;    // tuple 字节数 (0=已删除)
} slot_t;

// Slotted Page 操作函数（传入页面缓冲区指针）
int32_t  slotted_page_insert(char* page_data, const char* tuple_data, uint16_t tuple_size);
int      slotted_page_get_tuple(char* page_data, slot_id_t slot_id, char* out, uint16_t* out_size);
int      slotted_page_delete_tuple(char* page_data, slot_id_t slot_id);
int      slotted_page_update_tuple(char* page_data, slot_id_t slot_id, const char* data, uint16_t size);
uint32_t slotted_page_free_space(const char* page_data);
```

**C 语言关键点**：
- 不使用 "class"，用纯函数 + 缓冲区指针操作
- 页面缓冲区就是 `char[PAGE_SIZE]`，所有操作直接读写这块内存
- header/slot/tuple 的偏移用宏计算，不依赖编译器对齐

**验证**：
1. 创建 slotted page，插入 5 个 tuple
2. 读取每个 tuple 验证数据正确
3. 删除中间 tuple，再插入新 tuple，验证空间复用
4. 页面满时插入返回失败

---

### Step 1.3 — Tuple 序列化/反序列化

**目标**：定义 tuple 在内存和磁盘上的表示。

**输入**：Step 1.2。

**输出**：
- `src/storage/tuple.h/c` — Tuple 结构和操作
- `src/storage/schema.h/c` — Schema 结构
- `src/storage/column.h` — Column 定义

**Tuple 磁盘格式**：
```
[NULL Bitmap (ceil(N/8) bytes)] [定长字段 (按定义顺序)] [变长字段 (按定义顺序)]
变长字段存储: [length(4B)] [data]
```

**Tuple 内存格式**：
```c
// 内存中的 Value 使用 tagged union
// (见 Step 1.4)

// Tuple 在内存中的表示
typedef struct {
    value_t* values;    // Value 数组 (动态分配)
    int32_t  count;     // 值数量
    rid_t    rid;       // 记录位置
} tuple_t;

// 序列化接口
size_t tuple_serialize(const tuple_t* tuple, const schema_t* schema, char* buf);
int    tuple_deserialize(tuple_t* tuple, const schema_t* schema, const char* buf, size_t size);
int    tuple_get_value(const tuple_t* tuple, const schema_t* schema, column_id_t col, value_t* out);
int    tuple_set_value(tuple_t* tuple, const schema_t* schema, column_id_t col, const value_t* val);
void   tuple_destroy(tuple_t* tuple);
```

**Schema**：
```c
typedef struct {
    column_t* columns;
    int32_t   column_count;
    size_t    fixed_length_size;
    size_t    variable_column_count;
    size_t    null_bitmap_size;
} schema_t;

typedef struct {
    char      name[64];
    type_id_t type;
    uint32_t  max_length;    // VARCHAR 最大长度
    int       nullable;
    int       is_primary_key;
    uint32_t  offset;        // 定长字段偏移
    column_id_t column_id;
} column_t;
```

**验证**：
1. 定义 Schema (int, varchar, float, bool)
2. 创建 Tuple，设置各列值（含 NULL）
3. 序列化 → 反序列化 → 验证所有值一致
4. 测试变长字符串的边界情况

---

### Step 1.4 — 数据类型系统

**目标**：实现 Value 类型，支持所有数据类型的运算。

**输入**：Step 0.3。

**输出**：
- `src/storage/value.h/c` — Value 类型

**Tagged Union 实现**：
```c
typedef enum {
    TYPE_INTEGER = 1,
    TYPE_FLOAT   = 2,
    TYPE_VARCHAR = 3,
    TYPE_BOOLEAN = 4,
    TYPE_NULL    = 5,
} type_id_t;

typedef struct {
    type_id_t type;
    union {
        int64_t int_val;
        double  float_val;
        int     bool_val;    // 0=false, 1=true
    };
    char*  varchar_val;      // VARCHAR 独立指针
    size_t varchar_len;
} value_t;

// 构造函数
value_t value_make_integer(int64_t v);
value_t value_make_float(double v);
value_t value_make_varchar(const char* s);
value_t value_make_boolean(int v);
value_t value_make_null(void);
void    value_destroy(value_t* v);
value_t value_copy(const value_t* v);

// 比较 (NULL 语义: 任何与 NULL 的比较返回 NULL)
value_t value_compare(const value_t* lhs, const value_t* rhs, token_type_t op);

// 算术
value_t value_add(const value_t* lhs, const value_t* rhs);
value_t value_subtract(const value_t* lhs, const value_t* rhs);
value_t value_multiply(const value_t* lhs, const value_t* rhs);
value_t value_divide(const value_t* lhs, const value_t* rhs);

// 类型转换
value_t value_cast_to(const value_t* v, type_id_t target_type);

// 序列化大小
size_t  value_serialized_size(const value_t* v);
```

**C 语言关键点**：
- tagged union 用 enum + union 实现，VARCHAR 用 char* 独立分配
- value_t 的拷贝语义需要手动管理（value_copy / value_destroy）
- varchar_val 需要在 value_destroy 时 free

**验证**：
1. 各类型创建 Value，验证类型正确
2. 比较运算验证（含 NULL 语义）
3. 算术运算验证
4. 类型转换验证

---

### Step 1.5 — 堆文件 (链表式)

**目标**：实现表数据的堆文件组织。

**输入**：Step 1.2 + 1.3。

**输出**：
- `src/storage/heap_file.h/c`

**接口**：
```c
typedef struct {
    buffer_pool_manager_t* bpm;
    page_id_t first_page_id;
    page_id_t last_page_id;
    table_id_t table_id;
} heap_file_t;

void     heap_file_init(heap_file_t* hf, buffer_pool_manager_t* bpm, page_id_t first_page_id, table_id_t table_id);
rid_t    heap_file_insert(heap_file_t* hf, const tuple_t* tuple, const schema_t* schema);
int      heap_file_delete(heap_file_t* hf, const rid_t* rid);
int      heap_file_update(heap_file_t* hf, const rid_t* rid, const tuple_t* tuple, const schema_t* schema);
int      heap_file_get_tuple(heap_file_t* hf, const rid_t* rid, const schema_t* schema, tuple_t* out);
```

**HeapIterator**：
```c
typedef struct {
    buffer_pool_manager_t* bpm;
    const schema_t* schema;
    page_id_t current_page_id;
    slot_id_t current_slot;
} heap_iterator_t;

int heap_iterator_init(heap_iterator_t* it, buffer_pool_manager_t* bpm, const schema_t* schema, page_id_t first_page_id);
int heap_iterator_next(heap_iterator_t* it, tuple_t* out);   // 返回 0=有数据, -1=结束
void heap_iterator_destroy(heap_iterator_t* it);
```

**C 语言关键点**：
- 迭代器用 next() 函数模式，而非 operator++
- 页面遍历时通过 FetchPage/UnpinPage 管理

**验证**：
1. 创建堆文件，插入 100 个 tuple
2. 通过 RID 读取验证每个 tuple
3. 遍历所有 tuple，验证数量和顺序
4. 删除部分 tuple，验证遍历跳过已删除项

---

### Step 1.6 — Free Space Management

**目标**：高效管理空闲页面空间。

**输入**：Step 1.5。

**输出**：
- `src/storage/free_space_manager.h/c`

**方案**：
- 在堆文件层维护一个简易的页面空间映射
- 使用动态数组存储 (page_id, free_space_size) 对
- 插入时查找有足够空间的页面
- 空页面进入 free list 可被复用

**C 语言关键点**：
- 使用 vector_t 存储页面空间信息
- 每次页面修改后调用 fsm_update() 更新空间信息

**验证**：
1. 插入大量 tuple 直到需要新页面
2. 删除部分 tuple 释放空间
3. 再次插入，验证复用已有页面空间

---

## Phase 2：Buffer Pool 管理

### Step 2.1 — Buffer Pool Manager

**目标**：内存中缓存热点页面，减少磁盘 I/O。

**输入**：Step 1.1 + 1.2。

**输出**：
- `src/buffer/buffer_pool_manager.h/c`
- `src/buffer/page_guard.h/c`

**核心数据结构**：
```c
typedef struct {
    char      data[PAGE_SIZE];
    page_id_t page_id;
    int       pin_count;
    int       is_dirty;
    page_latch_t latch;   // 预留，当前 no-op
} page_t;

typedef struct {
    size_t      pool_size;
    page_t*     pages;             // frame 数组 (动态分配)
    hashmap_t   page_table;        // page_id → frame_id 映射
    list_t      free_list;         // 空 frame 链表
    lru_replacer_t* replacer;
    disk_manager_t* disk_manager;
} buffer_pool_manager_t;
```

**关键接口**：
```c
int      bpm_init(buffer_pool_manager_t* bpm, size_t pool_size, disk_manager_t* dm);
void     bpm_destroy(buffer_pool_manager_t* bpm);
page_t*  bpm_fetch_page(buffer_pool_manager_t* bpm, page_id_t page_id);  // 获取页面，pin_count++
page_t*  bpm_new_page(buffer_pool_manager_t* bpm, page_id_t* page_id);  // 分配新页面
int      bpm_unpin_page(buffer_pool_manager_t* bpm, page_id_t page_id, int is_dirty);
int      bpm_flush_page(buffer_pool_manager_t* bpm, page_id_t page_id);
void     bpm_flush_all(buffer_pool_manager_t* bpm);
```

**PageGuard（C 语言 RAII 替代）**：
```c
typedef struct {
    page_t* page;
    buffer_pool_manager_t* bpm;
    int is_dirty;
} page_guard_t;

page_guard_t page_guard_create(page_t* page, buffer_pool_manager_t* bpm);
void         page_guard_release(page_guard_t* guard);  // 手动调用，自动 Unpin
void         page_guard_mark_dirty(page_guard_t* guard);
```

**C 语言关键点**：
- C 无 RAII，PageGuard 需要手动 release，或使用 goto cleanup 模式
- page_table 使用自定义 hashmap（key=page_id, value=frame_id）
- pages 数组使用 page_alloc 分配大块内存

**验证**：
1. 缓存池大小设为 5，读取 10 个不同页面，验证 LRU 淘汰正确
2. 修改页面后 Unpin(is_dirty=true)，验证刷盘后数据持久化
3. 同一页面多次 Fetch，验证 pin_count 正确

---

### Step 2.2 — 页面替换策略 (LRU-2)

**目标**：实现高效的页面替换算法。

**输入**：Step 2.1。

**输出**：
- `src/buffer/lru_replacer.h/c`

**LRU-2 实现**：
```c
typedef struct {
    list_t cold_list;    // 访问 1 次
    list_t hot_list;     // 访问 >=2 次
    hashmap_t access_count;  // frame_id → access_count
    size_t capacity;
} lru_replacer_t;

int  lru_replacer_init(lru_replacer_t* replacer, size_t capacity);
void lru_replacer_destroy(lru_replacer_t* replacer);
int  lru_replacer_victim(lru_replacer_t* replacer, frame_id_t* frame_id);
void lru_replacer_pin(lru_replacer_t* replacer, frame_id_t frame_id);
void lru_replacer_unpin(lru_replacer_t* replacer, frame_id_t frame_id);
size_t lru_replacer_size(lru_replacer_t* replacer);
```

**C 语言关键点**：
- cold_list/hot_list 使用侵入式双向链表
- 每个 frame 对应一个 list_node_t 嵌入在某个结构中
- 淘汰优先级：cold_list 尾部 > hot_list 尾部

**验证**：
1. 插入 10 个 frame，验证淘汰顺序
2. 访问某个 frame 两次，验证提升到 hot_list
3. Pin 后不可被淘汰，Unpin 后可被淘汰

---

### Step 2.3 — 脏页管理

**目标**：追踪脏页，保证刷盘正确性。

**输入**：Step 2.1。

**输出**：
- page_t 中维护 is_dirty 标志
- bpm_unpin_page 传入 is_dirty 参数
- bpm_flush_page 只刷脏页，刷完后清除 dirty
- bpm_flush_all 遍历所有页面刷脏

**验证**：
1. 修改 3 个页面，Unpin(is_dirty=true)
2. 修改 1 个页面，Unpin(is_dirty=false)
3. FlushAll，验证只有 3 个页面被写入磁盘

---

### Step 2.4 — 并发页面访问（预留接口）

**目标**：为后期多线程扩展预留页面级读写锁接口。

**输入**：Step 2.1。

**输出**：
- `src/buffer/page_latch.h` — PageLatch（当前 no-op）

```c
typedef struct {
    // 单线程模式: 空
    // 多线程模式: pthread_rwlock_t rwlock;
} page_latch_t;

void page_latch_read_lock(page_latch_t* latch);
void page_latch_read_unlock(page_latch_t* latch);
void page_latch_write_lock(page_latch_t* latch);
void page_latch_write_unlock(page_latch_t* latch);
```

**验证**：接口可编译，单线程下功能正常。

---

## Phase 3：B+ 树索引

### Step 3.1 — B+ 树基础结构

**目标**：定义 B+ 树的节点结构和基本操作。

**输入**：Step 2.1。

**输出**：
- `src/index/b_plus_tree.h/c`
- `src/index/b_plus_tree_node.h/c`

**节点结构**（存储在页面缓冲区中）：
```
内部节点:
[PageHeader(24B)] [node_type=INTERNAL(1B)] [size(4B)] [Key_0(8B)] [PageId_0(4B)] ... [Key_n] [PageId_n]

叶子节点:
[PageHeader(24B)] [node_type=LEAF(1B)] [size(4B)] [Key_0(8B)] [Value_0(?B)] ... [Key_n] [Value_n]
```

**接口**：
```c
typedef struct {
    index_id_t index_id;
    page_id_t root_page_id;
    buffer_pool_manager_t* bpm;
    key_comparator_t comparator;
    int is_unique;
} b_plus_tree_t;

int   bpt_init(b_plus_tree_t* tree, index_id_t id, buffer_pool_manager_t* bpm, key_comparator_t cmp, int unique);
void  bpt_destroy(b_plus_tree_t* tree);
int   bpt_find(b_plus_tree_t* tree, const value_t* key, value_t* out_value);
int   bpt_find_range(b_plus_tree_t* tree, const value_t* low, const value_t* high, vector_t* results);
int   bpt_insert(b_plus_tree_t* tree, const value_t* key, const value_t* value);
int   bpt_remove(b_plus_tree_t* tree, const value_t* key);
```

**节点操作**（直接操作页面缓冲区）：
```c
// 内部节点
int32_t     internal_node_get_size(const char* page);
page_id_t   internal_node_get_child(const char* page, int index);
int64_t     internal_node_get_key(const char* page, int index);
void        internal_node_set_key(char* page, int index, int64_t key);
int         internal_node_find_position(const char* page, int64_t key);
void        internal_node_insert(char* page, int64_t key, page_id_t child);

// 叶子节点
int32_t     leaf_node_get_size(const char* page);
int64_t     leaf_node_get_key(const char* page, int index);
void*       leaf_node_get_value(const char* page, int index, uint16_t* value_size);
page_id_t   leaf_node_get_next_page(const char* page);
void        leaf_node_set_next_page(char* page, page_id_t page_id);
int         leaf_node_insert(char* page, int64_t key, const void* value, uint16_t value_size);
int         leaf_node_remove(char* page, int index);
```

**C 语言关键点**：
- 节点操作用纯函数 + 页面缓冲区指针，不用结构体映射（避免对齐问题）
- 用宏计算各字段偏移量
- key 固定 8 字节 (int64_t)，value 大小可变

**验证**：
1. 创建 B+ 树，插入 1000 个随机 key
2. Find 验证每个 key 都能找到正确值

---

### Step 3.2 — B+ 树分裂与合并

**目标**：实现节点溢出分裂和下溢合并/重分布。

**输入**：Step 3.1。

**输出**：
- 插入时：叶子节点满 → 分裂 → 递归向上分裂内部节点
- 删除时：叶子节点下溢 → 尝试从兄弟借 → 借不了则合并 → 递归向上合并

**实现策略**：
- 插入路径上的节点记录在动态数组中（替代 C++ 的栈）
- 自底向上递归分裂
- 删除时选择立即合并（保持树最优平衡）

**验证**：
1. 插入大量 key 触发多级分裂，验证树依然平衡
2. 删除大量 key 触发合并，验证树结构正确
3. 交替插入删除，验证稳定性

---

### Step 3.3 — 聚簇索引

**目标**：主键索引的叶子节点直接存储完整 tuple。

**输入**：Step 3.1 + 1.3。

**输出**：
- 聚簇索引的叶子节点 Value = 完整 Tuple 二进制数据
- 主键查找直接返回 Tuple，无需回表
- 范围扫描天然有序

**接口**：
```c
int bpt_find_range(b_plus_tree_t* tree, const value_t* low, const value_t* high, vector_t* results);
```

**IndexIterator**：
```c
typedef struct {
    buffer_pool_manager_t* bpm;
    page_id_t current_page_id;
    int current_index;
} index_iterator_t;

int  index_iterator_init(index_iterator_t* it, b_plus_tree_t* tree, const value_t* start_key);
int  index_iterator_next(index_iterator_t* it, value_t* out_key, value_t* out_value);
void index_iterator_destroy(index_iterator_t* it);
```

**验证**：
1. INSERT 100 条记录
2. SELECT * WHERE id = 50 — 直接返回完整 tuple
3. SELECT * WHERE id BETWEEN 20 AND 30 — 范围扫描有序

---

### Step 3.4 — 二级索引

**目标**：非主键列索引，叶子节点存储主键值。

**输入**：Step 3.3。

**输出**：
- 二级索引叶子节点 Value = 主键值
- 查询流程：二级索引查找 → 获得主键值 → 回表到聚簇索引获取完整 tuple

**验证**：
1. CREATE INDEX idx_age ON t(age)
2. SELECT * WHERE age = 25 → 先查二级索引获得主键 → 再查聚簇索引
3. 验证结果与全表扫描一致

---

### Step 3.5 — 范围扫描

**目标**：利用叶子节点链表实现高效范围查询。

**输入**：Step 3.3。

**输出**：
- IndexIterator 从起始 key 的叶子节点开始，沿 next_page_id 遍历
- 支持 >, >=, <, <= 等范围谓词

**验证**：
1. 插入 1000 条记录
2. SELECT * WHERE id > 500 AND id < 800 — 验证只扫描相关页面
3. 验证结果有序且完整

---

### Step 3.6 — 唯一索引约束

**目标**：INSERT/UPDATE 时检查唯一性。

**输入**：Step 3.4。

**输出**：
- B+ 树 Insert 前先 Find 检查 key 是否已存在
- 存在则返回 DB_DUPLICATE_KEY

**验证**：
1. INSERT id=1 成功
2. INSERT id=1 再次插入失败，报唯一约束错误

---

## Phase 4：系统目录 (Catalog)

### Step 4.1 — __tables 系统表

**目标**：持久化表元数据。

**输入**：Step 1.3 + 3.3。

**输出**：
- `src/catalog/catalog.h/c`

**__tables Schema**：
```
CREATE TABLE __tables (
    table_id    INTEGER NOT NULL,
    table_name  VARCHAR NOT NULL,
    root_page_id INTEGER,
    table_type  VARCHAR   -- "user" or "system"
);
```

**接口**：
```c
typedef struct {
    buffer_pool_manager_t* bpm;
    id_generator_t id_gen;
    hashmap_t table_cache;     // table_id → table_meta_t
    hashmap_t name_to_id;      // table_name → table_id
    hashmap_t index_cache;     // index_id → index_meta_t
    hashmap_t schema_cache;    // table_id → schema_t
} catalog_t;

table_id_t catalog_create_table(catalog_t* cat, const char* name, const schema_t* schema);
int        catalog_get_table(catalog_t* cat, const char* name, table_info_t* out);
int        catalog_drop_table(catalog_t* cat, const char* name);
int        catalog_list_tables(catalog_t* cat, vector_t* out);
```

**验证**：
1. 创建 3 个表
2. 按名查找，验证信息正确
3. 删除表，验证查找失败

---

### Step 4.2 — __columns 系统表

**目标**：持久化列元数据。

**输入**：Step 4.1。

**输出**：
- __columns 表结构
- 按 table_id 建二级索引

**接口**：
```c
int catalog_get_schema(catalog_t* cat, table_id_t table_id, schema_t* out);
int catalog_add_column(catalog_t* cat, table_id_t table_id, const column_t* col);
```

**验证**：
1. 创建表并定义 5 列
2. 读取 Schema，验证列信息一致

---

### Step 4.3 — __indexes 系统表

**目标**：持久化索引元数据。

**输入**：Step 4.1。

**输出**：
- __indexes 表结构
- 二级索引

**接口**：
```c
index_id_t catalog_create_index(catalog_t* cat, const char* name, table_id_t table_id,
                                 column_id_t col_id, int unique);
int        catalog_get_index(catalog_t* cat, const char* name, index_meta_t* out);
int        catalog_get_table_indexes(catalog_t* cat, table_id_t table_id, vector_t* out);
int        catalog_drop_index(catalog_t* cat, const char* name);
```

---

### Step 4.4 — Catalog 缓存

**目标**：内存中缓存元数据，减少反复读盘。

**输入**：Step 4.1-4.3。

**输出**：
- catalog_t 内维护 hashmap 缓存
- DDL 修改时同步更新缓存和磁盘
- 数据库启动时从系统表加载缓存

---

### Step 4.5 — 自增 ID 分配器

**目标**：为 table_id, column_id, index_id 提供自增分配。

**输入**：Step 4.1。

**输出**：
```c
typedef struct {
    uint64_t next_table_id;
    uint64_t next_column_id;
    uint64_t next_index_id;
} id_generator_t;

table_id_t  id_gen_next_table(id_generator_t* gen);
column_id_t id_gen_next_column(id_generator_t* gen);
index_id_t  id_gen_next_index(id_generator_t* gen);
```

---

## Phase 5：SQL 解析器

### Step 5.1 — Lexer (词法分析器)

**目标**：将 SQL 文本拆分为 Token 流。

**输入**：Step 0.3。

**输出**：
- `src/parser/token.h` — Token 结构和 TokenType 枚举
- `src/parser/lexer.h/c` — Lexer 实现

**Token 结构**：
```c
typedef enum {
    // 关键字 (大写)
    TK_SELECT, TK_FROM, TK_WHERE, TK_INSERT, TK_INTO, TK_VALUES,
    TK_UPDATE, TK_SET, TK_DELETE, TK_CREATE, TK_TABLE, TK_DROP,
    TK_INDEX, TK_ON, TK_AND, TK_OR, TK_NOT, TK_NULL, TK_PRIMARY,
    TK_KEY, TK_UNIQUE, TK_INTEGER, TK_FLOAT, TK_VARCHAR, TK_BOOLEAN,
    TK_BEGIN, TK_COMMIT, TK_ROLLBACK, TK_TRANSACTION, TK_ISOLATION,
    TK_LEVEL, TK_READ, TK_COMMITTED, TK_UNCOMMITTED, TK_REPEATABLE,
    TK_EXPLAIN, TK_ORDER, TK_BY, TK_ASC, TK_DESC, TK_GROUP,
    TK_HAVING, TK_LIMIT, TK_JOIN, TK_INNER, TK_LEFT, TK_RIGHT,
    TK_AS, TK_COUNT, TK_SUM, TK_AVG, TK_MIN, TK_MAX,
    TK_BETWEEN, TK_IN, TK_IS, TK_LIKE, TK_TRUE, TK_FALSE,

    // 非关键字
    TK_IDENTIFIER, TK_INT_LITERAL, TK_FLOAT_LITERAL, TK_STRING_LITERAL,

    // 运算符
    TK_EQUAL, TK_NOT_EQUAL, TK_LESS, TK_LESS_EQUAL, TK_GREATER,
    TK_GREATER_EQUAL, TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,

    // 分隔符
    TK_LEFT_PAREN, TK_RIGHT_PAREN, TK_COMMA, TK_SEMICOLON, TK_DOT,

    TK_EOF, TK_ERROR,
} token_type_t;

typedef struct {
    token_type_t type;
    char    lexeme[256];   // 原始文本
    int     line;
    int     column;
} token_t;
```

**Lexer 接口**：
```c
typedef struct {
    const char* source;
    size_t      pos;
    int         line;
    int         column;
} lexer_t;

void  lexer_init(lexer_t* lex, const char* source);
int   lexer_tokenize(lexer_t* lex, token_t** tokens, int* token_count);
void  lexer_destroy(lexer_t* lex);
```

**C 语言关键点**：
- 关键字表用静态数组 + 二分查找（或简单线性扫描，关键字数不多）
- token_t 数组用 vector_t 动态增长

**验证**：
1. 简单 SELECT 语句 → 正确 token 序列
2. 字符串含空格和转义 → 正确识别
3. 非法字符 → 报错行号列号
4. 注释跳过

---

### Step 5.2 — AST 定义

**目标**：定义抽象语法树的节点类型。

**输入**：Step 5.1。

**输出**：
- `src/parser/ast.h` — 所有 AST 节点结构体

**AST 节点层次**（C 语言用结构体 + 函数指针）：
```c
typedef enum {
    AST_CREATE_TABLE, AST_DROP_TABLE, AST_CREATE_INDEX, AST_DROP_INDEX,
    AST_INSERT, AST_UPDATE, AST_DELETE, AST_SELECT,
    AST_BEGIN, AST_COMMIT, AST_ROLLBACK, AST_SET_ISOLATION,
    AST_EXPLAIN,
} stmt_type_t;

// 表达式类型
typedef enum {
    EXPR_LITERAL, EXPR_COLUMN_REF, EXPR_BINARY, EXPR_UNARY,
    EXPR_FUNCTION_CALL, EXPR_SUBQUERY,
} expr_type_t;

// 基础表达式
typedef struct expr {
    expr_type_t type;
    union {
        struct { value_t value; } literal;
        struct { char table[64]; char column[64]; } column_ref;
        struct { struct expr* left; token_type_t op; struct expr* right; } binary;
        struct { token_type_t op; struct expr* operand; } unary;
        struct { char name[64]; struct expr** args; int arg_count; int is_star; } func_call;
        struct { struct stmt* subquery; } subquery;
    };
} expr_t;

// Statement 基础
typedef struct stmt {
    stmt_type_t type;
    union {
        struct { char table_name[64]; column_def_t* columns; int column_count; } create_table;
        struct { char table_name[64]; } drop_table;
        struct { char table_name[64]; char** columns; int col_count;
                 expr_t*** values; int val_row_count; int* val_col_counts; } insert;
        struct { char table_name[64]; assignment_t* assignments; int assign_count;
                 expr_t* where; } update;
        struct { char table_name[64]; expr_t* where; } delete_stmt;
        struct { expr_t** select_list; int select_count;
                 table_ref_t* from_tables; int from_count;
                 join_clause_t* joins; int join_count;
                 expr_t* where; expr_t** group_by; int group_count;
                 expr_t* having; order_by_item_t* order_by; int order_count;
                 int limit; } select;
        struct { char isolation_level[32]; } begin_txn;
        struct { char level[32]; } set_isolation;
        struct { struct stmt* inner; } explain;
    };
} stmt_t;
```

**C 语言关键点**：
- 使用 tagged union（enum + union）代替 C++ 的继承体系
- 字符串字段用固定大小 char[] 避免频繁 malloc
- expr_t 和 stmt_t 的销毁需要递归释放所有子节点

**验证**：AST 节点可正确构建和打印。

---

### Step 5.3 — Parser (递归下降)

**目标**：将 Token 流解析为 AST。

**输入**：Step 5.1 + 5.2。

**输出**：
- `src/parser/parser.h/c`

**接口**：
```c
typedef struct {
    token_t* tokens;
    int      token_count;
    int      pos;
    char     error_msg[512];
    int      error_line;
    int      error_column;
} parser_t;

void  parser_init(parser_t* p, token_t* tokens, int count);
void  parser_destroy(parser_t* p);
stmt_t* parser_parse(parser_t* p);   // 返回 AST 根节点，失败返回 NULL
```

**内部解析函数**：
```c
stmt_t* parser_parse_statement(parser_t* p);
stmt_t* parser_parse_create_table(parser_t* p);
stmt_t* parser_parse_insert(parser_t* p);
stmt_t* parser_parse_select(parser_t* p);
expr_t* parser_parse_expression(parser_t* p);
expr_t* parser_parse_expression_prec(parser_t* p, int min_prec);  // Pratt 优先级爬升法
expr_t* parser_parse_primary(parser_t* p);
```

**表达式优先级**（低→高）：
1. OR
2. AND
3. NOT
4. =, <>, !=, <, <=, >, >=, IS, IN, BETWEEN, LIKE
5. +, -
6. *, /
7. 一元 -, NOT, 函数调用, 括号

**C 语言关键点**：
- 所有 expr_t* 和 stmt_t* 通过 malloc 分配
- 错误信息存储在 parser_t 中
- 调用者负责释放返回的 AST（ast_destroy 递归释放）

**验证**：
1. `SELECT * FROM t WHERE a > 1 AND b = 'hello'` → 正确 AST
2. `INSERT INTO t VALUES (1, 2.0, 'test')` → 正确 AST
3. `CREATE TABLE t (id INTEGER PRIMARY KEY, name VARCHAR(100))` → 正确 AST
4. 语法错误 → 报错行号 + 期望 token 信息

---

### Step 5.4 — 语法错误报告

**目标**：提供清晰的语法错误信息。

**输入**：Step 5.3。

**输出**：
- 错误信息格式：`Syntax Error: line X, column Y: expected TOKEN but found TOKEN`
- 常见错误的友好提示

**验证**：
1. `SELECT * FROM` → 报错期望表名
2. `CREAT TABLE t` → 提示可能是 CREATE

---

### Step 5.5 — .sql 文件读取与执行

**目标**：完善 .sql 文件执行器。

**输入**：Step 0.8 + 5.3。

**输出**：
- sql_runner 现在可完整工作：读取文件 → 拆分语句 → 解析 AST → 交给执行引擎

**验证**：
1. 创建 `test_schema.sql` 包含建表语句
2. 创建 `test_data.sql` 包含插入和查询
3. 执行两个文件，验证结果正确

---

## Phase 6：查询计划与优化

### Step 6.1 — 逻辑计划生成

**目标**：将 AST 转换为逻辑算子树。

**输入**：Step 5.2 + 4.1。

**输出**：
- `src/planner/logical_plan.h` — 逻辑算子节点定义
- `src/planner/planner.h/c` — Planner

**逻辑算子**：
```c
typedef enum {
    LOP_SCAN, LOP_INDEX_SCAN, LOP_FILTER, LOP_PROJECT, LOP_JOIN,
    LOP_AGGREGATE, LOP_SORT, LOP_LIMIT, LOP_INSERT, LOP_UPDATE, LOP_DELETE
} logical_op_t;

typedef struct logical_node {
    logical_op_t op;
    struct logical_node** children;  // 子节点数组
    int child_count;
    union {
        struct { char table_name[64]; table_id_t table_id; } scan;
        struct { index_id_t index_id; expr_t* low_key; expr_t* high_key; } index_scan;
        struct { expr_t* predicate; } filter;
        struct { expr_t** expressions; int expr_count; char** output_names; } project;
        struct { char join_type[16]; expr_t* condition; } join;
        struct { expr_t** group_by; int group_count; aggregate_expr_t* aggregates; int agg_count; } aggregate;
        struct { order_by_item_t* items; int count; } sort;
        struct { int limit; } limit_node;
        struct { char table_name[64]; vector_t* values; } insert;
        struct { char table_name[64]; assignment_t* assignments; expr_t* where; } update;
        struct { char table_name[64]; expr_t* where; } delete_node;
    };
} logical_node_t;
```

**Planner 接口**：
```c
logical_node_t* planner_plan(planner_t* p, const stmt_t* stmt);
void            logical_node_destroy(logical_node_t* node);
```

**验证**：
1. `SELECT * FROM t WHERE a > 1` → Scan(t) → Filter(a>1) → Project(*)
2. JOIN 语句 → 多表连接计划正确

---

### Step 6.2 — 物理计划生成

**目标**：将逻辑算子映射为物理算子。

**输入**：Step 6.1。

**输出**：
- `src/planner/physical_plan.h` — 物理算子节点定义
- `src/planner/physical_planner.h/c`

**映射规则**：
| 逻辑算子 | 物理算子 |
|----------|---------|
| LogicalScan | SeqScan / IndexScan |
| LogicalFilter | Filter (合并到 Scan) |
| LogicalProject | Project |
| LogicalJoin | NestedLoopJoin / HashJoin |
| LogicalAggregate | HashAggregate |
| LogicalSort | Sort |
| LogicalLimit | Limit |

**索引选择规则**（RBO）：
- WHERE 等值列有索引 → IndexScan
- 否则 → SeqScan

---

### Step 6.3 — RBO 规则优化

**目标**：实现启发式查询优化规则。

**输入**：Step 6.1。

**输出**：
- `src/planner/optimizer.h/c`

**优化规则**（按应用顺序）：
1. MatchIndex → 将等值条件转换为 IndexScan
2. PushDownFilter → Filter 下推
3. MergeFilterScan → 合并 Filter 和 Scan
4. PruneColumns → 删除不需要的列
5. ReorderJoin → 重排连接顺序

---

### Step 6.4 — EXPLAIN 命令

**目标**：输出查询计划树。

**输入**：Step 6.2。

**输出**：
- `EXPLAIN SELECT ...` → 打印物理计划树（缩进树形结构）

---

### Step 6.5 — 简单统计信息（后期）

**目标**：收集表和列的统计信息。

**输入**：Step 4.1。

**输出**：
- `__stats` 系统表
- `ANALYZE TABLE t` 命令

---

### Step 6.6 — CBO 基础（后期）

**目标**：基于代价的查询优化。

**输入**：Step 6.5。

---

## Phase 7：查询执行引擎

### Step 7.1 — Volcano 迭代器框架

**目标**：实现 Init/Next/Close 执行模型。

**输入**：Step 6.2。

**输出**：
- `src/executor/executor.h` — Executor 基础接口

**C 语言 Volcano 接口**：
```c
typedef struct executor executor_t;

struct executor {
    // 函数指针表 (手写 vtable)
    int   (*init)(executor_t* self);
    int   (*next)(executor_t* self, tuple_t* out);   // 0=有数据, -1=结束, <0=错误
    void  (*close)(executor_t* self);

    executor_context_t* exec_ctx;
    executor_t** children;    // 子算子数组
    int child_count;
};
```

**ExecutorContext**：
```c
typedef struct {
    catalog_t*              catalog;
    buffer_pool_manager_t*  bpm;
    transaction_t*          txn;
    lock_manager_t*         lock_mgr;
} executor_context_t;
```

**C 语言关键点**：
- 用函数指针实现多态（手写 vtable）
- self 指针作为每个函数的第一个参数（类似 Python 的 self）
- next() 使用出参模式返回 tuple

**验证**：框架可编译，基类接口完整。

---

### Step 7.2 — 基础算子

**目标**：实现 SeqScan, IndexScan, Insert, Update, Delete。

**输入**：Step 7.1。

**输出**：
- `src/executor/seq_scan_executor.h/c`
- `src/executor/index_scan_executor.h/c`
- `src/executor/insert_executor.h/c`
- `src/executor/update_executor.h/c`
- `src/executor/delete_executor.h/c`

**SeqScanExecutor 示例**：
```c
typedef struct {
    executor_t base;          // 继承基类
    heap_file_t* heap_file;
    heap_iterator_t iter;
    expr_t* predicate;
} seq_scan_executor_t;

int   seq_scan_init(executor_t* self);
int   seq_scan_next(executor_t* self, tuple_t* out);
void  seq_scan_close(executor_t* self);
```

**验证**：
1. SeqScan：插入 10 条 → 扫描返回 10 条
2. IndexScan：按主键查找 → 返回 1 条
3. Insert + 二级索引更新
4. Update：更新后查询返回新值
5. Delete：删除后扫描不返回

---

### Step 7.3 — Filter + Project

**目标**：实现 WHERE 过滤和列裁剪。

**输入**：Step 7.2 + 7.4。

**输出**：
- `src/executor/filter_executor.h/c`
- `src/executor/project_executor.h/c`

---

### Step 7.4 — 表达式求值器

**目标**：执行 WHERE、SELECT 列表等中的表达式。

**输入**：Step 5.2 + 1.4。

**输出**：
- `src/executor/expression_evaluator.h/c`

```c
int expr_evaluate(const expr_t* expr, const tuple_t* tuple, const schema_t* schema, value_t* out);
```

**NULL 语义**：
- 算术：任何操作数为 NULL → 结果为 NULL
- 比较：任何操作数为 NULL → 结果为 NULL
- AND：FALSE AND NULL → FALSE; TRUE AND NULL → NULL
- OR：TRUE OR NULL → TRUE; FALSE OR NULL → NULL
- IS NULL / IS NOT NULL：不返回 NULL

---

### Step 7.5 — Nested Loop Join

**目标**：实现最基础的连接算法。

**输入**：Step 7.1。

**输出**：
- `src/executor/nested_loop_join_executor.h/c`

**验证**：
1. 等值连接正确
2. 空表连接 → 无结果

---

### Step 7.6 — Hash Join

**目标**：实现高效的等值连接。

**输入**：Step 7.1。

**输出**：
- `src/executor/hash_join_executor.h/c`

**验证**：
1. 100 行 × 200 行等值连接 → 结果正确
2. 无匹配行 → 空结果

---

### Step 7.7 — 聚合算子

**目标**：实现 GROUP BY + 聚合函数。

**输入**：Step 7.1 + 7.4。

**输出**：
- `src/executor/hash_aggregate_executor.h/c`

**支持**：COUNT(*), COUNT(col), SUM, AVG, MIN, MAX

**验证**：
1. `SELECT COUNT(*) FROM t` → 总行数
2. `SELECT age, COUNT(*) FROM t GROUP BY age` → 分组计数
3. `SELECT age, AVG(salary) FROM t GROUP BY age HAVING AVG(salary) > 5000`

---

### Step 7.8 — Sort 算子

**目标**：实现 ORDER BY。

**输入**：Step 7.1。

**输出**：
- `src/executor/sort_executor.h/c`

**算法**：
- 全部拉取子算子输出到内存
- 使用 qsort + 自定义比较器（支持 ASC/DESC、多列排序）

**C 语言关键点**：
- qsort_r 或全局变量传递比较上下文
- NULL 排序：NULL LAST

---

### Step 7.9 — Limit 算子

**目标**：实现 LIMIT。

**输入**：Step 7.1。

**输出**：
- `src/executor/limit_executor.h/c`

---

### Step 7.10 — 子查询（后期）

**目标**：支持标量子查询和 IN 子查询。

**输入**：Step 7.1。

---

## Phase 8：事务管理

### Step 8.1 — 事务状态机

**目标**：管理事务生命周期。

**输入**：Step 0.3。

**输出**：
- `src/transaction/transaction.h/c`
- `src/transaction/transaction_manager.h/c`

**状态机**：
```
┌─────────┐  BEGIN   ┌────────┐  COMMIT  ┌───────────┐
│ Invalid │ ───────→ │ Active │ ───────→ │ Committed  │
└─────────┘          └────────┘          └───────────┘
                        │  ROLLBACK
                        ↓
                   ┌─────────┐
                   │ Aborted  │
                   └─────────┘
```

**接口**：
```c
typedef enum { TXN_INVALID, TXN_ACTIVE, TXN_COMMITTED, TXN_ABORTED } txn_state_t;
typedef enum { ISOLATION_READ_UNCOMMITTED, ISOLATION_READ_COMMITTED, ISOLATION_REPEATABLE_READ } isolation_level_t;

typedef struct {
    txn_id_t txn_id;
    txn_state_t state;
    isolation_level_t isolation_level;
    lsn_t last_lsn;
    hashmap_t table_lock_set;   // ResourceId set
    hashmap_t row_lock_set;     // table_id → RID set
} transaction_t;

txn_id_t txn_manager_begin(txn_manager_t* mgr, isolation_level_t level);
int      txn_manager_commit(txn_manager_t* mgr, txn_id_t txn_id);
int      txn_manager_abort(txn_manager_t* mgr, txn_id_t txn_id);
```

---

### Step 8.2 — Lock Manager

**目标**：实现锁的请求、授予、释放。

**输入**：Step 8.1。

**输出**：
- `src/transaction/lock_manager.h/c`

**锁请求结构**：
```c
typedef enum { LOCK_S = 0, LOCK_X = 1, LOCK_IS = 2, LOCK_IX = 3 } lock_mode_t;

typedef struct {
    txn_id_t txn_id;
    lock_mode_t mode;
    int granted;
} lock_request_t;

typedef struct {
    lock_request_t* requests;   // 动态数组
    int request_count;
} lock_request_queue_t;

typedef struct {
    resource_id_t resource;
    lock_request_queue_t queue;
    list_node_t hash_node;     // 侵入式链表节点（用于 hashmap bucket）
} lock_entry_t;
```

**兼容矩阵**：
```c
static int COMPATIBLE[4][4] = {
    // IS   S    IX   X
    {  1,   1,   1,   0},  // IS
    {  1,   1,   0,   0},  // S
    {  1,   0,   1,   0},  // IX
    {  0,   0,   0,   0},  // X
};
```

**接口**：
```c
int lock_manager_lock_table(lock_manager_t* mgr, txn_id_t txn_id, lock_mode_t mode, table_id_t table_id);
int lock_manager_lock_row(lock_manager_t* mgr, txn_id_t txn_id, lock_mode_t mode, table_id_t table_id, const rid_t* rid);
int lock_manager_unlock_table(lock_manager_t* mgr, txn_id_t txn_id, table_id_t table_id);
int lock_manager_unlock_row(lock_manager_t* mgr, txn_id_t txn_id, table_id_t table_id, const rid_t* rid);
```

---

### Step 8.3 — 多粒度锁协议

**目标**：实现意向锁层级协议。

**输入**：Step 8.2。

**协议规则**：
1. 行 S 锁前必须先有表 IS 或更强锁
2. 行 X 锁前必须先有表 IX 或更强锁
3. 释放时行锁先释放，表锁后释放
4. 支持锁升级（IS→S, IS→IX, IX→X 等）

**验证**：
1. 未获取表级意向锁就加行锁 → 报错
2. IS 表锁 + S 行锁 → 正确
3. 锁升级 IS→IX → 正确

---

### Step 8.4 — 死锁检测

**目标**：检测死锁并选择牺牲者。

**输入**：Step 8.2。

**输出**：
- `src/transaction/deadlock_detector.h/c`

**Wait-For Graph**：
```c
typedef struct {
    lock_manager_t* lock_mgr;
    hashmap_t graph;   // txn_id → txn_id vector (邻接表)
} deadlock_detector_t;

int deadlock_detector_detect(deadlock_detector_t* dd, txn_id_t* victim);  // 返回 0=有死锁, -1=无
```

**周期检测**：DFS + 颜色标记（WHITE/GRAY/BLACK）

**牺牲者选择**：youngest transaction (txn_id 最大)

---

### Step 8.5 — 隔离级别支持

**目标**：支持 READ UNCOMMITTED / READ COMMITTED / REPEATABLE READ。

**输入**：Step 8.2。

**不同隔离级别下的锁行为**：

| 操作 | READ UNCOMMITTED | READ COMMITTED | REPEATABLE READ |
|------|-----------------|----------------|-----------------|
| 读行 | 无锁 | IS表锁 + S行锁(读完释放) | IS表锁 + S行锁(事务结束释放) |
| 写行 | IX表锁 + X行锁(事务结束释放) | IX表锁 + X行锁(事务结束释放) | IX表锁 + X行锁(事务结束释放) |

**SET TRANSACTION ISOLATION LEVEL 命令切换**

---

### Step 8.6 — WAL 日志管理

**目标**：实现预写日志的写入和刷盘。

**输入**：Step 1.1。

**输出**：
- `src/transaction/wal_manager.h/c`
- `src/transaction/log_record.h`

**日志记录类型**：
```c
typedef enum {
    LOG_BEGIN = 1, LOG_COMMIT = 2, LOG_ABORT = 3,
    LOG_UPDATE = 4, LOG_INSERT = 5, LOG_DELETE = 6,
    LOG_CHECKPOINT = 7, LOG_CLR = 8,
} log_record_type_t;

typedef struct {
    lsn_t lsn;
    txn_id_t txn_id;
    lsn_t prev_lsn;
    log_record_type_t type;
    char*  payload;
    size_t payload_size;
    uint32_t checksum;
} log_record_t;
```

**日志文件格式**：
```
[LSN(8B)] [TxnID(8B)] [PrevLSN(8B)] [Type(4B)] [Length(4B)] [Payload] [Checksum(4B)]
```

**WAL 接口**：
```c
lsn_t wal_append(wal_manager_t* wm, const log_record_t* record);
int   wal_flush(wal_manager_t* wm, lsn_t target_lsn);
lsn_t wal_get_last_lsn(wal_manager_t* wm);
lsn_t wal_get_flushed_lsn(wal_manager_t* wm);
int   wal_read_records(wal_manager_t* wm, lsn_t start, log_record_t** out, int* count);
```

---

### Step 8.7 — 物理日志记录

**目标**：记录页面级 before/after image。

**输入**：Step 8.6。

**输出**：
- UPDATE_RECORD payload: (page_id, slot_id, old_tuple, new_tuple)
- PageLSN: 页面 header 中存储最近修改的 LSN
- WAL Rule: 刷页前检查 page.page_lsn <= wal.flushed_lsn

---

### Step 8.8 — ARIES 崩溃恢复

**目标**：实现三阶段恢复算法。

**输入**：Step 8.6 + 8.7。

**输出**：
- `src/transaction/recovery_manager.h/c`

**三阶段**：
1. **Analysis**: 从最近 Checkpoint 扫描日志，重建 ATT 和 DPT
2. **Redo**: 从 DPT 最小 recLSN 开始重放
3. **Undo**: 对 ATT 中活跃事务逆序撤销

**接口**：
```c
typedef struct {
    wal_manager_t* wal;
    buffer_pool_manager_t* bpm;
    transaction_manager_t* txn_mgr;
} recovery_manager_t;

int recovery_manager_recover(recovery_manager_t* rm);
```

**验证**：
1. 正常运行 → 模拟崩溃 → 重启恢复 → 数据一致
2. 提交后崩溃 → 恢复后已提交数据保留
3. 未提交崩溃 → 恢复后未提交数据回滚

---

### Step 8.9 — Sharp Checkpoint

**目标**：暂停事务，刷所有脏页，记录检查点。

**输入**：Step 8.6 + 2.1。

**Checkpoint 流程**：
1. 暂停接受新事务
2. 等待所有活跃事务完成
3. 刷所有脏页
4. 写入 CHECKPOINT_RECORD
5. 恢复接受事务

---

### Step 8.10 — 2PL+MVCC 混合方案（设计文档）

**目标**：在 design.md 中详细描述后期 2PL+MVCC 混合方案，Phase 8 不实现代码。

---

## Phase 9：集成与工具

### Step 9.1 — 交互式 REPL

**目标**：命令行交互式 SQL 执行。

**输入**：Step 5.3 + 7.1。

**输出**：
- `src/tools/repl.h/c`

```c
typedef struct {
    catalog_t* catalog;
    planner_t* planner;
    transaction_manager_t* txn_mgr;
} repl_t;

void repl_run(repl_t* repl);
```

**支持点命令**：
```
.help, .tables, .schema, .isolation, .read, .quit
```

**结果格式**：
```
+------+-------+-----+
| id   | name  | age |
+------+-------+-----+
| 1    | Alice | 30  |
+------+-------+-----+
1 rows returned.
```

---

### Step 9.2 — .sql 文件执行完善

**目标**：完善 .sql 文件执行器。

**输入**：Step 5.5 + 9.1。

**输出**：
- 支持 --echo, --stop-on-error, --continue-on-error
- 退出码：0 全部成功，1 有错误

---

### Step 9.3 — 端到端集成测试

**目标**：SQL → 结果的完整链路测试。

**输入**：所有 Phase。

**输出**：
- `tests/` 目录下使用 xtest 框架的集成测试文件
- 使用 `TEST_F` + Fixture 实现 setup/teardown（如启动数据库、创建表等）

**测试场景**：
1. 建表 → 插入 → 查询 → 更新 → 删除 → 删表
2. 多表 JOIN 查询
3. 事务：BEGIN → 操作 → COMMIT/ROLLBACK
4. 崩溃恢复
5. 死锁场景
6. 隔离级别切换

**验证**：`./xtest_runner --parallel=0` 所有集成测试通过。

---

### Step 9.4 — 多线程扩展点标注

**目标**：标注哪些模块可并行化。

**输出**：
- 代码注释 `/* THREAD-SAFETY-NOTE: ... */`
- 主要扩展点：BPM 页面锁、LockManager mutex、WAL mutex、TxnManager mutex

---

### Step 9.5 — 2PL+MVCC 混合实现（后期）

**目标**：根据 design.md 中的方案实现 MVCC 读 + 2PL 写。

---

## 执行优先级总结

```
Phase 0 ──→ Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4
                                                      │
                                           Phase 5 ──→ Phase 6 ──→ Phase 7 ──→ Phase 8 ──→ Phase 9
```

- Phase 0-4 可顺序执行（存储和元数据是基础）
- Phase 5-7 在 Phase 4 后开始（解析和执行依赖元数据）
- Phase 8 在 Phase 7 后（事务管理需要完整执行引擎）
- Phase 9 最后（集成和工具）
