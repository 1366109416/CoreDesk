# CoreDesk v1.0 - Codex 可执行技术设计与实施规范

> 文档状态：Implementation Specification v1.0  
> 目标：C++ 客户端秋招项目 / Codex 实施输入  
> 核心原则：标准 C++20 Core + Qt 薄平台层


# 0. 文档定位与使用方式

本文件是 **CoreDesk v1.0 的规范性实现文档（normative implementation specification）**，目标是让 Codex、其他代码生成工具或人工开发者在尽量少的自由解释空间下完成同一个工程。

本文使用以下规范词：

- **MUST / 必须**：不允许偏离；若无法实现，必须停止对应里程碑并记录阻塞原因。
- **SHOULD / 应该**：默认执行；只有明确技术原因才能偏离，并需记录原因。
- **MAY / 可选**：不影响 v1.0 验收。

给 Codex 使用时，应把本文件放在仓库根目录 `docs/COREDESK_SPEC.md`，并要求它 **先完整读取，再按里程碑实现**。禁止让 Codex“一次性完成所有功能”。

### Codex 执行总原则

1. 每次只实现一个里程碑（M0、M1、M2……）。
2. 每个里程碑完成后必须：配置、编译、测试、运行最小验收用例、更新 `IMPLEMENTATION_STATUS.md`。
3. 未通过当前里程碑 Definition of Done（DoD）前，不得进入下一里程碑。
4. 本规范中的“固定技术决策”和“非目标”优先级高于 Codex 自己的工程偏好。
5. 禁止为了让测试通过而删除、弱化或跳过测试。
6. 禁止自行增加大型依赖、数据库、服务端框架、云服务、登录系统或复杂 UI。
7. 若实现中发现规范存在真正冲突，应暂停该点并在 `IMPLEMENTATION_STATUS.md` 写明冲突，不得静默改设计。


# 1. 项目一句话定义

**CoreDesk 是一个以标准 C++20 为核心的跨平台桌面文件索引与局域网传输客户端。**

用户可以选择一个本地目录，后台服务进程递归扫描并建立文件名索引；桌面进程通过本地 IPC 查询后台服务并实时展示搜索结果；用户还可通过 TCP 将单个文件发送到局域网另一台 CoreDesk 实例。

该项目的求职价值不在 UI，而在以下工程能力：

- 现代 C++：RAII、值语义/所有权、STL、移动语义、接口设计。
- 并发：线程池、生产者-消费者、取消、任务生命周期、锁粒度。
- 操作系统：进程、IPC、文件系统、文件 IO、后台服务。
- 网络：TCP 字节流、自定义帧协议、半包/粘包、超时、断线处理、分块传输。
- 数据结构：倒排索引、前缀索引、LRU Cache、排序与 Top-K。
- 工程：CMake、多 target、测试、日志、错误处理、性能基准、Crash 定位。
- 客户端：Qt 事件循环、薄 UI、UI/后台线程隔离、跨平台适配。


# 2. v1.0 范围与明确非目标

### 2.1 v1.0 必须完成

- Windows x64 和 Linux x64 **至少有一个平台完整可运行**；工程结构必须为第二个平台保留适配路径。
- 纯 C++20 核心库：文件模型、并发、扫描、索引、搜索、缓存、帧协议。
- 独立后台服务进程 `coredesk_service`。
- Qt Widgets 桌面进程 `coredesk_desktop`，只负责交互与展示。
- 桌面进程与服务进程通过 `QLocalServer / QLocalSocket` IPC。
- 手动选择根目录并执行全量扫描。
- 支持文件名/路径搜索，结果按规则排序，最多返回 100 条。
- 扫描任务支持进度、取消、错误统计。
- 基于 TCP 的局域网单文件发送：手动输入 IP 与端口、分块传输、临时文件、完成校验。
- 单元测试 + 至少两个集成测试 + 性能基准 CLI。
- README、架构图、线程模型、协议说明、性能数据和至少一次问题复盘。

### 2.2 v1.0 明确不做

以下项目 **MUST NOT** 被 Codex 擅自加入 v1.0：

- 不做文件内容全文索引。
- 不做双向自动文件夹同步、冲突合并或版本历史。
- 不做云端、账号、登录、服务器、P2P 穿透。
- 不做自动设备发现；局域网目标地址手工输入。
- 不做 TLS/加密认证；v1.0 仅用于可信局域网演示，严禁暴露到公网。
- 不做数据库持久化；服务重启后允许重新扫描。
- 不做复杂主题、动画、Dock、多标签页、插件系统。
- 不做 Android 主程序；Android NDK/JNI 仅为未来岗位定向扩展。
- 不引入 Boost、SQLite、spdlog、Abseil 等未列入“允许依赖”的库。
- 不追求 macOS v1.0 正式支持。


# 3. 固定技术决策（Codex 不得替换）

| 决策项 | 固定方案 | 原因 |
|---|---|---|
| C++ 标准 | C++20 | 兼顾现代特性与编译器可用性 |
| GUI | Qt 6 Widgets | UI 只做薄层，跨平台稳定 |
| 本地 IPC | `QLocalServer` + `QLocalSocket` | Qt 在 Windows 使用命名管道、Unix 使用本地域套接字，跨平台统一 |
| LAN 网络 | `QTcpServer` + `QTcpSocket` | 事件驱动、跨平台、避免重复造平台 socket adapter |
| 核心并发 | `std::thread` + `std::mutex` + `std::condition_variable` | 面试价值高，核心不依赖 QtConcurrent |
| JSON | nlohmann/json | 只用于控制消息 payload |
| 测试 | GoogleTest | 模块级单测与集成测试 |
| 构建 | CMake target-based | 禁止全局 `include_directories()` / `link_directories()` 风格 |
| 索引持久化 | v1.0 不持久化 | 控制范围，聚焦 C++/系统/网络 |
| 网络完整性 | SHA-256（Qt `QCryptographicHash` 可用于网络适配层） | 避免整个文件驻留内存 |
| UI/核心边界 | Qt 类型不得进入 core/index/filesystem/concurrency 公共头文件 | 保持核心纯 C++ |

### 3.1 允许的第三方依赖

**仅允许：**

- Qt 6：`Core`, `Widgets`, `Network`
- nlohmann/json
- GoogleTest（仅测试 target）

任何新增第三方依赖都必须先修改本规范，并说明不可替代原因。


# 4. 系统架构

