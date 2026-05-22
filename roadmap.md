# MiniSQLite (C 版) - 功能路线图

## 项目概述

用纯 C (C99/C11) 从零实现一个类 SQLite 的关系型数据库，支持 SQL 解析、查询优化与执行、B+ 树索引、Buffer Pool、事务管理（2PL + 死锁检测 + 可切换隔离级别）、WAL + ARIES 崩溃恢复、持久化存储。

本项目基于 cpp_sqlite 的设计方案，将 C++ 实现转换为纯 C 实现，功能点完全对齐。

---

## C 语言适配要点

| C++ 特性 | C 语言替代方案 |
|----------|---------------|
| class / virtual | 函数指针 + 结构体（手写 vtable） |
| std::unique_ptr | 手动 malloc/free + 所有权约定 |
| std::vector | 手写动态数组（可增长 buffer） |
| std::unordered_map | 手写哈希表（开放寻址 / 链地址法） |
| std::string | char* + 长度 + 手动内存管理 |
| std::optional | 返回值 + 标志位 / 出参指针 |
| RAII / 析构函数 | 手动 init/destroy + goto cleanup |
| 异常 / Result<T> | 错误码返回 + errno 模式 |
| 模板 / 泛型 | void* + 宏 / 为每种类型生成代码 |
| std::fstream | FILE* + fread/fwrite |
| std::mutex | 预留接口（单线程暂不启用） |

---

## Phase 0：基础设施搭建

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 0.1 | Makefile 项目骨架 | 🔴 必须 | 构建系统、目录结构、编译选项 (-std=c11 -Wall -Wextra) |
| 0.2 | 跨平台抽象层 | 🔴 必须 | 文件 I/O (FILE*)、路径、字节序、内存对齐封装 |
| 0.3 | 错误处理框架 | 🔴 必须 | 统一错误码枚举 + 错误信息函数 |
| 0.4 | 日志系统 | 🟡 重要 | 宏式 LOG_DEBUG/INFO/WARN/ERROR，printf 风格格式化 |
| 0.5 | 集成 xtest 测试框架 | 🔴 必须 | 集成 xtest_c_testinglib，TEST/EXPECT/ASSERT 宏，进程隔离 + 并行执行 |
| 0.6 | 通用数据结构 | 🔴 必须 | 动态数组(vector)、哈希表(hashmap)、双向链表(list) |
| 0.7 | 内存管理工具 | 🔴 必须 | 统一 malloc/free 包装、内存泄漏检测、缓冲区分配器 |
| 0.8 | .sql 文件执行器 | 🟡 重要 | 读取 .sql 文件并批量执行，用于测试排障 |

## Phase 1：磁盘与存储管理

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 1.1 | Disk Manager | 🔴 必须 | 页面级文件读写(FILE*)，页面分配/释放 |
| 1.2 | 页面布局 (Slotted Page) | 🔴 必须 | Header + Slot Array + Tuple Data，变长字段支持 |
| 1.3 | Tuple 序列化/反序列化 | 🔴 必须 | 定长/变长字段编码，NULL bitmap |
| 1.4 | 数据类型系统 | 🔴 必须 | INTEGER/FLOAT/VARCHAR/BOOLEAN/NULL，tagged union 实现 |
| 1.5 | 堆文件 (链表式) | 🔴 必须 | 表数据页的双向链表组织 |
| 1.6 | Free Space Management | 🔴 必须 | 空闲页面追踪与分配 |

## Phase 2：Buffer Pool 管理

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 2.1 | Buffer Pool Manager | 🔴 必须 | 页面缓存，frame 数组 + page_table 哈希表 |
| 2.2 | 页面淘汰策略 | 🔴 必须 | LRU-2 替换算法（cold_list + hot_list 双链表） |
| 2.3 | 脏页管理 | 🔴 必须 | 脏页追踪，刷盘策略 |
| 2.4 | 并发页面访问 | 🟡 重要 | 页面级读写锁接口预留（单线程 no-op，后期 pthread_rwlock） |

## Phase 3：B+ 树索引

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 3.1 | B+ 树基础操作 | 🔴 必须 | 查找、插入、删除 |
| 3.2 | B+ 树页面分裂/合并 | 🔴 必须 | 节点溢出时分裂，下溢时合并/重分布 |
| 3.3 | 聚簇索引（主键） | 🔴 必须 | 叶子节点存储完整 tuple |
| 3.4 | 二级索引 | 🔴 必须 | 叶子节点存储主键值，需回表 |
| 3.5 | 范围扫描 | 🔴 必须 | 叶子链表遍历，支持 ORDER BY |
| 3.6 | 唯一索引约束 | 🟡 重要 | INSERT/UPDATE 时唯一性检查 |

## Phase 4：系统目录 (Catalog)

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 4.1 | __tables 系统表 | 🔴 必须 | 表元数据（表名、表ID、首页面ID等） |
| 4.2 | __columns 系统表 | 🔴 必须 | 列元数据（列名、类型、偏移、是否可空等） |
| 4.3 | __indexes 系统表 | 🔴 必须 | 索引元数据（索引名、关联表、列、类型等） |
| 4.4 | Catalog 缓存 | 🟡 重要 | 内存中缓存元数据（哈希表），避免反复读盘 |
| 4.5 | 自增 ID 分配器 | 🟡 重要 | 表ID、列ID、索引ID 的自增分配 |

