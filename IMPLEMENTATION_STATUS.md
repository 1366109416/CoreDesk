# CoreDesk Implementation Status

## Current Milestone
M2 - IndexBuilder + SearchEngine + LRU (DONE)

## Completed
- Preserved M0/M1 behavior, including `coredesk_cli --version` and `coredesk_cli scan <root>`.
- Added pure C++ index module types for `PostingList` and immutable `IndexSnapshot`.
- Added centralized path-to-index-text conversion using standard C++ `generic_u8string()` byte handling; Qt is not used.
- Added tokenizer with ASCII lowercase normalization, fixed separators, byte-preserving non-ASCII handling, and 128-byte token truncation.
- Added `IndexBuilder` producing `std::shared_ptr<const IndexSnapshot>` with FileId `1..N`, `id_to_pos`, deduplicated/sorted posting lists, sorted unique tokens, and `normalized_names`.
- Added `SearchEngine` with exact token search, prefix search via `sorted_tokens + lower_bound`, AND semantics, substring fallback, deterministic fixed scoring, result limits, and cache invalidation support.
- Added `LruCache` using `std::list + std::unordered_map` with O(1) get/put behavior and mutex-protected internal state.
- Added `coredesk_cli search <root> <query> [query...]`, which scans once, builds one snapshot, and runs consecutive searches without Service/IPC.
- Implemented `coredesk_bench_search` with 100,000 synthetic records, real chrono measurements, consumed benchmark results, baseline/index/cache timings, and snapshot size metrics.
- Added M2 unit tests for Tokenizer, IndexBuilder, SearchEngine, and LRU Cache.
- Updated CMake so `coredesk_index` builds as a real target and links into tests, CLI, service library, and benchmark.

## Tests / Commands Run
- `git branch --show-current`
  - Result: `feat/m2-index-search-lru`.
- `git status --short`
  - Result before implementation: clean.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m2-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: failed in this environment while FetchContent downloaded `nlohmann/json`; the sandboxed attempt hit TLS credential errors.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m2-verify-net -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: failed in this environment while FetchContent downloaded `nlohmann/json`; approved-network retry reached GitHub and the connection was reset.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m0-clean-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: passed by reusing the existing local FetchContent dependency cache.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m0-clean-verify`
  - Result: passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m0-clean-verify --output-on-failure`
  - Result: passed, 66/66 tests with 2 directory symlink tests skipped because directory symlinks are unavailable in this environment.
- `.\build-m0-clean-verify\coredesk_cli.exe --version`
  - Result: `CoreDesk 1.0.0`.
- `.\build-m0-clean-verify\coredesk_cli.exe scan .\build-m0-clean-verify\tmp_m2_cli`
  - Result: passed; scanned 4 records, failed 0.
- `.\build-m0-clean-verify\coredesk_cli.exe search .\build-m0-clean-verify\tmp_m2_cli report 2026 alpha`
  - Result: passed; built one snapshot with 4 records and returned one hit for each query.
- `.\build-m0-clean-verify\coredesk_bench_search.exe`
  - Result:
    - `records: 100000`
    - `tokens: 100006`
    - `postings: 603900`
    - `build_us: 1786253`
    - `linear_check_count: 20`
    - `indexed_check_hits: 1`
    - `linear_avg_us: 2743`
    - `linear_median_us: 2745`
    - `indexed_avg_us: 13`
    - `indexed_median_us: 11`
    - `cached_avg_us: 0`
    - `cached_median_us: 0`
    - `cached_median_ns: 700`
    - `checksum: 856`
- `Select-String -Path include\coredesk\common\*.h,include\coredesk\concurrency\*.h,include\coredesk\filesystem\*.h,include\coredesk\index\*.h -Pattern '#include <Q|#include "Q|QString|QByteArray|QFile|QThread|Qt'`
  - Result: no matches.
- `Select-String -CaseSensitive -Path include\coredesk\index\*.h,src\index\*.cpp,tests\unit\test_index_builder.cpp,tests\unit\test_search_engine.cpp,tests\unit\test_lru_cache.cpp,tests\unit\test_tokenizer.cpp,apps\cli\main.cpp,benchmarks\bench_search.cpp -Pattern 'FrameProtocol|MessageType|FrameCodec|FrameDecoder|QLocal|QLocalServer|QLocalSocket|ServiceController|QTcp|QTcpServer|QTcpSocket|QWidget|IPC|Boost|SQLite|FTS|Trie|spdlog|Abseil'`
  - Result: no matches.
- `git check-ignore -v build-m2-verify build-m2-verify-net build-m0-clean-verify build-m0-clean-verify/tmp_m2_cli _deps`
  - Result: all checked build/dependency paths are ignored.

## Known Issues
- Default full configure still requires a local Qt 6 development package. On this machine, CMake cannot find `Qt6Config.cmake`, so UI/network placeholder targets cannot be configured until Qt 6 is installed or `Qt6_DIR`/`CMAKE_PREFIX_PATH` is set.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent cache allowed M2 configure/build/test/benchmark verification to complete.
- The two directory symlink FileScanner tests remain skipped on this machine because directory symlink creation is not available in the current environment.
- `cached_median_us` may be `0` because the measured cache-hit latency is below microsecond resolution; `cached_median_ns` is also reported for benchmark display precision.

## Deviations from Spec
- None. M2 was implemented as IndexBuilder + SearchEngine + LRU only; no M3 or later functionality was started.

## Next Milestone
M3 - FrameProtocol (NOT STARTED)