```text
+----------------------------------------------------------+
| coredesk_desktop (Qt Widgets process)                    |
|  MainWindow / SearchView / TransferView                  |
|  仅负责：用户输入、展示、启动服务、IPC client            |
+------------------------------+---------------------------+
                               |
                     QLocalSocket + FrameProtocol
                               |
+------------------------------v---------------------------+
| coredesk_service (QCoreApplication process)              |
|  LocalIpcServer  -> ServiceController                    |
|                         |                                |
|              +----------+-----------+                    |
|              |                      |                    |
|        IndexManager             TransferManager          |
|              |                      |                    |
|     FileScanner/SearchEngine      QTcpServer/Socket      |
|       ThreadPool/LruCache            LAN TCP             |
+--------------+-------------------------------------------+
               |
         std::filesystem
               |
       Windows/Linux filesystem
```

### 4.1 架构边界

- `coredesk_desktop` **不得**直接扫描目录、构建索引或进行大文件传输。
- `coredesk_service` 是长生命周期后台进程，持有当前索引快照和传输状态。
- `coredesk_core` 及其下属纯 C++ 模块 **不得包含任何 Qt header**。
- Qt 只允许存在于 `apps/`、`adapters/qt_ipc/`、`adapters/qt_network/`、`ui/`。
- 搜索期间如果新扫描正在进行，旧索引继续服务；新索引必须完整构建后一次性交换，禁止“边扫描边修改当前索引”。


# 5. 仓库与 target 结构

```text
CoreDesk/
├─ CMakeLists.txt
├─ CMakePresets.json                 # SHOULD
├─ README.md
├─ IMPLEMENTATION_STATUS.md
├─ docs/
│  ├─ COREDESK_SPEC.md               # 本规范 Markdown 版
│  ├─ ARCHITECTURE.md
│  └─ PROTOCOL.md
├─ cmake/
│  ├─ CompilerWarnings.cmake
│  └─ Sanitizers.cmake
├─ include/coredesk/
│  ├─ common/
│  │  ├─ Error.h
│  │  ├─ Result.h
│  │  ├─ Types.h
│  │  └─ Cancellation.h
│  ├─ concurrency/
│  │  └─ ThreadPool.h
│  ├─ filesystem/
│  │  ├─ FileRecord.h
│  │  └─ FileScanner.h
│  ├─ index/
│  │  ├─ IndexSnapshot.h
│  │  ├─ IndexBuilder.h
│  │  ├─ SearchEngine.h
│  │  └─ LruCache.h
│  ├─ protocol/
│  │  ├─ MessageTypes.h
│  │  ├─ Frame.h
│  │  └─ FrameCodec.h
│  └─ service/
│     └─ ServiceController.h
├─ src/
│  ├─ common/
│  ├─ concurrency/
│  ├─ filesystem/
│  ├─ index/
│  ├─ protocol/
│  └─ service/
├─ adapters/
│  ├─ qt_ipc/
│  │  ├─ LocalIpcServer.h/.cpp
│  │  └─ LocalIpcClient.h/.cpp
│  └─ qt_network/
│     ├─ TcpTransferServer.h/.cpp
│     └─ TcpTransferClient.h/.cpp
├─ apps/
│  ├─ service/main.cpp
│  ├─ desktop/main.cpp
│  └─ cli/main.cpp                    # 开发/调试 CLI，MUST
├─ ui/
│  ├─ MainWindow.h/.cpp
│  ├─ SearchWidget.h/.cpp
│  └─ TransferWidget.h/.cpp
├─ tests/
│  ├─ unit/
│  └─ integration/
└─ benchmarks/
   ├─ bench_search.cpp
   └─ bench_scan_model.cpp
```

### 5.1 CMake target 名称固定

- `coredesk_common`
- `coredesk_concurrency`
- `coredesk_filesystem`
- `coredesk_index`
- `coredesk_protocol`
- `coredesk_service_lib`
- `coredesk_qt_ipc`
- `coredesk_qt_network`
- `coredesk_cli`
- `coredesk_service`
- `coredesk_desktop`
- `coredesk_tests`
- `coredesk_bench_search`

禁止把所有 `.cpp` 塞入单一 executable target。


# 6. 构建与开发环境约束

### 6.1 最低工程约束

- CMake：3.24 或更高。
- C++：`CMAKE_CXX_STANDARD 20`，`CMAKE_CXX_STANDARD_REQUIRED ON`，关闭 compiler extensions。
- Qt：Qt 6.x；代码 SHOULD 避免依赖非常新的小版本专有 API。
- Windows：MSVC 2022 系列。
- Linux：GCC 12+ 或 Clang 15+。

### 6.2 CMake 规则

- MUST 使用 `target_include_directories()` / `target_link_libraries()` / `target_compile_features()`。
- MUST 将 public include 暴露为 `PUBLIC`/`INTERFACE`，实现依赖用 `PRIVATE`。
- MUST 提供选项：

```cmake
COREDESK_BUILD_UI=ON
COREDESK_BUILD_TESTS=ON
COREDESK_BUILD_NETWORK=ON
COREDESK_ENABLE_ASAN=OFF
COREDESK_ENABLE_TSAN=OFF
COREDESK_WARNINGS_AS_ERRORS=OFF
COREDESK_FETCH_DEPS=ON
```

- Qt 用 `find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)`。
- nlohmann/json 与 GoogleTest：优先 `find_package(... CONFIG QUIET)`；未找到且 `COREDESK_FETCH_DEPS=ON` 时可使用 FetchContent。

### 6.3 标准验证命令

Linux / Ninja 或 Make：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCOREDESK_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Windows multi-config：

```powershell
cmake -S . -B build -DCOREDESK_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```


# 7. 编码规范与全局约束

- namespace 固定为 `coredesk`，子模块可使用 `coredesk::index` 等。
- 类型/类：`PascalCase`；函数/变量：`snake_case`；私有成员后缀 `_`。
- 所有 owning resource MUST 由 RAII 对象持有。
- 禁止 owning raw pointer；裸指针只允许非 owning 短生命周期观察。
- 公共接口优先 `std::string_view`、`std::span`（若数据所有权不转移）。
- 文件路径核心类型固定为 `std::filesystem::path`；**禁止在 core 内随意 `path.string()` 作为跨平台 UTF-8**。
- Qt `QString/QByteArray/QFile/QThread` 等不得出现在纯 C++ 公共接口。
- filesystem 操作优先使用带 `std::error_code` 的 overload，避免不可控异常穿越线程边界。
- 工作线程的 task 不应向线程池边界抛出异常；最外层必须 catch 并转换为结构化错误。
- 禁止 global mutable singleton。日志器若做全局访问，必须封装为可替换的进程级 service，并不得影响单元测试。
- 所有网络/IPC 输入都必须做长度上限、枚举值和 JSON schema 基础校验。


