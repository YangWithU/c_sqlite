# MiniSQLite (C 版) - 详细设计方案

> 基于 cpp_sqlite 的设计方案，适配为纯 C 实现。本文档详细描述每个模块的数据结构、接口定义、功能取舍、C 语言适配方案。

---

## 目录

1. [整体架构](#1-整体架构)
2. [磁盘管理器 (Disk Manager)](#2-磁盘管理器)
3. [存储引擎 - Slotted Page](#3-存储引擎---slotted-page)
4. [Tuple 序列化与数据类型](#4-tuple-序列化与数据类型)
5. [堆文件](#5-堆文件)
6. [Buffer Pool Manager](#6-buffer-pool-manager)
7. [B+ 树索引](#7-b-树索引)
8. [系统目录 (Catalog)](#8-系统目录-catalog)
9. [SQL 解析器](#9-sql-解析器)
10. [查询计划与优化](#10-查询计划与优化)
11. [查询执行引擎](#11-查询执行引擎)
12. [事务管理](#12-事务管理)
13. [WAL 与 ARIES 恢复](#13-wal-与-aries-恢复)
14. [工具层 - REPL 与 .sql 执行](#14-工具层)
15. [拓展阅读 - 2PL+MVCC 混合方案](#15-拓展阅读---2plmvcc-混合方案)
16. [拓展阅读 - 物化模型与编译执行](#16-拓展阅读---物化模型与编译执行)

---

## 1. 整体架构

### 1.1 分层架构

```
┌──────────────────────────────────────────────────────┐
│                   SQL Interface Layer                 │
│  ┌──────────┐ ┌──────────┐ ┌───────────┐             │
│  │  Lexer   │→│  Parser  │→│  Planner  │             │
│  └──────────┘ └──────────┘ └───────────┘             │
├──────────────────────────────────────────────────────┤
│                   Execution Engine                    │
│  ┌──────────────────────────────────────┐             │
│  │  Volcano Iterators (Init/Next/Close) │             │
│  │  + Expression Evaluator              │             │
│  └──────────────────────────────────────┘             │
├──────────────────────────────────────────────────────┤
│                   Storage Engine                      │
│  ┌─────────────┐ ┌──────────┐ ┌───────────┐           │
│  │ Buffer Pool │→│ B+ Tree  │→│ Heap File │           │
│  └─────────────┘ └──────────┘ └───────────┘           │
├──────────────────────────────────────────────────────┤
│                   Transaction Manager                 │
│  ┌────────────┐ ┌─────────┐ ┌─────────────────┐     │
│  │Lock Manager│→│WAL Mgr  │→│Recovery Manager │     │
│  └────────────┘ └─────────┘ └─────────────────┘     │
├──────────────────────────────────────────────────────┤
│                   Disk Manager                        │
│  ┌──────────────────────────────────────┐             │
│  │  页面级文件读写 (FILE*) + 文件头管理  │             │
│  └──────────────────────────────────────┘             │
└──────────────────────────────────────────────────────┘
```

### 1.2 层间依赖规则

- 上层可调用下层，下层不可调用上层
- 同层模块可互相引用（通过明确接口）
- 跨层调用必须通过明确接口（不直接访问内部数据结构）

### 1.3 全局类型定义

```c
// src/common/types.h
#include <stdint.h>
#include <stddef.h>

typedef int32_t   page_id_t;
typedef int32_t   slot_id_t;
typedef uint64_t  txn_id_t;
typedef uint64_t  lsn_t;
typedef int32_t   table_id_t;
typedef int32_t   column_id_t;
typedef int32_t   index_id_t;
typedef int32_t   frame_id_t;

#define PAGE_SIZE          4096
#define INVALID_PAGE_ID    (-1)
#define INVALID_LSN        (0)
#define INVALID_TXN_ID     (0)
#define INVALID_TABLE_ID   (-1)
#define INVALID_INDEX_ID   (-1)
#define INVALID_SLOT_ID    (-1)
```

### 1.4 错误码体系

```c
// src/common/error.h
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
    DB_PAGE_FULL,
    DB_INDEX_FULL,
    DB_INTERNAL_ERROR = 9001,
} error_code_t;

const char* error_code_to_string(error_code_t code);
```

### 1.5 C 语言编码规范

- **命名**：模块前缀 + 下划线分隔（如 `bpm_fetch_page`, `disk_manager_read_page`）
- **类型**：结构体用 `_t` 后缀（如 `page_t`, `tuple_t`）
- **生命周期**：所有结构体提供 `xxx_init()` / `xxx_destroy()` 函数对
- **错误处理**：函数返回 `int`（0=成功，负数=错误码），结果通过出参返回
- **内存**：使用 `db_malloc/db_free` 包装，便于泄漏检测
- **头文件**：使用 `#pragma once`

### 1.6 测试框架 — xtest

本项目使用 [xtest](https://github.com/nicholasgasior/xtest) 作为单元测试框架，集成方式如下：

**集成位置**：`third_party/xtest/xtest.h` + `third_party/xtest/xtest_runner.c`

**核心 API**：

```c
// 基本测试定义（自动注册到 ELF section，无需手动注册）
TEST(suite_name, test_name) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(condition);
    ASSERT_NOT_NULL(ptr);         // 致命断言：失败后立即 return
}

// 带 Fixture 的测试（setup/teardown）
static void my_setup(void)    { /* 初始化资源 */ }
static void my_teardown(void) { /* 清理资源 */ }
TEST_DEFINE_FIXTURE(my_fxt, my_setup, my_teardown);

TEST_F(suite, test_with_fixture, my_fxt) {
    // setup 已调用；测试结束后 teardown 自动调用
}

// 预期失败测试
FAIL_TEST(suite, expected_fail) {
    EXPECT_EQ(1, 2);  // 预期会失败，标记为 XFAIL
}

// 禁用测试
DISABLED_TEST(suite, skip_me) {
    // 运行时跳过
}

// 基准测试
TEST_BENCH(name, iterations) {
    // 迭代 iterations 次并计时
}

// 测试内日志
TEST_LOG("value = %d", x);
```

**断言宏**（EXPECT = 非致命，ASSERT = 致命）：

| 宏 | 用途 |
|----|------|
| `EXPECT_EQ / ASSERT_EQ` | 整数相等 |
| `EXPECT_NE / ASSERT_NE` | 整数不等 |
| `EXPECT_TRUE / ASSERT_TRUE` | 布尔为真 |
| `EXPECT_FALSE / ASSERT_FALSE` | 布尔为假 |
| `EXPECT_NULL / ASSERT_NULL` | 指针为 NULL |
| `EXPECT_NOT_NULL / ASSERT_NOT_NULL` | 指针非 NULL |
| `EXPECT_STR_EQ / ASSERT_STR_EQ` | 字符串相等（NULL 安全） |
| `EXPECT_STR_NE / ASSERT_STR_NE` | 字符串不等 |
| `EXPECT_FLOAT_EQ / ASSERT_FLOAT_EQ` | 浮点近似（epsilon 1e-6） |
| `EXPECT_DOUBLE_EQ / ASSERT_DOUBLE_EQ` | 双精度近似（epsilon 1e-15） |
| `EXPECT_ARRAY_EQ_INT / ASSERT_ARRAY_EQ_INT` | int 数组相等 |
| `EXPECT_ARRAY_EQ_FLOAT / ASSERT_ARRAY_EQ_FLOAT` | float 数组近似 |
| `EXPECT_ARRAY_EQ_CHAR / ASSERT_ARRAY_EQ_CHAR` | char 数组相等 |

**xtest 关键特性**：
- **进程隔离**：每个测试在 fork 的子进程中运行，崩溃不影响其他测试
- **并行执行**：`--parallel=N` 多进程并行
- **超时支持**：`--timeout=N` 秒超时自动终止
- **崩溃捕获**：SIGSEGV/SIGABRT/SIGFPE 自动捕获并打印信息
- **JUnit XML 输出**：`--output=xml` 用于 CI 集成
- **Sanitizer/Coverage**：test-asan、test-tsan、test-leak、test-cov 目标

**取舍**：选择 xtest 而非自建测试框架。

理由：
1. xtest 提供进程隔离和并行执行，自建框架难以实现
2. 丰富的断言宏（浮点近似、数组比较、字符串 NULL 安全）覆盖数据库测试需求
3. Fixture 支持 setup/teardown，适合需要初始化磁盘/缓冲区的数据库模块测试
4. JUnit XML 输出便于 CI/CD 集成
5. xtest 仅两个文件（xtest.h + xtest_runner.c），集成成本极低
6. 代价：依赖 POSIX API（fork/sigaction），仅支持 Linux/macOS

---

## 2. 磁盘管理器

### 2.1 数据库文件格式

```
┌────────────────────────────────────────────────┐
│ Page 0: 文件头 (Header Page)                    │
│   magic:        "MINISQLITE\0" (10 bytes)       │
│   version:      uint32_t                        │
│   page_count:   uint32_t (当前总页面数)          │
│   free_list_head: page_id_t (空闲页面链表头)     │
│   reserved:     4074 bytes (保留)               │
├────────────────────────────────────────────────┤
│ Page 1+: 数据页面 (4KB each)                    │
└────────────────────────────────────────────────┘
```

**取舍说明**：
- 选择单文件存储（类似 SQLite 哲学），而非 PostgreSQL 的每表一个文件
- 文件头占用完整一页，保留空间为后期扩展预留（checkpoint 信息、schema 版本号等）
- WAL 使用独立文件（`<db_name>.wal`），分离数据与日志 I/O

### 2.2 页面分配策略

- **分配**：优先从 free_list_head 取空闲页面；空则扩展文件（page_count++）
- **释放**：将页面加入 free_list 链表头部（LIFO，最近释放的优先重用）
- **free_list 结构**：每个空闲页面的前几个字节存储 next_free_page_id

```
空闲页面内部:
[next_free_page_id(4B)] [空...]
```

### 2.3 DiskManager 接口

```c
typedef struct {
    FILE*   db_fp;            // 数据库文件指针
    FILE*   wal_fp;           // WAL 日志文件指针
    char    db_file_name[256];
    char    wal_file_name[256];
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

**C 语言关键点**：
- 使用 FILE* + fread/fwrite/fseek 进行页面 I/O
- 每次 ReadPage/WritePage 使用 fseek 定位 + fread/fwrite 4096 字节
- 初始化时读取 Page 0 的文件头信息
- 关闭时写入更新后的文件头

### 2.4 多线程扩展点

> **THREAD-SAFETY-NOTE**: DiskManager 的文件操作需用 mutex 保护。后期可改为每线程独立 FILE* 指针以减少争用。

---

## 3. 存储引擎 - Slotted Page

### 3.1 页面通用 Header

```c
// 存储在每个页面最前面的 24 字节
typedef struct {
    page_id_t page_id;           // 4 bytes
    lsn_t     page_lsn;          // 8 bytes (最近修改该页的 WAL LSN)
    int32_t   num_tuples;        // 4 bytes (当前 slot 数量)
    int32_t   free_space_offset; // 4 bytes (空闲空间起始偏移，从页头向后增长)
    page_id_t next_page_id;      // 4 bytes (链表/兄弟指针)
} page_header_t;
// sizeof = 24 bytes (注意：需要确保无填充，使用 __attribute__((packed)) 或手动偏移)
```

**C 语言对齐问题**：
- 由于 page_header_t 包含 8 字节的 lsn_t，在 32 位系统上可能有对齐填充
- 解决方案 A：使用 `#pragma pack(1)` 或 `__attribute__((packed))`
- 解决方案 B：不使用结构体映射，用 memcpy 逐字段读写
- **推荐方案 B**：手动偏移读写，避免对齐和可移植性问题

```c
// 手动偏移读写宏
#define PAGE_HEADER_OFFSET_PAGE_ID     0
#define PAGE_HEADER_OFFSET_PAGE_LSN    4
#define PAGE_HEADER_OFFSET_NUM_TUPLES  12
#define PAGE_HEADER_OFFSET_FREE_SPACE  16
#define PAGE_HEADER_OFFSET_NEXT_PAGE   20
#define PAGE_HEADER_SIZE               24

// 读取
page_id_t page_get_page_id(const char* data) {
    int32_t v;
    memcpy(&v, data + PAGE_HEADER_OFFSET_PAGE_ID, sizeof(v));
    return v;
}

// 写入
void page_set_page_id(char* data, page_id_t id) {
    int32_t v = id;
    memcpy(data + PAGE_HEADER_OFFSET_PAGE_ID, &v, sizeof(v));
}
```

**取舍**：选择手动偏移而非结构体映射。

理由：
1. 跨平台一致性（无对齐差异）
2. 可以在 32 位和 64 位系统间共享数据库文件
3. 避免编译器 padding 导致的难以调试的 bug
4. 代价是代码稍冗长，但用宏可简化

### 3.2 Slotted Page 布局

```
┌─────────────────────────────────────────────────────┐
│ 0-23:     PageHeader (24 bytes)                     │
├─────────────────────────────────────────────────────┤
│ 24-?:     Slot Array (从偏移 24 向后增长)            │
│           每个 Slot = {tuple_offset(2B), tuple_size(2B)} │
│           共 4 bytes/slot                            │
├─────────────────────────────────────────────────────┤
│ ?-free_space_offset:  Free Space                    │
├─────────────────────────────────────────────────────┤
│ PAGE_SIZE-?:  Tuple Data (从页尾向前增长)             │
└─────────────────────────────────────────────────────┘
```

### 3.3 Slot 结构

```c
// Slot 大小：4 bytes
#define SLOT_SIZE 4
#define SLOT_OFFSET_TUPLE_OFFSET 0  // uint16_t
#define SLOT_OFFSET_TUPLE_SIZE   2  // uint16_t

// 删除标记：tuple_size = 0 表示已删除
```

### 3.4 SlottedPage 接口

```c
// 所有函数直接操作页面缓冲区指针（char* page_data）
slot_id_t  slotted_page_insert(char* page_data, const char* tuple_data, uint16_t tuple_size);
int        slotted_page_get_tuple(const char* page_data, slot_id_t slot_id, char* out, uint16_t* out_size);
int        slotted_page_delete_tuple(char* page_data, slot_id_t slot_id);
int        slotted_page_update_tuple(char* page_data, slot_id_t slot_id, const char* data, uint16_t size);
uint32_t   slotted_page_free_space(const char* page_data);
int32_t    slotted_page_num_tuples(const char* page_data);
```

### 3.5 插入流程

```
1. 检查 FreeSpace() >= tuple_size → 不够则返回 INVALID_SLOT_ID
2. free_space_offset += SLOT_SIZE (为 slot 腾出空间)
3. tuple 区域: free_space_offset_end -= tuple_size
4. 将 tuple_data 复制到 data + free_space_offset_end
5. 在 Slot Array 新增 Slot:
   - slot.tuple_offset = free_space_offset_end
   - slot.tuple_size = tuple_size
6. num_tuples++
```

### 3.6 删除流程

```
1. 标记 slot.tuple_size = 0 (逻辑删除)
2. 不立即回收空间
3. 空间回收策略: 碎片超过 50% 时触发碎片整理
4. 整理: 将所有存活 tuple 向页尾压缩，更新 Slot offsets
```

### 3.7 更新流程

```
1. 新 tuple 大小 <= 旧 tuple 大小 → 原地覆盖
2. 新 tuple 大小 > 旧 tuple 大小:
   a. 检查 FreeSpace + 旧 tuple 空间 >= 新 tuple 大小
   b. 标记删除旧 tuple
   c. 重新插入新 tuple
   d. 空间不够 → 返回失败 (调用方需移到新页面)
```

**取舍**：选择标记删除 + 延迟碎片整理而非立即空间回收。理由与 C++ 版一致：立即回收需移动数据且更新所有 slot offset，频繁更新场景下性能差。

---

## 4. Tuple 序列化与数据类型

### 4.1 Value 类型 (Tagged Union)

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
    char*   varchar_val;     // VARCHAR 独立分配
    size_t  varchar_len;      // VARCHAR 长度（不含 \0）
} value_t;

// 构造
value_t value_make_integer(int64_t v);
value_t value_make_float(double v);
value_t value_make_varchar(const char* s);
value_t value_make_boolean(int v);
value_t value_make_null(void);

// 生命周期
value_t value_copy(const value_t* v);   // 深拷贝（varchar_val 会 strdup）
void    value_destroy(value_t* v);      // 释放 varchar_val

// 比较
value_t value_compare(const value_t* lhs, const value_t* rhs, int op);  // op: TK_EQUAL, TK_LESS, ...

// 算术
value_t value_add(const value_t* lhs, const value_t* rhs);
value_t value_subtract(const value_t* lhs, const value_t* rhs);
value_t value_multiply(const value_t* lhs, const value_t* rhs);
value_t value_divide(const value_t* lhs, const value_t* rhs);

// 类型转换
value_t value_cast_to(const value_t* v, type_id_t target_type);

// 序列化
size_t  value_serialized_size(const value_t* v);
```

**C 语言关键点**：
- tagged union 用 enum + union 实现
- VARCHAR 通过 char* 独立分配，value_destroy 时需要 free
- value_copy 执行深拷贝（varchar_val 用 strdup）
- 比较运算返回 value_t（可能是 BOOLEAN 或 NULL）

### 4.2 Column 和 Schema

```c
typedef struct {
    char      name[64];
    type_id_t type;
    uint32_t  max_length;    // VARCHAR 最大长度
    int       nullable;
    int       is_primary_key;
    uint32_t  offset;        // 定长字段偏移 (序列化时计算)
    column_id_t column_id;
} column_t;

typedef struct {
    column_t* columns;       // 动态分配数组
    int32_t   column_count;
    size_t    fixed_length_size;
    size_t    variable_column_count;
    size_t    null_bitmap_size;
} schema_t;

// 接口
int     schema_init(schema_t* s, column_t* cols, int count);
void    schema_destroy(schema_t* s);
int     schema_get_column_by_name(const schema_t* s, const char* name, column_t* out);
size_t  schema_get_fixed_length_size(const schema_t* s);
size_t  schema_get_null_bitmap_size(const schema_t* s);
```

### 4.3 Tuple 磁盘格式

```
┌────────────┬─────────────────────┬──────────────────────┐
│ NULL Bitmap │   Fixed-Length Data │  Variable-Length Data │
│ (ceil(N/8)) │   (按定义顺序)       │  (按定义顺序)          │
│   bytes     │                     │                      │
└────────────┴─────────────────────┴──────────────────────┘

Variable-Length 每个字段格式:
┌──────────────┬──────────────────────┐
│ length (4B)  │   data (length bytes)│
└──────────────┴──────────────────────┘

NULL Bitmap: 第 i 位 = 1 表示第 i 列为 NULL
  - NULL 列不占用 Fixed/Variable 空间
```

**取舍**：选择 NULL bitmap + 定长/变长分离而非全变长（如 SQLite 记录格式）。

理由：
1. 定长字段可通过 offset 直接访问，不需遍历变长 header
2. NULL bitmap 紧凑，每列仅 1 bit 开销
3. 与 PostgreSQL、InnoDB 一致，学习价值高

### 4.4 Tuple 内存格式

```c
typedef struct {
    value_t* values;     // Value 数组 (动态分配)
    int32_t  count;      // 值数量
    rid_t    rid;        // 记录位置
} tuple_t;

int   tuple_init(tuple_t* t, int count);
void  tuple_destroy(tuple_t* t);
int   tuple_get_value(const tuple_t* t, column_id_t col, value_t* out);
int   tuple_set_value(tuple_t* t, column_id_t col, const value_t* val);
size_t tuple_serialize(const tuple_t* t, const schema_t* schema, char* buf);
int    tuple_deserialize(tuple_t* t, const schema_t* schema, const char* buf, size_t size);
```

### 4.5 RID (Record ID)

```c
typedef struct {
    page_id_t page_id;
    slot_id_t slot_id;
} rid_t;

int    rid_equal(const rid_t* a, const rid_t* b);
size_t rid_hash(const rid_t* rid);
```

---

## 5. 堆文件

### 5.1 链表式堆文件结构

```
HeapFile:
  first_page_id → Page 1 → Page 2 → Page 3 → ... → INVALID_PAGE_ID
                   (page_header.next_page_id 指向下一页)

每个页面内部: SlottedPage 布局
```

### 5.2 HeapFile 接口

```c
typedef struct {
    buffer_pool_manager_t* bpm;
    page_id_t first_page_id;
    page_id_t last_page_id;
    table_id_t table_id;
} heap_file_t;

void     heap_file_init(heap_file_t* hf, buffer_pool_manager_t* bpm,
                         page_id_t first_page_id, table_id_t table_id);
rid_t    heap_file_insert(heap_file_t* hf, const tuple_t* tuple, const schema_t* schema);
int      heap_file_delete(heap_file_t* hf, const rid_t* rid);
int      heap_file_update(heap_file_t* hf, const rid_t* rid, const tuple_t* tuple, const schema_t* schema);
int      heap_file_get_tuple(heap_file_t* hf, const rid_t* rid, const schema_t* schema, tuple_t* out);
```

### 5.3 HeapIterator

```c
typedef struct {
    buffer_pool_manager_t* bpm;
    const schema_t* schema;
    page_id_t current_page_id;
    slot_id_t current_slot;
    int       finished;
} heap_iterator_t;

int  heap_iterator_init(heap_iterator_t* it, buffer_pool_manager_t* bpm,
                        const schema_t* schema, page_id_t first_page_id);
int  heap_iterator_next(heap_iterator_t* it, tuple_t* out);  // 0=有数据, 1=结束
void heap_iterator_destroy(heap_iterator_t* it);
```

遍历逻辑：
1. 从 first_page_id 开始
2. 遍历当前页面的所有 slot（跳过 tuple_size=0 的已删除项）
3. 当前页面遍历完 → 跳到 next_page_id
4. next_page_id == INVALID_PAGE_ID → 结束

### 5.4 插入流程

```
1. 从 first_page_id 开始遍历页面链表
2. 对每个页面，检查 FreeSpace() >= tuple_size
3. 找到空间足够的页面 → 插入，返回 RID
4. 所有页面都不够 → 分配新页面
   a. bpm_new_page(&new_page_id)
   b. 初始化 SlottedPage header
   c. 将新页面链接到链表末尾
   d. 在新页面插入 tuple
```

**取舍**：简单遍历找空间在页面数很多时效率低。但初期足够，后期可添加 Free Space Map 优化。

---

## 6. Buffer Pool Manager

### 6.1 核心数据结构

```c
typedef struct {
    char      data[PAGE_SIZE];
    page_id_t page_id;
    int       pin_count;
    int       is_dirty;
    page_latch_t latch;     // 预留，当前 no-op
} page_t;

typedef struct {
    size_t pool_size;
    page_t* pages;                           // frame 数组 (大块分配)
    hashmap_t page_table;                    // page_id → frame_id 映射
    list_t  free_list;                       // 空 frame 链表
    lru_replacer_t* replacer;
    disk_manager_t* disk_manager;
} buffer_pool_manager_t;
```

### 6.2 接口

```c
int      bpm_init(buffer_pool_manager_t* bpm, size_t pool_size, disk_manager_t* dm);
void     bpm_destroy(buffer_pool_manager_t* bpm);
page_t*  bpm_fetch_page(buffer_pool_manager_t* bpm, page_id_t page_id);
page_t*  bpm_new_page(buffer_pool_manager_t* bpm, page_id_t* page_id);
int      bpm_unpin_page(buffer_pool_manager_t* bpm, page_id_t page_id, int is_dirty);
int      bpm_flush_page(buffer_pool_manager_t* bpm, page_id_t page_id);
void     bpm_flush_all(buffer_pool_manager_t* bpm);
```

### 6.3 PageGuard (C 语言手动 RAII 替代)

```c
typedef struct {
    page_t* page;
    buffer_pool_manager_t* bpm;
    int is_dirty;
    int released;    // 防止重复释放
} page_guard_t;

page_guard_t page_guard_create(page_t* page, buffer_pool_manager_t* bpm);
void         page_guard_release(page_guard_t* guard);  // 自动 Unpin
void         page_guard_mark_dirty(page_guard_t* guard);
```

**C 语言惯用模式**：
```c
int some_function(buffer_pool_manager_t* bpm, page_id_t pid) {
    page_guard_t guard = page_guard_create(bpm_fetch_page(bpm, pid), bpm);
    if (!guard.page) return DB_PAGE_NOT_FOUND;

    // 修改页面...
    page_guard_mark_dirty(&guard);

    // 使用 goto cleanup 模式确保释放
    int rc = DB_OK;
    if (some_error) {
        rc = DB_INTERNAL_ERROR;
        goto cleanup;
    }

cleanup:
    page_guard_release(&guard);
    return rc;
}
```

**取舍**：C 语言没有 RAII，使用 goto cleanup 模式是 C 语言中资源管理的最佳实践（Linux kernel 广泛使用）。PageGuard 封装了 UnpinPage 调用，减少遗漏。

### 6.4 FetchPage 流程

```
1. 在 page_table 中查找 page_id
2. 命中 (hit):
   a. pin_count++
   b. replacer_pin(frame_id)
   c. 返回 page 指针
3. 未命中 (miss):
   a. free_list 非空 → 取一个空 frame
   b. 否则 replacer_victim() 淘汰一个页面
      - 被淘汰页面 is_dirty → FlushPage 写回磁盘
      - 从 page_table 移除旧映射
   c. 从磁盘读取新页面到 frame
   d. 更新 page_table: page_id → frame_id
   e. pin_count = 1
   f. 返回 page 指针
```

### 6.5 LRU-2 替换策略

```c
typedef struct lru_node {
    frame_id_t frame_id;
    list_node_t node;       // 侵入式链表节点
} lru_node_t;

typedef struct {
    list_t  cold_list;      // 访问 1 次
    list_t  hot_list;       // 访问 >= 2 次
    hashmap_t access_count; // frame_id → access_count
    hashmap_t node_map;     // frame_id → lru_node_t*
    size_t  capacity;
} lru_replacer_t;

int   lru_replacer_init(lru_replacer_t* r, size_t capacity);
void  lru_replacer_destroy(lru_replacer_t* r);
int   lru_replacer_victim(lru_replacer_t* r, frame_id_t* frame_id);
void  lru_replacer_pin(lru_replacer_t* r, frame_id_t frame_id);
void  lru_replacer_unpin(lru_replacer_t* r, frame_id_t frame_id);
size_t lru_replacer_size(lru_replacer_t* r);
```

**淘汰优先级**：cold_list 尾部 > hot_list 尾部

**取舍**：选择 LRU-2 而非简单 LRU 或 Clock。

理由：
- LRU：全表扫描会淘汰所有缓存（scan resistance 差）
- Clock：近似 LRU，scan resistance 稍好但不如 LRU-2
- LRU-2：必须被访问两次才留在 hot list，天然抵抗扫描污染

---

## 7. B+ 树索引

### 7.1 节点结构

**内部节点**：
```
[PageHeader(24B)] [node_type=INTERNAL(1B)] [size(4B)]
[Key_0(8B)] [PageId_0(4B)] [Key_1(8B)] [PageId_1(4B)] ... [Key_n] [PageId_n]

内部节点容量: (4096 - 29) / (8 + 4) ≈ 339 个 key
max_size = 339, min_size = 170 (半满)
```

**叶子节点**：
```
[PageHeader(24B)] [node_type=LEAF(1B)] [size(4B)]
[Key_0(8B)] [Value_0(?B)] [Key_1(8B)] [Value_1(?B)] ... [Key_n] [Value_n]
[next_page_id: 在 PageHeader.next_page_id 中复用]

Value 大小:
  - 聚簇索引: 完整 Tuple (变长)
  - 二级索引: 主键值 (固定 8B)
```

### 7.2 节点操作（纯函数 + 偏移宏）

```c
// 内部节点偏移
#define INTERNAL_NODE_HEADER_SIZE  29  // PageHeader(24) + type(1) + size(4)
#define INTERNAL_KEY_OFFSET(i)     (INTERNAL_NODE_HEADER_SIZE + (i) * 12)
#define INTERNAL_CHILD_OFFSET(i)   (INTERNAL_NODE_HEADER_SIZE + (i) * 12 + 8)

// 叶子节点偏移（聚簇索引，value 变长）
#define LEAF_NODE_HEADER_SIZE      29
// 聚簇索引叶子的 value 大小可变，需要从 schema 获取
// 二级索引叶子的 value 大小固定 (8B)

// 通用操作
int32_t     node_get_size(const char* page);
void        node_set_size(char* page, int32_t size);
int         node_is_leaf(const char* page);
int64_t     node_get_key(const char* page, int index);  // 通用 key 获取
int         node_find_position(const char* page, int64_t key, int size);

// 内部节点
page_id_t   internal_get_child(const char* page, int index);
void        internal_set_child(char* page, int index, page_id_t child);
void        internal_insert_key_child(char* page, int pos, int64_t key, page_id_t child);
void        internal_remove_at(char* page, int index);

// 叶子节点
void*       leaf_get_value(const char* page, int index, uint16_t* value_size);
void        leaf_insert_key_value(char* page, int pos, int64_t key, const void* value, uint16_t value_size);
void        leaf_remove_at(char* page, int index);
page_id_t   leaf_get_next_page(const char* page);
void        leaf_set_next_page(char* page, page_id_t page_id);
```

**C 语言关键点**：
- 所有节点操作使用偏移宏 + memcpy，不依赖结构体映射
- key 统一为 int64_t (8 字节)，简化比较逻辑
- value 大小根据索引类型决定（聚簇=变长，二级=8B）

### 7.3 BPlusTree 接口

```c
typedef struct {
    int64_t key;
    const void* value;
    uint16_t value_size;
} key_value_t;

typedef int (*key_comparator_t)(int64_t a, int64_t b);

typedef struct {
    index_id_t index_id;
    page_id_t  root_page_id;
    buffer_pool_manager_t* bpm;
    key_comparator_t comparator;
    int is_unique;
} b_plus_tree_t;

int   bpt_init(b_plus_tree_t* tree, index_id_t id, buffer_pool_manager_t* bpm,
               key_comparator_t cmp, int unique);
void  bpt_destroy(b_plus_tree_t* tree);
int   bpt_find(b_plus_tree_t* tree, int64_t key, void* out_value, uint16_t* out_size);
int   bpt_find_range(b_plus_tree_t* tree, int64_t low, int64_t high,
                      key_value_t** results, int* result_count);
int   bpt_insert(b_plus_tree_t* tree, int64_t key, const void* value, uint16_t value_size);
int   bpt_remove(b_plus_tree_t* tree, int64_t key);
```

### 7.4 插入流程

```
1. bpt_find_leaf_page(key) → 找到目标叶子页面
2. 检查唯一性约束 (如果 is_unique): key 已存在 → 返回 DB_DUPLICATE_KEY
3. 在叶子节点中插入 (key, value)
4. 如果叶子节点 size > max_size → 分裂:
   a. SplitLeaf(): 将后半部分移到新叶子
   b. 新叶子的最小 key 上提到父节点
   c. 递归: 如果父节点也溢出 → SplitInternal() → 继续上提
   d. 如果根节点分裂 → 创建新根，树高度 +1
```

**分裂路径记录**（替代 C++ stack）：
```c
// 使用动态数组记录从根到叶的路径
typedef struct {
    page_id_t page_id;
    int       child_index;  // 在父节点中的位置
} tree_path_entry_t;

// vector_t path;  // 插入时构建路径
```

### 7.5 删除流程

```
1. FindLeafPage(key) → 找到目标叶子
2. 在叶子中删除 key
3. 如果删除后 size < min_size → CoalesceOrRedistribute:
   a. 尝试从左兄弟借 (Redistribute Left)
   b. 尝试从右兄弟借 (Redistribute Right)
   c. 借不了 → 与兄弟合并 (Coalesce)
   d. 合并后从父节点删除分隔 key
   e. 递归向上
   f. 根节点只剩一个子节点 → 删除根，树高度 -1
```

**取舍**：选择立即合并而非延迟合并。代码逻辑清晰，后期可改为延迟合并。

### 7.6 IndexIterator

```c
typedef struct {
    buffer_pool_manager_t* bpm;
    page_id_t current_page_id;
    int       current_index;
    int       finished;
} index_iterator_t;

int  index_iterator_init(index_iterator_t* it, buffer_pool_manager_t* bpm, page_id_t start_page, int start_index);
int  index_iterator_next(index_iterator_t* it, int64_t* out_key, void* out_value, uint16_t* out_size);
void index_iterator_destroy(index_iterator_t* it);
```

---

## 8. 系统目录 (Catalog)

### 8.1 系统表 Schema

**__tables**：
```
CREATE TABLE __tables (
    table_id    INTEGER NOT NULL,
    table_name  VARCHAR NOT NULL,
    root_page_id INTEGER,
    table_type  VARCHAR   -- "user" or "system"
);
主键: table_id, 二级索引: table_name
```

**__columns**：
```
CREATE TABLE __columns (
    column_id    INTEGER NOT NULL,
    table_id     INTEGER NOT NULL,
    column_name  VARCHAR NOT NULL,
    column_type  VARCHAR NOT NULL,
    max_length   INTEGER,
    column_offset INTEGER,
    is_nullable  BOOLEAN,
    is_primary_key BOOLEAN
);
主键: column_id, 二级索引: table_id
```

**__indexes**：
```
CREATE TABLE __indexes (
    index_id    INTEGER NOT NULL,
    index_name  VARCHAR NOT NULL,
    table_id    INTEGER NOT NULL,
    column_id   INTEGER NOT NULL,
    index_type  VARCHAR,
    is_unique   BOOLEAN,
    root_page_id INTEGER
);
主键: index_id, 二级索引: table_id, index_name
```

### 8.2 Catalog 接口

```c
typedef struct {
    table_id_t table_id;
    char       table_name[64];
    page_id_t  root_page_id;
    int        is_system_table;
} table_meta_t;

typedef struct {
    index_id_t index_id;
    char       index_name[64];
    table_id_t table_id;
    column_id_t column_id;
    int        is_unique;
    page_id_t  root_page_id;
} index_meta_t;

typedef struct {
    buffer_pool_manager_t* bpm;
    id_generator_t id_gen;

    hashmap_t table_cache;     // table_id → table_meta_t*
    hashmap_t name_to_id;      // table_name → table_id
    hashmap_t index_cache;     // index_id → index_meta_t*
    hashmap_t schema_cache;    // table_id → schema_t*
} catalog_t;

int        catalog_init(catalog_t* cat, buffer_pool_manager_t* bpm);
void       catalog_destroy(catalog_t* cat);
table_id_t catalog_create_table(catalog_t* cat, const char* name, const schema_t* schema);
int        catalog_get_table(catalog_t* cat, const char* name, table_meta_t* out);
int        catalog_drop_table(catalog_t* cat, const char* name);
int        catalog_list_tables(catalog_t* cat, vector_t* out);
index_id_t catalog_create_index(catalog_t* cat, const char* name, table_id_t tid,
                                 column_id_t col_id, int unique);
int        catalog_get_index(catalog_t* cat, const char* name, index_meta_t* out);
int        catalog_get_schema(catalog_t* cat, table_id_t tid, schema_t* out);
int        catalog_bootstrap(catalog_t* cat);  // 新建数据库时初始化系统表
```

### 8.3 Bootstrap 流程

数据库启动时：
1. 读取 Page 0 验证魔数
2. 从 __tables 系统表加载所有表元数据到 table_cache
3. 从 __columns 系统表加载列信息，构建 schema_cache
4. 从 __indexes 系统表加载索引元数据到 index_cache
5. 如果是新建数据库 → 创建 __tables/__columns/__indexes 系统表

---

## 9. SQL 解析器

### 9.1 Lexer

**Token 类型**：参见 todo.md Step 5.1 的完整枚举。

**Lexer 接口**：
```c
typedef struct {
    const char* source;
    size_t      pos;
    size_t      length;
    int         line;
    int         column;
} lexer_t;

void  lexer_init(lexer_t* lex, const char* source);
int   lexer_tokenize(lexer_t* lex, token_t** out_tokens, int* out_count);
void  lexer_destroy_tokens(token_t* tokens, int count);
```

**关键字表**：静态数组 + 线性扫描（关键字 ~60 个，线性扫描足够快）。

```c
typedef struct {
    const char*  keyword;
    token_type_t type;
} keyword_entry_t;

static keyword_entry_t KEYWORDS[] = {
    {"SELECT", TK_SELECT}, {"FROM", TK_FROM}, {"WHERE", TK_WHERE},
    {"INSERT", TK_INSERT}, {"INTO", TK_INTO}, {"VALUES", TK_VALUES},
    // ... 所有关键字
};
#define KEYWORD_COUNT (sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))
```

### 9.2 AST 定义

参见 todo.md Step 5.2 的完整定义。

**C 语言关键设计**：
- 使用 tagged union（enum + union）代替 C++ 继承
- expr_t 中的字符串使用固定大小 char[] 避免频繁 malloc
- AST 的释放需要递归遍历所有子节点
- 提供 `ast_destroy_expr(expr_t*)` 和 `ast_destroy_stmt(stmt_t*)` 函数

### 9.3 Parser (递归下降 + Pratt 优先级爬升)

```c
typedef struct {
    token_t* tokens;
    int      count;
    int      pos;
    char     error_msg[512];
    int      error_line;
    int      error_column;
    int      has_error;
} parser_t;

void    parser_init(parser_t* p, token_t* tokens, int count);
void    parser_destroy(parser_t* p);
stmt_t* parser_parse(parser_t* p);  // 成功返回 AST，失败返回 NULL
```

**表达式优先级表**：

| 优先级 | 运算符 | 结合性 |
|--------|--------|--------|
| 1 (最低) | OR | 左 |
| 2 | AND | 左 |
| 3 | NOT | 右 |
| 4 | =, <>, !=, <, <=, >, >=, IS, IN, BETWEEN, LIKE | 左 |
| 5 | +, - | 左 |
| 6 | *, / | 左 |
| 7 (最高) | 一元 -, NOT, 函数调用, 括号 | - |

### 9.4 语法错误报告

```c
typedef struct {
    int   line;
    int   column;
    char  message[256];
    char  expected[64];
    char  found[64];
} parse_error_t;

const char* parse_error_to_string(const parse_error_t* err);
```

---

## 10. 查询计划与优化

### 10.1 逻辑算子

```c
typedef enum {
    LOP_SCAN, LOP_INDEX_SCAN, LOP_FILTER, LOP_PROJECT, LOP_JOIN,
    LOP_AGGREGATE, LOP_SORT, LOP_LIMIT, LOP_INSERT, LOP_UPDATE, LOP_DELETE
} logical_op_t;

typedef struct logical_node {
    logical_op_t op;
    struct logical_node** children;
    int child_count;
    union {
        // ... 各算子特定数据 (见 todo.md)
    };
} logical_node_t;
```

### 10.2 Planner (AST → 逻辑计划)

```c
typedef struct {
    catalog_t* catalog;
} planner_t;

int              planner_init(planner_t* p, catalog_t* cat);
void             planner_destroy(planner_t* p);
logical_node_t*  planner_plan(planner_t* p, const stmt_t* stmt);
void             logical_node_destroy(logical_node_t* node);
```

### 10.3 物理算子

```c
typedef enum {
    POP_SEQ_SCAN, POP_INDEX_SCAN, POP_FILTER, POP_PROJECT,
    POP_NESTED_LOOP_JOIN, POP_HASH_JOIN,
    POP_HASH_AGGREGATE, POP_SORT, POP_LIMIT,
    POP_INSERT, POP_UPDATE, POP_DELETE
} physical_op_t;

typedef struct physical_node {
    physical_op_t op;
    struct physical_node** children;
    int child_count;
    union {
        // ... 各物理算子特定数据
    };
} physical_node_t;
```

### 10.4 RBO 优化规则

```c
typedef struct {
    // 无状态，纯函数
} optimizer_t;

logical_node_t* optimizer_optimize(optimizer_t* opt, logical_node_t* plan);
```

**规则执行顺序**（固定）：
1. MatchIndex → 将等值条件转换为 IndexScan
2. PushDownFilter → Filter 下推
3. MergeFilterScan → 合并 Filter 和 Scan
4. PruneColumns → 删除不需要的列
5. ReorderJoin → 重排连接顺序

**取舍**：选择固定顺序规则而非 Cascades 优化器框架。理由：Cacades 实现极其复杂，固定顺序 RBO 简单有效，能覆盖 80% 常见查询。

---

## 11. 查询执行引擎

### 11.1 Volcano 迭代器接口（函数指针 vtable）

```c
typedef struct executor executor_t;

struct executor {
    int   (*init)(executor_t* self);
    int   (*next)(executor_t* self, tuple_t* out);  // 0=有数据, 1=结束, <0=错误
    void  (*close)(executor_t* self);

    executor_context_t* exec_ctx;
    executor_t** children;
    int child_count;
    void* custom_data;   // 各算子的私有数据
};

typedef struct {
    catalog_t*              catalog;
    buffer_pool_manager_t*  bpm;
    transaction_t*          txn;
    lock_manager_t*         lock_mgr;
} executor_context_t;
```

**C 语言多态实现**：
```c
// 创建 SeqScan 算子
executor_t* seq_scan_executor_create(executor_context_t* ctx, heap_file_t* hf,
                                     const schema_t* schema, expr_t* predicate) {
    executor_t* e = db_malloc(sizeof(executor_t));
    e->init  = seq_scan_init;
    e->next  = seq_scan_next;
    e->close = seq_scan_close;
    e->exec_ctx = ctx;
    e->children = NULL;
    e->child_count = 0;

    seq_scan_data_t* data = db_malloc(sizeof(seq_scan_data_t));
    data->heap_file = hf;
    data->schema = schema;
    data->predicate = predicate;
    e->custom_data = data;

    return e;
}

// 使用
executor_t* scan = seq_scan_executor_create(ctx, hf, schema, pred);
scan->init(scan);
tuple_t tuple;
while (scan->next(scan, &tuple) == 0) {
    // 处理 tuple
}
scan->close(scan);
```

### 11.2 各算子实现要点

**SeqScanExecutor**：heap_iterator_t 遍历 + 谓词过滤

**IndexScanExecutor**：index_iterator_t 遍历 + 范围检查 + 二级索引回表

**HashJoinExecutor**：
```c
typedef struct {
    hashmap_t hash_table;    // hash(key) → tuple list
    column_id_t build_key;
    column_id_t probe_key;
    int build_done;
    executor_t* build_child;
    executor_t* probe_child;
    tuple_t current_probe;
    vector_t current_matches;  // 当前 probe key 的匹配列表
    int match_index;
} hash_join_data_t;
```

**HashAggregateExecutor**：
```c
typedef struct {
    hashmap_t groups;       // group_key → aggregate_state_t
    int built;              // 是否已消费所有输入
    hashmap_iterator_t iter; // 输出迭代器
    expr_t* having;
} hash_aggregate_data_t;

typedef struct {
    value_t* group_values;
    int      group_count;
    int64_t  count_val;
    double   sum_val;
    double   min_val;
    double   max_val;
    int      has_value;
} aggregate_state_t;
```

### 11.3 表达式求值器

```c
int expr_evaluate(const expr_t* expr, const tuple_t* tuple, const schema_t* schema, value_t* out);
```

**NULL 语义**（SQL 标准）：
- 算术运算：任何操作数为 NULL → 结果为 NULL
- 比较：任何操作数为 NULL → 结果为 NULL
- AND：FALSE AND NULL → FALSE; TRUE AND NULL → NULL; NULL AND NULL → NULL
- OR：TRUE OR NULL → TRUE; FALSE OR NULL → NULL; NULL OR NULL → NULL
- IS NULL / IS NOT NULL：不返回 NULL

---

## 12. 事务管理

### 12.1 事务状态机

```c
typedef enum { TXN_INVALID, TXN_ACTIVE, TXN_COMMITTED, TXN_ABORTED } txn_state_t;
typedef enum {
    ISOLATION_READ_UNCOMMITTED,
    ISOLATION_READ_COMMITTED,
    ISOLATION_REPEATABLE_READ
} isolation_level_t;

typedef struct {
    txn_id_t        txn_id;
    txn_state_t     state;
    isolation_level_t isolation_level;
    lsn_t           last_lsn;
    hashmap_t       table_lock_set;   // ResourceId → lock_mode_t
    hashmap_t       row_lock_set;     // table_id → hashmap_t(RID → lock_mode_t)
} transaction_t;
```

### 12.2 Lock Manager

**锁模式**：
```c
typedef enum { LOCK_S = 0, LOCK_X = 1, LOCK_IS = 2, LOCK_IX = 3 } lock_mode_t;
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

**锁请求队列**：
```c
typedef struct {
    txn_id_t    txn_id;
    lock_mode_t mode;
    int         granted;
} lock_request_t;

typedef struct {
    lock_request_t* requests;
    int              request_count;
    int              request_capacity;
} lock_request_queue_t;

typedef struct {
    resource_id_t resource;
    lock_request_queue_t queue;
    list_node_t hash_node;
} lock_entry_t;
```

**ResourceId**：
```c
typedef enum { RESOURCE_TABLE, RESOURCE_ROW } resource_type_t;

typedef struct {
    resource_type_t type;
    table_id_t table_id;
    rid_t      rid;       // 仅 ROW 有效
} resource_id_t;

size_t resource_id_hash(const resource_id_t* rid);
int    resource_id_equal(const resource_id_t* a, const resource_id_t* b);
```

**LockManager 接口**：
```c
typedef struct {
    hashmap_t lock_table;  // resource_id_t → lock_entry_t*
} lock_manager_t;

int  lock_manager_init(lock_manager_t* mgr);
void lock_manager_destroy(lock_manager_t* mgr);
int  lock_manager_lock_table(lock_manager_t* mgr, txn_id_t txn_id, lock_mode_t mode, table_id_t table_id);
int  lock_manager_lock_row(lock_manager_t* mgr, txn_id_t txn_id, lock_mode_t mode, table_id_t table_id, const rid_t* rid);
int  lock_manager_unlock_table(lock_manager_t* mgr, txn_id_t txn_id, table_id_t table_id);
int  lock_manager_unlock_row(lock_manager_t* mgr, txn_id_t txn_id, table_id_t table_id, const rid_t* rid);
```

### 12.3 死锁检测

```c
typedef struct {
    lock_manager_t* lock_mgr;
} deadlock_detector_t;

// 返回 0=有死锁(写victim), -1=无死锁
int deadlock_detector_detect(deadlock_detector_t* dd, txn_id_t* victim);
```

**周期检测**：DFS + 颜色标记（WHITE=0/GRAY=1/BLACK=2）

**牺牲者选择**：youngest transaction (txn_id 最大，已做工作最少)

**检测触发**：每次锁请求等待时触发

### 12.4 隔离级别实现

| 操作 | READ UNCOMMITTED | READ COMMITTED | REPEATABLE READ |
|------|-----------------|----------------|-----------------|
| 读行 | 无锁 | IS表锁 + S行锁(读完释放) | IS表锁 + S行锁(事务结束释放) |
| 写行 | IX表锁 + X行锁(事务结束释放) | IX表锁 + X行锁(事务结束释放) | IX表锁 + X行锁(事务结束释放) |

**READ COMMITTED 的 "读完释放"**：
- 读操作获取 S 锁后立即释放

**REPEATABLE READ 的 "事务结束释放"**：
- 所有锁保持到 COMMIT/ROLLBACK 时统一释放（严格 2PL）

### 12.5 TransactionManager

```c
typedef struct {
    volatile txn_id_t next_txn_id;  // 单线程无需 atomic
    hashmap_t txn_map;              // txn_id → transaction_t*
    lock_manager_t* lock_mgr;
    wal_manager_t*  wal_mgr;
} txn_manager_t;

txn_id_t txn_manager_begin(txn_manager_t* mgr, isolation_level_t level);
int      txn_manager_commit(txn_manager_t* mgr, txn_id_t txn_id);
int      txn_manager_abort(txn_manager_t* mgr, txn_id_t txn_id);
```

**Commit 流程**：
```
1. 写入 COMMIT_RECORD 到 WAL
2. 强制刷 WAL (Force-at-Commit)
3. 设置 txn 状态为 COMMITTED
4. 释放所有锁
```

**Abort 流程**：
```
1. 逆序遍历该事务的日志记录
2. 对每个 UPDATE/INSERT/DELETE 执行 Undo
3. 写入 ABORT_RECORD
4. 刷 WAL
5. 设置 txn 状态为 ABORTED
6. 释放所有锁
```

---

## 13. WAL 与 ARIES 恢复

### 13.1 WAL 日志记录

```c
typedef enum {
    LOG_BEGIN = 1, LOG_COMMIT = 2, LOG_ABORT = 3,
    LOG_UPDATE = 4, LOG_INSERT = 5, LOG_DELETE = 6,
    LOG_CHECKPOINT = 7, LOG_CLR = 8,
} log_record_type_t;

typedef struct {
    lsn_t           lsn;
    txn_id_t        txn_id;
    lsn_t           prev_lsn;
    log_record_type_t type;
    char*           payload;
    size_t          payload_size;
    uint32_t        checksum;
} log_record_t;
```

**日志文件格式**：
```
[LSN(8B)] [TxnID(8B)] [PrevLSN(8B)] [Type(4B)] [Length(4B)] [Payload] [Checksum(4B)]
```

**UPDATE_RECORD payload**：
```c
typedef struct {
    page_id_t page_id;
    slot_id_t slot_id;
    char*     before_image;   // 旧 tuple 二进制
    size_t    before_size;
    char*     after_image;    // 新 tuple 二进制
    size_t    after_size;
} update_payload_t;
```

**CLR payload**：
```c
typedef struct {
    lsn_t undo_next;  // 撤销后继续撤销的下一个 LSN
    // 其余与 UPDATE_RECORD 相同
} clr_payload_t;
```

### 13.2 WAL Manager 接口

```c
typedef struct {
    FILE*       wal_fp;
    char*       log_buffer;     // 日志缓冲
    size_t      log_buffer_size;
    size_t      log_buffer_used;
    lsn_t       next_lsn;
    lsn_t       flushed_lsn;
} wal_manager_t;

int   wal_manager_init(wal_manager_t* wm, const char* wal_file);
void  wal_manager_destroy(wal_manager_t* wm);
lsn_t wal_append(wal_manager_t* wm, const log_record_t* record);
int   wal_flush(wal_manager_t* wm, lsn_t target_lsn);
lsn_t wal_get_last_lsn(const wal_manager_t* wm);
lsn_t wal_get_flushed_lsn(const wal_manager_t* wm);
int   wal_read_records(wal_manager_t* wm, lsn_t start, log_record_t** out, int* count);
```

### 13.3 WAL 规则实施

**Write-Ahead Rule**：在 bpm_flush_page 前检查：
```c
int bpm_flush_page(buffer_pool_manager_t* bpm, page_id_t page_id) {
    page_t* page = &bpm->pages[...]; // 找到 frame
    lsn_t page_lsn = page_get_page_lsn(page->data);
    if (page_lsn > wal_get_flushed_lsn(bpm->wal_mgr)) {
        wal_flush(bpm->wal_mgr, page_lsn);
    }
    disk_manager_write_page(bpm->disk_manager, page_id, page->data);
    page->is_dirty = 0;
    return DB_OK;
}
```

### 13.4 ARIES 恢复算法

```c
typedef struct {
    txn_id_t txn_id;
    lsn_t    last_lsn;
    txn_state_t state;
} att_entry_t;   // Active Transaction Table entry

typedef struct {
    page_id_t page_id;
    lsn_t     rec_lsn;    // 首次变脏的 LSN
} dpt_entry_t;   // Dirty Page Table entry

typedef struct {
    wal_manager_t*         wal;
    buffer_pool_manager_t* bpm;
    txn_manager_t*         txn_mgr;
} recovery_manager_t;

int recovery_manager_recover(recovery_manager_t* rm);
```

#### Phase 1: Analysis

```
目标: 重建 ATT 和 DPT

1. 从最近 Checkpoint 的 CHECKPOINT_RECORD 开始
2. 顺序扫描所有日志记录:
   a. BEGIN_RECORD → 添加到 ATT
   b. COMMIT_RECORD → 从 ATT 移除
   c. ABORT_RECORD → 从 ATT 移除
   d. UPDATE/INSERT/DELETE → 更新 ATT 中该事务的 lastLSN
      - 如果 page_id 不在 DPT → 添加到 DPT, recLSN = 当前记录 LSN
   e. CHECKPOINT_RECORD → 用检查点中的 ATT/DPT 初始化
   f. CLR → 同 UPDATE
3. 扫描结束 → ATT 中剩余的是崩溃时活跃的事务
```

#### Phase 2: Redo

```
目标: 重做所有已提交和未提交事务的修改

1. 从 DPT 中最小 recLSN 开始
2. 顺序扫描日志:
   a. UPDATE/INSERT/DELETE/CLR:
      - 读取对应页面的 page_lsn
      - 如果 page_lsn >= record.lsn → 跳过 (已刷盘)
      - 否则: 重做该操作 (使用 after_image)，更新 page_lsn
   b. COMMIT/ABORT → 刷对应页面
3. Redo 结束 → 所有页面恢复到崩溃前状态
```

#### Phase 3: Undo

```
目标: 撤销所有崩溃时未提交事务的修改

1. 对 ATT 中每个活跃事务:
   a. 从 lastLSN 开始逆序扫描
   b. 对每个 UPDATE/INSERT/DELETE:
      - 使用 before_image 撤销该操作
      - 写入 CLR (记录 undo_next)
      - 继续到 prev_lsn
   c. 写入 ABORT_RECORD
2. Undo 结束 → 未提交事务完全回滚
```

### 13.5 Checkpoint

```c
typedef struct {
    att_entry_t* active_txns;
    int           active_txn_count;
    dpt_entry_t*  dirty_pages;
    int           dirty_page_count;
} checkpoint_payload_t;
```

**Sharp Checkpoint 流程**：
```
1. 拒绝新事务开始
2. 等待所有活跃事务完成
3. 刷所有脏页到磁盘 (bpm_flush_all)
4. 写入 CHECKPOINT_RECORD
5. 刷 WAL
6. 恢复接受事务
```

**取舍**：选择 Sharp Checkpoint 而非 Fuzzy Checkpoint。理由：Sharp Checkpoint 恢复时只需从检查点开始 Redo，Analysis 简单。后期可扩展为 Fuzzy Checkpoint。

---

## 14. 工具层 - REPL 与 .sql 执行

### 14.1 REPL

```c
typedef struct {
    catalog_t*          catalog;
    planner_t*          planner;
    txn_manager_t*      txn_mgr;
    isolation_level_t   current_isolation;
} repl_t;

void repl_run(repl_t* repl);
```

**支持的点命令**：
```
.help              显示帮助
.tables            列出所有用户表
.schema [table]    显示表结构
.isolation [level] 查看/设置隔离级别
.read file.sql     执行 SQL 文件
.quit              退出
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

**C 语言关键点**：
- 使用 fgets 读取用户输入
- 表格格式化：先计算每列最大宽度，再用 printf 格式化
- 点命令解析：以 '.' 开头，与 SQL 语句分开处理

### 14.2 .sql 文件执行器

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

**语句拆分规则**：
- 按 `;` 分割
- 忽略 `--` 单行注释和 `/* */` 块注释
- 字符串内的 `;` 不分割

---

## 15. 拓展阅读 - 2PL+MVCC 混合方案

> 本节为后期扩展的设计参考，当前 Phase 不实现。

### 15.1 混合策略概述

```
读操作: MVCC (多版本读, 无锁)
写操作: 2PL  (X 锁, 严格两阶段)

优势:
- 读不阻塞写, 写不阻塞读 → 高并发
- 写写冲突仍用 2PL → 简单正确
- 类似 MySQL InnoDB 的 REPEATABLE READ 实现
```

### 15.2 版本链设计

每个 tuple 增加三个隐藏字段：
```c
// 添加到 tuple 磁盘格式的末尾
txn_id_t  create_txn_id;  // 创建该版本的事务 ID
txn_id_t  delete_txn_id;  // 删除该版本的标记 (INVALID_TXN_ID = 未删除)
page_id_t prev_version;   // 指向上一个版本的指针 (undo log 链)
```

版本链方向：新版本 → 旧版本（通过 prev_version 指针）

### 15.3 可见性判断

```
Tuple 对事务 T 可见, 当且仅当:
1. create_txn_id < T.begin_ts  (在 T 开始前已提交)
   或 create_txn_id == T.txn_id  (T 自己创建的)
2. delete_txn_id == INVALID  (未被删除)
   或 delete_txn_id >= T.begin_ts  (在 T 开始后被删除, T 不可见删除)
```

### 15.4 读流程 (MVCC)

```
1. 从最新版本开始, 沿版本链向前查找
2. 对每个版本, 检查可见性:
   - 可见 → 返回该版本
   - 不可见 → 继续 prev_version
3. 所有版本都不可见 → 返回空 (行不存在)
4. 全程不需要获取任何锁
```

### 15.5 写流程 (2PL)

```
1. 获取 IX 表锁
2. 获取 X 行锁
3. 修改操作:
   - INSERT: 创建新版本, create_txn_id = 当前事务
   - DELETE: 设置当前版本 delete_txn_id = 当前事务
   - UPDATE: 旧版本设 delete_txn_id, 创建新版本
4. 旧版本数据保存在 undo log 中
```

### 15.6 版本清理 (Vacuum)

```
当某个版本:
- create_txn_id 的事务已提交
- delete_txn_id 的事务已提交
- 没有任何活跃事务可能访问该版本

→ 该版本可以被安全清理
```

### 15.7 与现有 2PL 的集成路径

```
改造步骤:
1. 在 Tuple 格式中增加 create_txn_id / delete_txn_id / prev_version
2. 修改 Schema 以支持隐藏列
3. 修改读操作: 不再获取 S 锁, 改为版本链遍历
4. 保留写操作的 X 锁逻辑
5. 实现 Vacuum 清理
6. 事务管理器增加 begin_ts 分配 (单调递增时间戳)
```

---

## 16. 拓展阅读 - 物化模型与编译执行

> 本节为学习参考，当前实现选择 Volcano 迭代器模型。

### 16.1 物化模型 (Materialization Model)

**原理**：每个算子一次性处理所有输入，返回完整结果集。

**优点**：
- 实现最简单
- 无需迭代器状态管理
- 适合查询计划深度浅的场景

**缺点**：
- 中间结果全在内存，数据量大时 OOM
- 无法流式处理
- 无法提前终止（如 LIMIT 1 需要扫描全表）

**适用场景**：嵌入式数据库、OLAP 子查询。

### 16.2 编译执行 (Compiled Execution)

**向量执行 (Vectorized Execution)**：
- 每个算子一次处理一批 (如 1024) tuple
- 内部循环对同一列数据做 SIMD 向量化运算

**JIT 编译**：
- 将查询计划编译为 LLVM IR → 优化 → 生成机器码
- 消除虚函数调用和分支预测失败
- 性能可达 Volcano 的 5-10 倍

**缺点**：
- 编译时间长 (10-100ms)
- 实现极度复杂 (需要 LLVM 依赖)
- 调试困难

**适用场景**：大型 OLAP 数据库 (DuckDB, Velox, ClickHouse)。

### 16.3 三种模型对比

| 特性 | Volcano | 物化 | 编译执行 |
|------|---------|------|---------|
| 内存占用 | 低 (一行) | 高 (全量) | 低 (一批) |
| 实现复杂度 | 中 | 低 | 极高 |
| 吞吐量 | 中 | 低 | 高 |
| 首次响应延迟 | 低 | 高 | 高 (编译) |
| 适用数据量 | 任意 | 小 | 大 |
| 代表系统 | PostgreSQL | SQLite | DuckDB, Hyper |

**本项目选择 Volcano 的理由**：在实现复杂度、学习价值、功能完整性之间取得最佳平衡。物化模型功能不足，编译执行实现成本过高。Volcano 是数据库教材的标准模型，几乎所有高级优化都可在其基础上扩展。

**C 语言适配 Volcano 的额外考量**：
- 函数指针调用的开销在 C 中比 C++ 虚函数更低（C++ 有 vtable 间接寻址 + this 指针调整）
- C 的 Volcano 实现可能比 C++ 版更快，因为函数指针直接调用、无 RTTI 开销
- 但 C 版本缺少编译器对虚函数调用的去虚拟化优化（devirtualization），手动内联更困难
