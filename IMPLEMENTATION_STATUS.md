# CoreDesk Implementation Status

## Current Milestone
M3 - FrameProtocol (DONE)

## Completed
- Preserved M0/M1/M2 behavior, including `coredesk_cli --version`, scanner tests, search tests, and index/LRU tests.
- Added fixed protocol message types with explicit wire values matching `docs/COREDESK_SPEC.md`.
- Added `Frame`, `FrameEncoder`, and streaming `FrameDecoder` for the fixed 24-byte `CDSK` header.
- Implemented explicit big-endian integer encoding/decoding; the wire format does not use C++ struct memory dumps.
- Enforced 1 MiB maximum payload length in encoder and decoder.
- Added decoder validation for bad magic, bad version, unknown message type, and oversized payload.
- Added decoder buffering for partial headers, partial payloads, multiple complete frames, and a complete frame followed by a partial next frame.
- Added JSON payload helpers for `ScanRequest`, `ScanProgress`, `ScanCompleted`, `SearchRequest`, `SearchResponse`, and error response schemas.
- JSON syntax errors map to `ProtocolError`; valid JSON with schema errors maps to `InvalidArgument`.
- Fixed the first M3 audit finding where JSON integer fields could be converted to narrower C++ integer types without explicit range validation.
- Added explicit range checks for JSON `int` and `std::int64_t` conversions before accepting schema values.
- Kept `nlohmann/json` inside the protocol implementation; protocol public headers expose only CoreDesk structs and standard C++ types.
- Converted `coredesk_protocol` from an INTERFACE placeholder into a real C++20 library target.
- Added M3 unit tests for FrameCodec and JSON payload helpers.
- Added `docs/PROTOCOL.md` with the 24-byte header, message type values, payload limit, streaming rules, and protocol error behavior.

## Files Added
- `include/coredesk/protocol/MessageTypes.h`
- `include/coredesk/protocol/Frame.h`
- `include/coredesk/protocol/FrameCodec.h`
- `include/coredesk/protocol/JsonPayload.h`
- `src/protocol/FrameCodec.cpp`
- `src/protocol/JsonPayload.cpp`
- `tests/unit/test_frame_protocol.cpp`
- `tests/unit/test_json_payload.cpp`
- `docs/PROTOCOL.md`

## Files Modified
- `CMakeLists.txt`
- `IMPLEMENTATION_STATUS.md`

## Files Removed
- `include/coredesk/protocol/.gitkeep`
- `src/protocol/.gitkeep`

## Tests / Commands Run
- `git branch --show-current`
  - Result: `feat/m3-frame-protocol`.
- `git status --short`
  - Result before implementation: clean.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m3-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: failed in this environment while FetchContent downloaded `nlohmann/json`; the sandboxed attempt hit GitHub TLS credential errors.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m0-clean-verify -G Ninja -DCMAKE_MAKE_PROGRAM='D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON`
  - Result: passed by reusing the existing local FetchContent dependency cache.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m0-clean-verify`
  - Result: passed. `coredesk_protocol`, `FrameCodec.cpp`, `JsonPayload.cpp`, and the new M3 tests were compiled from the current source.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m0-clean-verify --output-on-failure`
  - Result before first audit fix: passed, 91 total / 89 passed / 0 failed / 2 skipped.
  - Skipped tests: the two existing FileScanner directory symlink tests, because directory symlink creation is unavailable in this environment.
- First audit fix:
  - Fixed JSON numeric range validation for `required_i64()` and `required_int()`.
  - Added JSON numeric boundary tests for `score` and `modified_ms`.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m0-clean-verify`
  - Result after first audit fix: passed. `JsonPayload.cpp` and `test_json_payload.cpp` were recompiled from the current source.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m0-clean-verify --output-on-failure`
  - Result after first audit fix: passed, 95 total / 93 passed / 0 failed / 2 skipped.
  - Skipped tests: the two existing FileScanner directory symlink tests, because directory symlink creation is unavailable in this environment.
- `.\build-m0-clean-verify\coredesk_cli.exe --version`
  - Result: `CoreDesk 1.0.0`.
- `Select-String -Path include\coredesk\common\*.h,include\coredesk\concurrency\*.h,include\coredesk\filesystem\*.h,include\coredesk\index\*.h,include\coredesk\protocol\*.h -Pattern '#include <Q|#include "Q|QString|QByteArray|QFile|QThread|Qt'`
  - Result: no matches.
- `Select-String -CaseSensitive -Path include\coredesk\protocol\*.h,src\protocol\*.cpp,tests\unit\test_frame_protocol.cpp,tests\unit\test_json_payload.cpp,CMakeLists.txt -Pattern 'QLocalServer|QLocalSocket|LocalIpcServer|LocalIpcClient|ServiceController|QTcpServer|QTcpSocket|TcpTransfer|SHA-256|QWidget|QtConcurrent|Boost|SQLite|FTS|spdlog|Abseil'`
  - Result: no matches.
- `git diff --check`
  - Result: no whitespace errors; only the existing CRLF working-copy warning for `CMakeLists.txt`.

## Known Issues
- Default full configure still requires a local Qt 6 development package. On this machine, CMake cannot find `Qt6Config.cmake`, so UI/network placeholder targets cannot be configured until Qt 6 is installed or `Qt6_DIR`/`CMAKE_PREFIX_PATH` is set.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent cache allowed M3 configure/build/test verification to complete.
- The two directory symlink FileScanner tests remain skipped on this machine because directory symlink creation is not available in the current environment.

## Deviations from Spec
- None. M3 was implemented as FrameProtocol only; no M4 or later functionality was started.

## Next Milestone
M4 - Service + QLocal IPC (NOT STARTED)