# 8. 核心数据模型

### 8.1 公共类型

```cpp
namespace coredesk {
using FileId = std::uint64_t;
using IndexGeneration = std::uint64_t;
using RequestId = std::uint64_t;

enum class EntryType : std::uint8_t {
    RegularFile,
    Directory,
    Symlink,
    Other
};
}
```

### 8.2 FileRecord

```cpp
struct FileRecord {
    FileId id{};
    std::filesystem::path absolute_path;
    std::filesystem::path relative_path;
    std::filesystem::path file_name;
    std::filesystem::path extension;
    std::uintmax_t size_bytes{};              // 非 regular file 为 0
    std::filesystem::file_time_type modified_time{};
    EntryType type{EntryType::Other};
};
```

约束：

- `absolute_path` MUST 是扫描根目录下的绝对/规范化可用路径；无法 canonical 时允许 lexical-normalized absolute path。
- `relative_path` 相对扫描 root，用于 UI 展示和网络元数据。
- `id` 在单次 IndexSnapshot 内唯一即可；v1.0 不要求跨重启稳定。
- v1.0 搜索只针对 **文件名、扩展名、相对路径字符串**，不读取文件内容。

### 8.3 Result / Error

不得引入 C++23 `std::expected`。实现轻量结果类型：

```cpp
enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    PathNotFound,
    PermissionDenied,
    Busy,
    Cancelled,
    IndexNotReady,
    ProtocolError,
    PayloadTooLarge,
    ConnectionFailed,
    Timeout,
    TargetExists,
    IoError,
    HashMismatch,
    InternalError
};

struct Error {
    ErrorCode code{ErrorCode::Ok};
    std::string message;
};

template <class T>
class Result; // 持有 T 或 Error；必须提供 ok()/value()/error()
```

`Result<void>` MUST 有专门实现或等效形式。


# 9. 并发模块规范

### 9.1 ThreadPool 公共接口

```cpp
class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t thread_count,
                        std::size_t max_queue_size = 4096);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool submit(Task task);      // 队列满时等待；shutdown 后返回 false
    void wait_idle();            // 队列为空且 active_count == 0
    void shutdown();             // 幂等；唤醒并 join 全部线程

    std::size_t thread_count() const noexcept;
};
```

### 9.2 ThreadPool 内部不变量

- 队列 MUST 有上限，默认 4096，防止百万文件扫描时产生百万 lambda 占满内存。
- `submit` 在队列满时可阻塞等待空间；shutdown 后不得继续接收任务。
- worker 取 task 前后维护 `active_count_`。
- `wait_idle()` 条件必须是：`tasks_.empty() && active_count_ == 0`。
- `shutdown()` MUST 幂等；析构函数调用 `shutdown()`。
- worker 的最终保护层 MUST `catch (...)`，但异常不得静默丢失，应调用可注入的 error callback 或至少写入测试可观察计数。

### 9.3 Cancellation

```cpp
class CancellationToken {
public:
    bool is_cancelled() const noexcept;
private:
    std::shared_ptr<std::atomic_bool> flag_;
};

class CancellationSource {
public:
    CancellationToken token() const;
    void cancel() noexcept;
};
```

扫描任务必须至少在：目录枚举循环、每个 metadata task 开始、索引构建批次之间检查取消。


# 10. FileScanner 规范

### 10.1 行为

`FileScanner` 负责从 root 枚举文件系统并生成 `FileRecord`，不负责搜索、不持有全局索引。

```cpp
struct ScanOptions {
    bool include_dot_hidden{false};
    bool follow_directory_symlinks{false};
    std::size_t worker_count{0}; // 0 => max(2, hardware_concurrency)
};

struct ScanProgress {
    std::uint64_t discovered{};
    std::uint64_t processed{};
    std::uint64_t skipped{};
    std::uint64_t failed{};
};

struct ScanOutput {
    std::filesystem::path root;
    std::vector<FileRecord> records;
    ScanProgress stats;
    std::chrono::milliseconds elapsed{};
};

using ProgressCallback = std::function<void(const ScanProgress&)>;

class FileScanner {
public:
    Result<ScanOutput> scan(const std::filesystem::path& root,
                            const ScanOptions& options,
                            CancellationToken token,
                            ProgressCallback progress);
};
```

### 10.2 具体扫描规则

- root 不存在或不是目录：`InvalidArgument/PathNotFound`。
- 使用 `std::filesystem::directory_options::skip_permission_denied`。
- 默认不跟随 directory symlink，避免环路。
- v1.0 “hidden”定义为文件名以 `.` 开头；不额外读取 Windows Hidden Attribute。
- regular file 读取 size；目录和其它类型 size = 0。
- 单个文件 metadata 失败：计入 `failed`，扫描整体继续；根目录本身不可访问才整体失败。
- 每 250 ms **最多**触发一次 progress callback，避免 IPC/UI 被进度消息淹没。

### 10.3 并发方式固定

- 一个枚举线程顺序遍历目录。
- 枚举到 entry 后把“构造 FileRecord”的任务提交到 bounded ThreadPool。
- worker 生成 FileRecord 后追加到 thread-safe collector。
- 枚举结束后 `wait_idle()`，再一次性取出 records。
- **禁止**多个线程同时操作同一个 `recursive_directory_iterator`。

### 10.4 路径与 Unicode

- core 内保持 `std::filesystem::path`，不要假设 `.string()` 是 UTF-8。
- 发送给 UI/JSON 时必须调用 adapter 层的 `PathCodec`。
- Windows：Qt adapter 通过 native wide string 转 `QString`；Linux：通过 native bytes 按 UTF-8 转换。
- v1.0 不实现完整 Unicode case folding；ASCII 字母大小写不敏感，非 ASCII 字节保持原样。


# 11. IndexSnapshot / IndexBuilder 规范

### 11.1 不可变快照