## Phase 5：SQL 解析器

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 5.1 | Lexer (词法分析) | 🔴 必须 | Token 类型枚举、关键字表、标识符/字面量识别 |
| 5.2 | AST 定义 | 🔴 必须 | 抽象语法树节点结构体（ tagged union / 多种结构体） |
| 5.3 | Parser (递归下降) | 🔴 必须 | DDL/DML/DQL/TCL 语法规则解析，Pratt 优先级爬升法 |
| 5.4 | 语法错误报告 | 🟡 重要 | 行号、列号、期望 token 等错误信息 |
| 5.5 | SQL 文件读取与执行 | 🟡 重要 | 读取 .sql 文件，拆分语句，依次解析执行 |

## Phase 6：查询计划与优化

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 6.1 | 逻辑计划生成 | 🔴 必须 | AST → 逻辑算子树（Scan/Filter/Project/Join/Agg/Sort） |
| 6.2 | 物理计划生成 | 🔴 必须 | 逻辑算子 → 物理算子（IndexScan/HashJoin/Sort 等） |
| 6.3 | RBO 规则优化 | 🔴 必须 | 谓词下推、投影裁剪、连接重排序 |
| 6.4 | EXPLAIN 命令 | 🟡 重要 | 输出查询计划树 |
| 6.5 | 简单统计信息 | 🔟 后期 | 表行数、列 NDV、直方图（为 CBO 准备） |
| 6.6 | CBO 基础 | 🔟 后期 | 代价估计模型 + 连接排序优化 |

## Phase 7：查询执行引擎

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 7.1 | Volcano 迭代器框架 | 🔴 必须 | Init/Next/Close 函数指针接口 |
| 7.2 | 基础算子 | 🔴 必须 | SeqScan, IndexScan, Insert, Update, Delete |
| 7.3 | Filter + Project | 🔴 必须 | WHERE 过滤与列裁剪 |
| 7.4 | 表达式求值器 | 🔴 必须 | 算术/比较/逻辑表达式 + 函数调用 |
| 7.5 | Nested Loop Join | 🔴 必须 | 最基础连接算法 |
| 7.6 | Hash Join | 🟡 重要 | 等值连接优化 |
| 7.7 | 聚合算子 | 🟡 重要 | COUNT/SUM/AVG/MAX/MIN + GROUP BY + HAVING |
| 7.8 | Sort 算子 | 🟡 重要 | ORDER BY 支持（qsort + 自定义比较器） |
| 7.9 | Limit 算子 | 🟡 重要 | LIMIT 支持 |
| 7.10 | 子查询 | 🔟 后期 | 标量子查询、IN 子查询 |

## Phase 8：事务管理

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 8.1 | 事务状态机 | 🔴 必须 | Active → Committed/Aborted 状态管理 |
| 8.2 | Lock Manager | 🔴 必须 | S/X/IS/IX 锁的请求/授予/释放 |
| 8.3 | 多粒度锁协议 | 🔴 必须 | 意向锁兼容矩阵 + 锁升级 |
| 8.4 | 死锁检测 | 🔴 必须 | Wait-For Graph + DFS 周期检测 + 受害者选择 |
| 8.5 | 隔离级别支持 | 🔴 必须 | READ UNCOMMITTED / READ COMMITTED / REPEATABLE READ |
| 8.6 | WAL 日志管理 | 🔴 必须 | LSN 分配、日志缓冲、强制刷盘 |
| 8.7 | 物理日志记录 | 🔴 必须 | PageLSN, before_image, after_image |
| 8.8 | ARIES 恢复 | 🔴 必须 | Analysis → Redo → Undo 三阶段 |
| 8.9 | Sharp Checkpoint | 🟡 重要 | 暂停事务、刷脏页、记录检查点 LSN |
| 8.10 | 2PL+MVCC 混合（设计） | 🔟 后期 | 详细设计方案（Phase 8 仅作为设计文档补充） |

## Phase 9：集成与工具

| # | 功能 | 优先级 | 说明 |
|---|------|--------|------|
| 9.1 | 交互式 REPL | 🟡 重要 | 命令行交互式 SQL 执行（readline 式输入） |
| 9.2 | .sql 文件执行 | 🟡 重要 | 原生读取 .sql 文件并批量执行 |
| 9.3 | 端到端集成测试 | 🟡 重要 | SQL → 结果的完整链路测试 |
| 9.4 | 多线程扩展点标注 | 🔟 后期 | 标注哪些模块可并行化（pthread 扩展点） |
| 9.5 | 2PL+MVCC 混合实现 | 🔟 后期 | 根据 design.md 中的方案实现 |

---

## 里程碑

| 里程碑 | 完成标志 | 涉及 Phase |
|--------|---------|------------|
| M1：存储就绪 | 能创建数据库文件，读写页面，序列化 tuple | 0, 1 |
| M2：缓存可用 | Buffer Pool 缓存热点页面，LRU-2 淘汰 | 2 |
| M3：索引可用 | B+ 树支持 CRUD，范围扫描 | 3 |
| M4：元数据就绪 | 系统目录持久化，CREATE/DROP TABLE 可用 | 4 |
| M5：SQL 可解析 | DDL/DML/DQL 语句解析为 AST | 5 |
| M6：查询可执行 | SELECT/INSERT/UPDATE/DELETE 端到端跑通 | 6, 7 |
| M7：事务安全 | 2PL + WAL + ARIES，崩溃恢复可用 | 8 |
| M8：完整可用 | REPL + .sql 文件执行 + 集成测试通过 | 9 |

---

## 执行依赖关系

```
Phase 0 ──→ Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4
                                                      │
                                           Phase 5 ──→ Phase 6 ──→ Phase 7 ──→ Phase 8 ──→ Phase 9
```

- Phase 0-4 可顺序执行（存储和元数据是基础）
- Phase 5-7 在 Phase 4 后开始（解析和执行依赖元数据）
- Phase 8 在 Phase 7 后（事务管理需要完整执行引擎）
- Phase 9 最后（集成和工具）
