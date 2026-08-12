# CoreDesk Agent Rules

These rules are extracted from `docs/COREDESK_SPEC.md`. The specification is
normative and has higher priority than agent preferences.

## Project Architecture Rules

- CoreDesk is a standard C++20 based cross-platform desktop file indexing and
  LAN transfer client.
- `coredesk_desktop` is a Qt Widgets process and is responsible only for user
  input, display, service startup, and IPC client behavior.
- `coredesk_service` is an independent long-lived background service process.
  It owns the current index snapshot and transfer state.
- Desktop and service communicate through `QLocalSocket` / `QLocalServer` plus
  the frame protocol.
- `coredesk_desktop` must not directly scan directories, build indexes, or
  perform large-file transfer.
- Search must continue serving the old index while a new scan is running. The
  new index must be fully built and then swapped in at once.

## Allowed Dependencies

Only these third-party dependencies are allowed:

- Qt 6: `Core`, `Widgets`, `Network`
- nlohmann/json
- GoogleTest, only for test targets

Any additional third-party dependency requires changing the specification first
and documenting why it cannot be replaced.

## Forbidden Dependencies And Non-Goals

- Do not introduce Boost, SQLite, spdlog, Abseil, or other libraries not listed
  as allowed dependencies.
- Do not add large dependencies, databases, server frameworks, cloud services,
  account/login systems, or complex UI.
- Do not implement full-text file content indexing.
- Do not implement automatic bidirectional folder sync, conflict merging, or
  version history.
- Do not implement cloud, accounts, login, a server product, or P2P traversal.
- Do not implement automatic device discovery. LAN targets are entered manually.
- Do not implement TLS, encryption, or authentication for v1.0.
- Do not implement database persistence for v1.0.
- Do not implement complex themes, animation, docks, multiple tabs, or plugins.
- Do not implement an Android main program.
- Do not target formal macOS v1.0 support.

## Directory And Module Boundaries

- `include/coredesk/common/`: common types such as `Error`, `Result`, and fixed
  shared aliases.
- `include/coredesk/concurrency/`: standard C++ concurrency primitives such as
  `ThreadPool` and cancellation.
- `include/coredesk/filesystem/`: file records and filesystem scanning.
- `include/coredesk/index/`: index snapshots, index building, search, and LRU
  cache.
- `include/coredesk/protocol/`: message types, frame types, and frame codec.
- `include/coredesk/service/`: service controller boundary.
- `src/`: implementation files for the matching pure C++ modules.
- `adapters/qt_ipc/`: Qt local IPC adapter.
- `adapters/qt_network/`: Qt TCP LAN transfer adapter.
- `apps/service/`: `coredesk_service` process entry.
- `apps/desktop/`: `coredesk_desktop` process entry.
- `apps/cli/`: development/debug CLI.
- `ui/`: Qt Widgets UI classes.
- `tests/unit/` and `tests/integration/`: unit and integration tests.
- `benchmarks/`: benchmark targets.

Qt is allowed only in `apps/`, `adapters/qt_ipc/`, `adapters/qt_network/`, and
`ui/`.

## C++ Standard And Build Rules

- Use C++20.
- Set `CMAKE_CXX_STANDARD 20`.
- Set `CMAKE_CXX_STANDARD_REQUIRED ON`.
- Disable compiler extensions.
- Use CMake 3.24 or newer.
- Use target-based CMake.
- Use `target_include_directories()`, `target_link_libraries()`, and
  `target_compile_features()`.
- Public includes must be exposed as `PUBLIC` or `INTERFACE`; implementation
  dependencies use `PRIVATE`.
- Do not use global `include_directories()` or `link_directories()`.
- Do not place all `.cpp` files into one executable target.

Fixed target names:

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

Required CMake options:

```cmake
COREDESK_BUILD_UI=ON
COREDESK_BUILD_TESTS=ON
COREDESK_BUILD_NETWORK=ON
COREDESK_ENABLE_ASAN=OFF
COREDESK_ENABLE_TSAN=OFF
COREDESK_WARNINGS_AS_ERRORS=OFF
COREDESK_FETCH_DEPS=ON
```