```cpp
struct PostingList {
    std::vector<FileId> ids;
};

struct IndexSnapshot {
    IndexGeneration generation{};
    std::filesystem::path root;
    std::vector<FileRecord> records;
    std::unordered_map<FileId, std::size_t> id_to_pos;
    std::unordered_map<std::string, PostingList> token_index;
    std::vector<std::string> sorted_tokens;
    std::vector<std::string> normalized_names; // 与 records 同下标
};
```

构建完成后 snapshot 视为不可变。搜索线程不得修改它。

### 11.2 Tokenizer 固定规则

- 输入来源：file name、extension、relative path 的文本表示。
- ASCII `A-Z` 转小写。
- 以下字符视为分隔符：空格、`.`、`_`、`-`、`/`、`\\`、`()[]{} `。
- 连续分隔符合并。
- 非 ASCII UTF-8 字节不做 Unicode 归一化，保持在 token 内。
- token 长度 1..128 bytes；超长 token 截断或丢弃，行为必须有测试。

### 11.3 FileId

v1.0 使用构建顺序 `1..N` 递增分配。禁止使用未经碰撞处理的 path hash 作为唯一主键。

### 11.4 IndexBuilder

```cpp
class IndexBuilder {
public:
    Result<std::shared_ptr<const IndexSnapshot>> build(
        IndexGeneration generation,
        ScanOutput scan,
        CancellationToken token);
};
```

构建完成后：

- posting list 去重并排序。
- `sorted_tokens` 排序且唯一。
- `normalized_names.size() == records.size()`。
- 若取消，返回 `Cancelled`，不得产生半成品 snapshot。


# 12. SearchEngine 与 LRU 规范

### 12.1 搜索 API

```cpp
struct SearchRequest {
    std::string query_utf8;
    std::size_t limit{100}; // 上限 100
};

struct SearchHit {
    FileId id{};
    int score{};
};

struct SearchResponse {
    IndexGeneration generation{};
    std::vector<SearchHit> hits;
    std::chrono::microseconds elapsed{};
    bool from_cache{false};
};

class SearchEngine {
public:
    Result<SearchResponse> search(
        const IndexSnapshot& snapshot,
        const SearchRequest& request);
    void clear_cache();
};
```

### 12.2 搜索策略固定

1. query trim 后为空：返回空结果，不报错。
2. query 最大 256 bytes；超过返回 `InvalidArgument`。
3. 用同一 tokenizer 得到 query tokens。
4. 对每个 token：
   - 先查 `token_index` 完全匹配；
   - 若没有完全匹配，使用 `sorted_tokens + lower_bound` 查所有具有此前缀的 token，合并 posting；
5. 多 token 采用 **AND 语义**：取交集。
6. 如果索引候选为空，再对 `normalized_names` 做 substring fallback；最多收集 100 个，不必遍历后排序全部结果。
7. 评分固定：
   - 完整文件名精确匹配：100
   - 文件名以前缀开头：80
   - token 精确命中：60
   - token 前缀命中：50
   - substring fallback：30
8. 同分排序：文件名长度更短优先，再按相对路径字典序。
9. limit 最大 100，默认 100。

### 12.3 LRU Cache

- key = 规范化 query + limit。
- capacity 固定默认 128。
- 每次新 snapshot 成功交换后 MUST `clear_cache()`。
- Cache 自身可用一个 mutex；不要把锁延伸到真实搜索过程。
- LRU 必须通过 `unordered_map + list` 实现 O(1) get/put，作为项目数据结构证据。


# 13. 服务进程与一致性模型

### 13.1 ServiceController 状态

```text
NoIndex ----scan----> Scanning ----success----> Ready
   ^                    |   ^                     |
   |                    |   |                     |
   +----cancel/fail-----+   +----new scan*--------+

* v1.0 在 Scanning 状态收到第二个 ScanRequest：返回 Busy；UI 必须先 Cancel。
```

### 13.2 快照交换

`ServiceController` 持有：

```cpp
mutable std::shared_mutex snapshot_mutex_;
std::shared_ptr<const IndexSnapshot> current_snapshot_;
IndexGeneration next_generation_{1};
```

搜索流程：

1. shared lock；复制 `shared_ptr`；立即 unlock。
2. 在锁外执行搜索。

扫描完成流程：

1. 完整构建新 snapshot。
2. unique lock；交换 `current_snapshot_`；unlock。
3. 清搜索缓存。

这样搜索不会长时间持有全局锁。

### 13.3 扫描期间搜索语义

- 若已经有上一代 snapshot：继续使用旧 generation，响应中 `stale=true`。
- 第一次扫描且没有 snapshot：返回 `IndexNotReady`。
- 扫描失败不影响旧 snapshot。


# 14. 统一帧协议（IPC 与 TCP 共用）

### 14.1 二进制 Header（固定 24 bytes）

所有多字节整数使用 network byte order（big endian）。

```text
Offset  Size  Field
0       4     magic = ASCII "CDSK"
4       2     version = 1
6       2     message_type
8       4     flags
12      8     request_id
20      4     payload_length
-----------------------------
Total: 24 bytes
```

- `payload_length` 最大 1 MiB；`FileChunk` 例外仍必须 <= 1 MiB。
- 收到 magic/version 不正确：ProtocolError，关闭该连接。
- 收到 payload_length 超限：PayloadTooLarge，关闭连接。
- decoder 必须支持：一个 read 只有半个 header、半个 payload、多个 frame 连在一次 read 中。

### 14.2 FrameCodec API

```cpp
enum class MessageType : std::uint16_t;

struct Frame {
    MessageType type{};
    std::uint32_t flags{};
    RequestId request_id{};
    std::vector<std::byte> payload;
};

class FrameEncoder {
public:
    static Result<std::vector<std::byte>> encode(const Frame& frame);
};

class FrameDecoder {
public:
    Result<std::vector<Frame>> push(std::span<const std::byte> bytes);
    void reset();
};
```

### 14.3 MessageType 数值固定

```text
1   Ping
2   Pong
10  ScanRequest
11  ScanAccepted
12  ScanProgress
13  ScanCompleted
14  ScanFailed
15  CancelScanRequest
20  SearchRequest
21  SearchResponse
30  StatusRequest
31  StatusResponse
100 Hello
101 HelloAck
110 FileOffer
111 FileAccept
112 FileReject
113 FileChunk
114 FileFinish
115 FileResult
```

未识别 message_type：返回 ProtocolError；不得把未知数字直接 cast 后执行。


