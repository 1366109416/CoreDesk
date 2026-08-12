# CoreDesk Implementation Status

## Current Milestone
M0 - Engineering Skeleton (DONE)

## Completed
- Added target-based CMake engineering skeleton with the fixed CoreDesk target names for M0.
- Added required build options: `COREDESK_BUILD_UI`, `COREDESK_BUILD_TESTS`, `COREDESK_BUILD_NETWORK`, `COREDESK_ENABLE_ASAN`, `COREDESK_ENABLE_TSAN`, `COREDESK_WARNINGS_AS_ERRORS`, and `COREDESK_FETCH_DEPS`.
- Added pure C++ common headers for `Types`, `Error`, and `Result`, including `Result<void>`.
- Added minimal `coredesk_cli --version`.
- Added placeholder executables/targets for service, desktop, and search benchmark without implementing later milestones.
- Added GoogleTest-based M0 tests for `Result` and `Error`.
- Updated README with M0 build and verification commands.
- Added root `_deps` ignore rules to keep FetchContent artifacts out of version control.
- Restored spec-compliant `nlohmann/json` dependency discovery: `find_package` first, then FetchContent when not found and `COREDESK_FETCH_DEPS=ON`.

## Tests / Commands Run
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-default-check -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_TESTS=ON`
  - Result: failed because Qt 6 is not installed or not discoverable via `CMAKE_PREFIX_PATH` / `Qt6_DIR`.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m0-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_TESTS=ON -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF`
  - Result: passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m0-verify`
  - Result: passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m0-verify --output-on-failure`
  - Result: passed, 4/4 tests.
- `.\build-m0-verify\coredesk_cli.exe --version`
  - Result: `CoreDesk 1.0.0`.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m0-clean-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: passed. First sandboxed attempt failed due TLS/network credentials while downloading `nlohmann/json`; rerun with approved network access passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m0-clean-verify`
  - Result: passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m0-clean-verify --output-on-failure`
  - Result: passed, 4/4 tests.
- `.\build-m0-clean-verify\coredesk_cli.exe --version`
  - Result: `CoreDesk 1.0.0`.
- `git check-ignore -v build build-m0 build-m0-verify build-default-check _deps`
  - Result: all requested paths matched ignore rules.
- `Select-String -Path include/coredesk/**/*.h -Pattern '#include <Q|#include "Q|QString|QByteArray|QFile|QThread|Qt'`
  - Result: no matches.

## Known Issues
- Default full configure currently requires a local Qt 6 development package. On this machine, CMake cannot find `Qt6Config.cmake`, so UI/network placeholder targets cannot be configured until Qt 6 is installed or `Qt6_DIR`/`CMAKE_PREFIX_PATH` is set.
- The repository specification files display as mojibake in the current PowerShell code page, but the normative sections used for M0 were still identifiable by structure and English identifiers.

## Deviations from Spec
- None. The M0 verification build disabled UI/network placeholders only because Qt 6 is missing from this machine; the default project options remain `COREDESK_BUILD_UI=ON` and `COREDESK_BUILD_NETWORK=ON` as specified.

## Next Milestone
M1 - ThreadPool + FileScanner CLI (NOT STARTED)