## Naming And Coding Rules

- Use namespace `coredesk`; submodules may use namespaces such as
  `coredesk::index`.
- Types and classes use `PascalCase`.
- Functions and variables use `snake_case`.
- Private data members use a trailing underscore.
- Owning resources must be held by RAII objects.
- Owning raw pointers are forbidden. Raw pointers are allowed only as
  non-owning, short-lifetime observers.
- Prefer `std::string_view` and `std::span` in public interfaces when ownership
  does not transfer.
- Core filesystem path type is `std::filesystem::path`.
- Do not use C++23 `std::expected`; implement the lightweight `Result` type.
- `Result<void>` must have a dedicated or equivalent implementation.
- Do not use global mutable singletons.

## Multithreading And Concurrency Rules

- Core concurrency uses `std::thread`, `std::mutex`, and
  `std::condition_variable`.
- Do not replace the specified ThreadPool with QtConcurrent.
- `ThreadPool` must have a bounded queue, default size 4096.
- `submit` may block when the queue is full; after shutdown it must not accept
  more tasks.
- Workers maintain `active_count_` before and after taking tasks.
- `wait_idle()` waits for `tasks_.empty() && active_count_ == 0`.
- `shutdown()` must be idempotent, and the destructor calls `shutdown()`.
- Worker top-level protection must `catch (...)`; exceptions must not be
  silently lost.
- Scanning cancellation must be checked at least in directory enumeration loops,
  at the start of each metadata task, and between index-building batches.
- Background threads must not be detached and left behind.
- Qt socket objects must not be directly read or written from arbitrary
  `std::thread`s.
- Workers must not directly operate on `QWidget`.
- The service main/event thread must not perform full directory scans or full
  file SHA-256 work.

## Qt Boundary Rules

- The pure C++ core modules and their public headers must not include Qt
  headers.
- Qt types such as `QString`, `QByteArray`, `QFile`, and `QThread` must not
  appear in pure C++ public interfaces.
- Qt is limited to `apps/`, `adapters/qt_ipc/`, `adapters/qt_network/`, and
  `ui/`.
- GUI uses Qt 6 Widgets.
- Local IPC uses `QLocalServer` and `QLocalSocket`.
- LAN networking uses `QTcpServer` and `QTcpSocket`.
- Qt IPC adapters pass raw bytes to the pure C++ `FrameDecoder`; do not write a
  second protocol parser in the Qt layer.
- UI must not directly create `std::thread`; asynchronous behavior depends on
  the Qt event loop plus service.
- UI close must not violently terminate the service; the service is an
  independent process.

## Build Commands

Linux / Ninja or Make:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCOREDESK_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Windows multi-config:

```powershell
cmake -S . -B build -DCOREDESK_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Test Rules

- New functionality must have tests.
- Do not delete, weaken, or bypass tests to make a build pass.
- Run all existing tests, not only newly added tests.
- Required test areas include ThreadPool, FileScanner, Tokenizer/IndexBuilder,
  SearchEngine, LRU, FrameCodec, Service + Local IPC integration, and TCP
  loopback transfer integration as their milestones are implemented.

## Milestone Development Flow

For each milestone:

1. Read `docs/COREDESK_SPEC.md`, `IMPLEMENTATION_STATUS.md`, current CMake, and
   relevant source files.
2. Plan by listing files to be modified or added and their dependencies before
   starting implementation.
3. Implement according to public interfaces and fixed technical decisions.
4. Fix compile warnings without removing warning flags to avoid them.
5. Build with the specification commands.
6. Run all existing tests.
7. Run the milestone manual or CLI acceptance checks.
8. Update `IMPLEMENTATION_STATUS.md` with Done, Known Issues, Deviations, and
   Commands Run.
9. Stop after the milestone is complete.

## Milestone Boundaries

- Implement only one milestone at a time.
- Do not enter the next milestone before the current milestone Definition of
  Done passes.
- Do not begin later milestone work early.
- Do not implement v1.1 or v2.0 extension items early.
- If the specification contains a real conflict, pause that point and record it
  in `IMPLEMENTATION_STATUS.md`; do not silently change the design.