# 15. IPC JSON Payload 规范

JSON 文本编码固定 UTF-8。字段名固定，不得擅自改为其它命名风格。

### 15.1 ScanRequest

```json
{
  "root": "D:/Documents",
  "include_dot_hidden": false,
  "follow_directory_symlinks": false,
  "worker_count": 0
}
```

### 15.2 ScanProgress

```json
{
  "scan_id": "1",
  "discovered": 12000,
  "processed": 11820,
  "skipped": 123,
  "failed": 57,
  "elapsed_ms": 950
}
```

### 15.3 ScanCompleted

```json
{
  "scan_id": "1",
  "generation": 3,
  "file_count": 11820,
  "elapsed_ms": 1420
}
```

### 15.4 SearchRequest

```json
{
  "query": "project report",
  "limit": 100
}
```

### 15.5 SearchResponse

```json
{
  "generation": 3,
  "stale": false,
  "elapsed_us": 730,
  "from_cache": false,
  "results": [
    {
      "name": "project_report.docx",
      "path": "D:/Documents/work/project_report.docx",
      "relative_path": "work/project_report.docx",
      "size": 124334,
      "modified_ms": 1760000000000,
      "type": "file",
      "score": 100
    }
  ]
}
```

### 15.6 Error 响应

请求型消息失败时，使用对应 `*Failed` 或统一 StatusResponse，至少包含：

```json
{
  "ok": false,
  "code": "PERMISSION_DENIED",
  "message": "Cannot access root directory"
}
```

用户可见 message 要简洁；日志中可记录更详细 system error。


# 16. Qt 本地 IPC Adapter

### 16.1 组件

- Service：`LocalIpcServer` 包装 `QLocalServer`。
- Desktop：`LocalIpcClient` 包装 `QLocalSocket`。
- 两端都只把原始 bytes 交给纯 C++ `FrameDecoder`，不得在 Qt 层另写一套协议解析。

### 16.2 Server name

固定：`CoreDesk.Service.v1`。

启动逻辑：

1. 尝试 `listen(name)`。
2. 若失败且疑似已有实例：先用客户端探测是否可连接。
3. 若可连接：当前 service 退出，认为已有实例。
4. 若不可连接且平台允许清理 stale local endpoint：清理后重试一次。
5. 禁止无条件删除正在运行实例的 endpoint。

### 16.3 Desktop 启动服务

1. Desktop 首先连接 local service。
2. 连接失败则用 `QProcess::startDetached` 启动同目录下 `coredesk_service`。
3. 每 100 ms 重试，最多 3 秒。
4. 仍失败：UI 显示错误并保留“重试”按钮，不得直接崩溃。

### 16.4 IPC request correlation

- Desktop 的 `RequestId` 单调递增，从 1 开始。
- response 必须复制 request_id。
- progress event 可使用原 ScanRequest 的 request_id 或单独 scan_id；实现必须保持一致并有测试。


# 17. Desktop UI 精确范围

### 17.1 MainWindow 只包含以下区域

```text
+----------------------------------------------------------------+
| CoreDesk                                                       |
+----------------------------------------------------------------+
| Root: [....................................] [Browse] [Scan]    |
| Search: [....................................................]  |
+----------------------------------------------------------------+
| Name              Relative Path             Size     Modified  |
| ...                                                            |
+----------------------------------------------------------------+
| Status: Ready | 102421 files | search 1.2 ms | generation 3   |
+----------------------------------------------------------------+
| [Search] [LAN Transfer] tabs                                   |
+----------------------------------------------------------------+
```

### 17.2 UI 行为

- 搜索输入 debounce 150 ms 后发送 SearchRequest。
- 搜索结果最多展示 100 行。
- 扫描按钮在 Scanning 时 disabled；显示 Cancel。
- progress UI 更新频率不得超过 service progress 的实际频率。
- 双击结果可调用 `QDesktopServices` 打开文件或所在目录，此功能 MAY。
- UI 不保存或持有完整索引，不做本地二次搜索。
- UI 不直接创建 std::thread；异步依赖 Qt event loop + service。
- 样式保持默认或轻量，不投入自定义动画。


# 18. LAN 文件传输规范

### 18.1 v1.0 定义

“同步”在 v1.0 中明确等价于：**用户手动选择一个 regular file，发送到指定 IP:port 的另一台 CoreDesk；接收端写入用户设置的 receive directory。**

自动目录同步属于 v1.1+，不得混入 v1.0。

### 18.2 网络模型

- Service 内部可启用 `QTcpServer`，默认监听端口建议 45827；允许 UI 修改。
- 默认只在用户显式开启“Enable LAN Transfer”后监听。
- 客户端手动输入目标 IP 和 port。
- v1.0 单 service 同时最多进行 1 个发送 + 1 个接收；其它请求返回 Busy。
- 所有 socket IO 走 Qt event loop；不得使用 blocking `waitFor...()` 作为正常数据通道。

### 18.3 控制消息

`Hello` JSON：

```json
{ "protocol_version": 1, "node_name": "MyPC" }
```

`FileOffer`：

```json
{
  "transfer_id": "32-lowercase-hex-chars",
  "file_name": "report.pdf",
  "file_size": 123456789,
  "chunk_size": 262144,
  "sha256": "64-lowercase-hex-chars"
}
```

`FileAccept`：

```json
{ "transfer_id": "...", "start_offset": 0 }
```

v1.0 `start_offset` 必须为 0；断点续传 v1.1 再开放非零。

### 18.4 FileChunk payload（二进制）

```text
32 bytes  transfer_id ASCII hex
8 bytes   offset (big endian uint64)
4 bytes   data_length (big endian uint32)
N bytes   file data
```

- 默认 `chunk_size = 256 KiB`。
- 每个 chunk data_length <= 1 MiB。
- 接收端要求 offset 等于期望的 next_offset；不一致则 ProtocolError / transfer failed。

### 18.5 安全写文件

- 网络侧只接受 `file_name` basename，拒绝含 `/`, `\\`, `..` 的名字。
- 接收临时文件：`<receive_dir>/<file_name>.coredesk.part`。
- 最终目标已存在：v1.0 返回 `TargetExists`，不覆盖。
- 完成后检查：已写 size == file_size，SHA-256 == offer hash。
- 校验通过后 rename 临时文件为最终文件。
- 失败/取消后 SHOULD 删除 `.part`；如果删除失败记录日志。
- 任何时候禁止一次性把整个文件读入内存。

