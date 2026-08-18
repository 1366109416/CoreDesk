# CoreDesk Implementation Status

## Current Milestone
M1 - ThreadPool + FileScanner CLI (DONE)

## Completed
- Kept M0 engineering skeleton intact and preserved `coredesk_cli --version`.
- Added `CancellationToken` and `CancellationSource`.
- Added bounded C++20 `ThreadPool` using `std::thread`, `std::mutex`, and `std::condition_variable`.
- Added test-observable `ThreadPool` worker exception counting.
- Added `FileRecord`, `ScanOptions`, `ScanProgress`, `ScanOutput`, and `FileScanner`.
- Added `FileScanner` progress throttling and directory symlink cycle protection.
- Added CLI command `coredesk_cli scan <root>` that scans a directory and prints scan statistics.
- Added M1 GoogleTest coverage for cancellation, ThreadPool behavior, and FileScanner behavior.
- Updated CMake targets so `coredesk_concurrency` and `coredesk_filesystem` build real M1 libraries.
- Replaced obsolete `.gitkeep` files in implemented concurrency/filesystem directories.

## Tests / Commands Run
- `git branch --show-current`
  - Result: `feat/m1-filescanner`.
- `git status --short`
  - Result before implementation: clean.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m1-fix-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: failed in this environment while FetchContent downloaded `nlohmann/json`; the sandboxed attempt hit TLS credential errors.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m1-fix-verify-net -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: failed in this environment while FetchContent downloaded `nlohmann/json`; approved-network retry reached GitHub and was reset by `release-assets.githubusercontent.com`.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m0-clean-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: passed by reusing the existing local FetchContent dependency cache.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m0-clean-verify`
  - Result: passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m0-clean-verify --output-on-failure`
  - Result: passed, 26/26 tests with 2 directory symlink tests skipped because directory symlinks are unavailable in this environment.
- `.\build-m0-clean-verify\coredesk_cli.exe --version`
  - Result: `CoreDesk 1.0.0`.
- `.\build-m0-clean-verify\coredesk_cli.exe scan .\build-m0-clean-verify\tmp_m1_fix_scan`
  - Result: passed; scanned 3 records, failed 0.
- `Select-String -Path include\coredesk\common\*.h,include\coredesk\concurrency\*.h,include\coredesk\filesystem\*.h -Pattern '#include <Q|#include "Q|QString|QByteArray|QFile|QThread|Qt'`
  - Result: no matches.
- `Select-String -Path include\coredesk\**\*.h,src\**\*.cpp,tests\unit\*.cpp,apps\cli\main.cpp,CMakeLists.txt -Pattern 'IndexBuilder|SearchEngine|Tokenizer|Trie|Lru|LRU|QLocal|QTcp|QWidget|QtConcurrent|Boost|SQLite|spdlog|Abseil'`
  - Result: no matches.

## Known Issues
- Default full configure still requires a local Qt 6 development package. On this machine, CMake cannot find `Qt6Config.cmake`, so UI/network placeholder targets cannot be configured until Qt 6 is installed or `Qt6_DIR`/`CMAKE_PREFIX_PATH` is set.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent cache allowed M1 configure/build/test verification to complete.
- The symlink unit test is present but skipped on this machine because directory symlink creation is not available in the current environment.

## Deviations from Spec
- None. M1 was implemented as ThreadPool + FileScanner CLI only; no M2 or later functionality was started.

## Next Milestone
M2 - IndexBuilder + SearchEngine + LRU (NOT STARTED)