### 18.6 安全边界

v1.0 **没有 TLS、身份认证或公网威胁模型**。README 必须明确：LAN Transfer 仅用于可信局域网开发/演示，不能直接暴露到互联网。


# 19. 线程模型与生命周期

```text
Desktop Process
  Qt UI Thread
      |
      +-- QLocalSocket (event driven)

Service Process
  Qt Main/Event Thread
      |-- QLocalServer / connected local sockets
      |-- QTcpServer / QTcpSocket
      |
      +-- Scan Coordinator Thread (一次扫描最多一个)
              |
              +-- ThreadPool N workers
                      |- metadata task
                      |- metadata task
                      `- metadata task
```

### 19.1 禁止行为

- Qt socket object 不得被任意 std::thread 直接读写。
- worker 不得直接操作 QWidget。
- service main/event thread 不得进行全目录同步扫描或全文件 SHA-256。
- UI close 时不得粗暴 terminate service；service 是独立进程。

### 19.2 Service shutdown

收到正常退出：

1. 停止接收新 scan/transfer。
2. cancel active scan。
3. 停止 transfer 并关闭 socket。
4. 等待 ThreadPool shutdown。
5. 关闭 local server。
6. 退出 event loop。

任何后台 thread 都不得 detach 后遗留。


# 20. 日志与错误处理

### 20.1 日志格式

实现最小线程安全 file logger，不新增 spdlog：

```text
2026-08-12T10:10:11.123+08:00 | INFO | pid=1234 | tid=5678 | scanner | scan started root=...
```

等级：`DEBUG / INFO / WARN / ERROR`。

至少记录：

- service 启停、IPC connect/disconnect。
- scan start/cancel/complete/stats。
- 建索引 generation 和耗时。
- network connect/offer/accept/finish/fail。
- protocol parse error。
- 所有返回给用户的错误对应的底层 error_code。

### 20.2 不得泄漏

- 不记录文件内容。
- 日志可记录本地 path；README 提醒可能含隐私。
- 不记录未来可能加入的 token/password（v1.0 无此功能）。


# 21. 测试策略与必测用例

### 21.1 单元测试 MUST

**ThreadPool**

- 1000 task 全执行。
- `wait_idle()` 真正等待 active task。
- queue bounded 情况不会丢 task。
- `shutdown()` 幂等。
- shutdown 后 `submit()` 返回 false。

**FileScanner**

- 临时目录：普通文件、子目录、dot hidden、symlink（支持平台上测试）。
- permission error 不使整个扫描崩溃（平台可模拟时）。
- cancel 返回 Cancelled。
- file size/type 正确。

**Tokenizer / IndexBuilder**

- ASCII 大小写。
- 空格、点、下划线、横线分词。
- Chinese UTF-8 名称不会被损坏。
- posting 去重排序。
- prefix token 搜索。

**SearchEngine**

- exact / prefix / substring fallback。
- AND semantics。
- score 顺序。
- limit <= 100。
- cache hit 与 clear cache。

**LRU**

- get/put O(1) 行为语义。
- 更新 key 移到 MRU。
- capacity 淘汰 LRU。

**FrameCodec**

- 正常帧 roundtrip。
- header 被拆成多次 push。
- payload 被拆成多次 push。
- 一次 push 两个完整 frame。
- bad magic/version。
- payload 超限。

### 21.2 集成测试 MUST

1. **Service + Local IPC**：启动 service，client Ping/Pong，发 ScanRequest 到临时目录，等待 ScanCompleted，再 SearchRequest 并验证结果。
2. **TCP loopback transfer**：在 `127.0.0.1` 启动接收 server，发送至少 10 MiB 随机文件，最终 SHA-256 一致，过程中测试多次 chunk。

### 21.3 手工 UI 验收

- 服务不存在时 UI 能启动服务并连接。
- 扫描 1 万+文件目录期间 UI 可拖动、输入、取消，不出现“未响应”。
- 搜索框快速连续输入不产生错乱 response；旧 request 的 response 不能覆盖更新后的 query 展示。
- 服务崩溃/退出后 UI 显示离线并能重连。


# 22. 性能指标与 Benchmark 规则

绝对耗时受硬件影响，因此 v1.0 以“可复现 + 相对提升”为主，不伪造固定毫秒数据。

### 22.1 bench_search

生成 100,000 条合成 FileRecord，包含可控关键词。

必须输出：

- linear substring baseline 平均/中位耗时。
- indexed smart search 平均/中位耗时。
- cached repeated query 平均/中位耗时。
- snapshot 大致内存指标（至少 records 数、token 数、posting 数；可选 RSS）。

验收目标：

- 对能走 token/prefix index 的 query，indexed median SHOULD 至少比 linear baseline 快 5x。
- repeated cached query SHOULD 明显低于 uncached；若达不到必须分析原因，不得篡改数据。

### 22.2 scan benchmark

真实文件系统性能不作为自动测试 pass/fail。README 中记录同一目录：

- worker_count = 1 / 2 / 4 / 8（按机器调整）。
- file_count。
- elapsed_ms。
- failed/skipped。

面试时必须能解释：线程更多不一定更快，原因包括 IO 饱和、上下文切换、锁争用、目录元数据缓存等。

### 22.3 文件传输

- 1 GiB 文件（若本机磁盘空间允许）不能导致进程把 1 GiB 全部读入内存。
- 单连接用户态 chunk 缓冲 SHOULD 控制在数 MiB 内。
- 必须输出 total bytes / elapsed / MiB/s。


# 23. 里程碑实施计划（Codex 主执行单元）

### M0 - 工程骨架

**实现：** CMake targets、目录、common Result/Error、测试框架、CLI `version`。

**DoD：**

- `cmake` configure 成功。
- `coredesk_cli --version` 输出版本。
- 至少 1 个 GoogleTest 通过。
- 无 Qt 类型进入 common headers。

### M1 - ThreadPool + FileScanner CLI

**实现：** Cancellation、ThreadPool、FileRecord、FileScanner；CLI `scan <root>`。

**DoD：**

- 可扫描嵌套目录并输出统计。
- 扫描 permission/单文件错误不会整体崩溃。
- ThreadPool 与 scanner 单测通过。
- `scan` 可 Ctrl+C 或命令层触发取消（至少测试层 cancellation 可用）。

### M2 - IndexBuilder + SearchEngine + LRU

**实现：** tokenizer、immutable snapshot、inverted index、sorted prefix token、SearchEngine、LRU、CLI `search`。

**DoD：**

- CLI 能 scan 后连续 search。
- exact/prefix/AND/fallback 都有单测。
- `bench_search` 可输出 baseline/index/cache 数据。

### M3 - FrameProtocol

**实现：** 24-byte header、MessageType、FrameEncoder/Decoder、JSON schema helpers。

**DoD：**

- partial/multiple frame 测试全过。
- bad magic/version/oversize 有明确错误。
- `docs/PROTOCOL.md` 与代码枚举一致。

### M4 - Service + QLocal IPC

**实现：** `coredesk_service`、ServiceController、LocalIpcServer/Client、snapshot swap、scan/search messages。

**DoD：**

- local integration test：Ping -> Scan -> Search 全流程通过。
- service 同时只允许一个 scan。
- 扫描期间旧 snapshot 可搜索。
- service 无 index 时 Search 返回 IndexNotReady。

### M5 - Qt Desktop Thin UI

**实现：** MainWindow、root browse、scan/cancel、search debounce、results table、service auto-start/reconnect。

**DoD：**

- UI 不直接调用 FileScanner/SearchEngine。
- 1 万文件扫描时 UI 保持响应。
- 旧搜索 response 不覆盖新 query（用 request_id 判定）。
- service 不在时自动启动，失败可重试。

### M6 - TCP LAN Transfer

**实现：** QTcpServer/QTcpSocket、Hello、FileOffer/Accept/Chunk/Finish/Result、SHA-256、`.part`。

**DoD：**

- loopback 10 MiB transfer integration test 通过。
- target exists 正确拒绝。
- 文件名 path traversal 正确拒绝。
- 传输失败不留下被误认为完成的最终文件。

### M7 - 稳定性与性能

**实现：** 日志、ASan/TSan CMake option、压力测试、benchmark 文档、错误 UI。

**DoD：**

- Linux ASan build（若环境可用）核心测试无 sanitizer error。
- 至少修复并记录 1 个真实 bug，写 `docs/BUG_POSTMORTEM.md`。
- README 有 benchmark 真数据。

### M8 - 跨平台与求职包装

**实现：** 第二平台 compile fixes、README、架构图、线程图、协议图、演示脚本。

**DoD：**

- 至少一个平台完整运行；第二平台至少 Core + tests 编译，若环境具备则完整运行。
- README 明确架构取舍和 non-goals。
- 生成 1-2 分钟演示流程。


# 24. 14 天人工学习/监督节奏

| Day | Codex 主实现 | 你必须自己理解/复核 | 当天验收 |
|---|---|---|---|
| 1 | M0 骨架 | CMake target、编译链接、Result/Error | 能解释 target 依赖图 |
| 2 | M1 ThreadPool | thread/mutex/cv、RAII、shutdown | 手写 ThreadPool 核心循环 |
| 3 | M1 Scanner | filesystem、路径、IO、取消 | 能解释 symlink/permission 策略 |
| 4 | M2 Index | unordered_map、posting、排序、复杂度 | 手写 tokenizer/交集思路 |
| 5 | M2 Search/LRU | LRU、lower_bound、评分与缓存失效 | 手写 LRU |
| 6 | M3 Protocol | 大小端、TCP 字节流、半包/粘包 | 手画 24-byte header |
| 7 | M4 Service | 进程、IPC、shared_ptr 快照、锁粒度 | 讲清为什么分进程 |
| 8 | M4 IPC | QLocalSocket 事件流、request_id | 能解释 IPC vs TCP |
| 9 | M5 UI | Qt event loop、UI thread | 找出所有耗时操作的位置 |
| 10 | M6 TCP | 三次握手/流控制/自定义消息边界 | 用 Wireshark 可选观察 |
| 11 | M6 文件传输 | 分块 IO、hash、临时文件、原子 rename | 讲为什么不整文件进内存 |
| 12 | M7 调试 | GDB/VS、ASan、TSan、core dump | 完成真实 bug 复盘 |
| 13 | M7 性能 | Cache、上下文切换、锁争用、基准 | 得到真实优化数据 |
| 14 | M8 包装 | 项目架构+OS/网络/C++八股串联 | 90 分钟模拟面试 |

**重要：Codex 可以写大部分代码，但你必须自己完成表中“理解/复核”列，否则项目在面试深挖时价值会大幅下降。**


# 25. Definition of Done - v1.0 总验收清单

- [ ] Core 层公共头文件无 Qt include。
- [ ] 无 owning raw pointer。
- [ ] ThreadPool 有 bounded queue、wait_idle、幂等 shutdown。
- [ ] Scanner 默认不 follow directory symlink。
- [ ] Scanner 单文件失败不终止全扫描。
- [ ] Snapshot 完整构建后一次性交换。
- [ ] Search 支持 exact / prefix / AND / substring fallback。
- [ ] LRU cache 在 generation 变化时失效。
- [ ] FrameDecoder 通过 partial/multiple frame 测试。
- [ ] Local IPC Ping/Scan/Search 集成测试通过。
- [ ] UI 仅通过 IPC 获取搜索与扫描数据。
- [ ] UI 快速输入时不会被旧 response 覆盖。
- [ ] LAN Transfer 使用 chunk，不整文件读内存。
- [ ] 接收端防 path traversal、拒绝覆盖已有文件。
- [ ] 完成后 SHA-256 一致才 rename final。
- [ ] 测试命令在 README 可复制执行。
- [ ] benchmark 数据真实且标注机器/文件规模。
- [ ] 至少一份真实 bug postmortem。
- [ ] README 写清非目标与安全边界。


# 26. Codex 每个里程碑的强制工作流

Codex 每次收到“实现 Mx”时必须遵循：

1. **Read**：读取 `docs/COREDESK_SPEC.md`、`IMPLEMENTATION_STATUS.md`、当前 CMake 和相关源文件。
2. **Plan**：先输出本里程碑将修改/新增的文件清单和依赖关系，不开始下一里程碑内容。
3. **Implement**：按 public interface 和固定决策实现；不得擅自改协议字段/target 名/依赖。
4. **Format & warnings**：修复编译 warning；不通过删除 warning flags 逃避。
5. **Build**：执行规范命令。
6. **Test**：执行全部现有测试，不只跑新增测试。
7. **Acceptance**：执行该里程碑手工/CLI 验收。
8. **Document**：更新 `IMPLEMENTATION_STATUS.md`：Done / Known Issues / Deviations / Commands Run。
9. **Stop**：完成后停止，不自动进入下一里程碑。

### IMPLEMENTATION_STATUS.md 固定格式

```markdown
# CoreDesk Implementation Status

## Current Milestone
M2 - Index/Search

## Completed
- ...

## Tests / Commands Run
- `cmake ...`
- `ctest ...`

## Known Issues
- ...

## Deviations from Spec
- None

## Next Milestone
M3 - FrameProtocol (NOT STARTED)
```


# 27. 可直接给 Codex 的 Master Prompt

将下面内容和本规范一起交给 Codex；之后每次只替换 `<MILESTONE>`：

```text
你正在实现 CoreDesk，一个以标准 C++20 为核心的跨平台文件索引与局域网传输客户端。

仓库根目录的 docs/COREDESK_SPEC.md 是规范性文档，优先级高于你自己的架构偏好。
请先完整读取该文档、IMPLEMENTATION_STATUS.md、当前 CMakeLists.txt 和现有测试。

本轮只实现：<MILESTONE>。

强制要求：
1. 不得开始后续里程碑。
2. 不得增加规范未允许的第三方依赖。
3. 不得修改固定协议字段、MessageType 数值、CMake target 名或 UI/Core 边界，除非规范自身无法编译；如发现冲突，先记录并停止相关点。
4. Core/index/filesystem/concurrency 的公共头文件不得出现任何 Qt 类型。
5. 所有 owning resource 使用 RAII；禁止 owning raw pointer。
6. 新功能必须有测试；不得删除或弱化现有测试来通过构建。
7. 完成后必须运行完整 configure/build/ctest，并执行本里程碑 DoD 中的验收。
8. 更新 IMPLEMENTATION_STATUS.md，写清修改文件、测试命令、结果、已知问题、是否偏离规范。
9. 最后给出：变更摘要、关键设计点、测试结果、仍未完成事项。然后停止。

在写代码前，先给出本轮计划修改的文件列表与依赖关系。
```


# 28. 典型错误与禁止的“看似聪明”实现

Codex 很容易做出以下偏差，必须显式禁止：

1. **把全部逻辑写在 MainWindow.cpp**：拒绝。UI 只能发 IPC、收结果、展示。
2. **用 QtConcurrent 替代 ThreadPool**：拒绝。ThreadPool 是 C++并发学习核心。
3. **用 SQLite FTS 直接完成搜索**：拒绝。v1.0 要自己实现索引与缓存。
4. **用 Boost.Asio 替代 QTcpSocket**：拒绝。额外依赖且偏离固定技术决策。
5. **多个 worker 共享同一个 recursive_directory_iterator**：拒绝。
6. **扫描时直接 mutate 当前 search index**：拒绝。必须快照构建后 swap。
7. **用 path hash 直接当绝对唯一 FileId**：拒绝，存在碰撞问题。
8. **用 `path.string()` 当 Windows UTF-8**：拒绝，统一由 PathCodec 做平台转换。
9. **socket `readAll()` 后假定一条消息完整**：拒绝，必须 FrameDecoder 增量解析。
10. **发送文件前读入 `vector<char>(file_size)`**：拒绝，必须分块。
11. **直接用远端 filename 拼路径而不校验**：拒绝，防 path traversal。
12. **target 已存在时静默覆盖**：拒绝，v1.0 返回 TargetExists。
13. **为“跨平台”在 core 到处写 `#ifdef _WIN32`**：拒绝，平台差异收口到 adapter/helper。
14. **测试失败就注释断言**：拒绝。
15. **为了展示效果增加登录、主题、动画**：拒绝，非目标。


# 29. 面试时应能解释的设计取舍

项目完成后，你必须能够不看文档回答：

- 为什么 UI 和索引服务拆成两个进程？收益和 IPC 成本分别是什么？
- 为什么 current index 使用不可变 snapshot，而不是边扫描边写共享 map？
- 为什么 ThreadPool queue 必须 bounded？
- 为什么扫描枚举线程不直接无限 submit 百万任务？
- 为什么 FileId 不直接用 path hash？
- 为什么 path.string() 在 Windows Unicode 场景不可靠？
- 为什么 QLocalSocket 适合本地 IPC，而 TCP 另用于 LAN？
- TCP 是字节流，FrameCodec 如何解决消息边界？
- 为什么 FileChunk 要有 offset？为什么最终要 SHA-256？
- 为什么先 `.part` 再 rename？
- 为什么更多 worker 不一定更快？
- 为什么 cache 要在 index generation 改变后清空？
- 搜索的倒排索引、prefix token 与 substring fallback 各自的复杂度/适用场景？
- 如何证明性能优化真的有效，而不是偶然？
- 若线上出现 use-after-free / data race，你怎么定位？


# 30. v1.1 / v2.0 扩展清单（不得提前做）

完成 v1.0 后才可以选择：

- v1.1：文件监听增量索引（Windows ReadDirectoryChangesW / Linux inotify 或 Qt watcher adapter）。
- v1.1：断点续传，FileAccept `start_offset > 0`。
- v1.1：索引快照持久化或 SQLite metadata（仍保留自研搜索索引）。
- v1.1：UDP/mDNS 设备发现。
- v1.2：Windows 原生 IOCP adapter 与 QtNetwork 对比 benchmark。
- v1.2：更完整 Unicode case folding。
- v2.0：目录同步、冲突策略、版本日志。
- 定向岗位：Chromium/CEF、WebRTC/FFmpeg、Android NDK/JNI adapter。


# 31. 参考文档

- Qt QLocalServer: https://doc.qt.io/qt-6/qlocalserver.html
- Qt QLocalSocket: https://doc.qt.io/qt-6/qlocalsocket.html
- Qt QTcpServer: https://doc.qt.io/qt-6/qtcpserver.html
- Qt QTcpSocket: https://doc.qt.io/qt-6/qtcpsocket.html
- Qt Network Programming: https://doc.qt.io/qt-6/qtnetwork-programming.html
- CMake target_include_directories: https://cmake.org/cmake/help/latest/command/target_include_directories.html
- CMake target_link_libraries: https://cmake.org/cmake/help/latest/command/target_link_libraries.html
- cppreference: https://en.cppreference.com/
