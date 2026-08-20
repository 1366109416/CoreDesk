# CoreDesk Implementation Status

## Current Milestone
M4 - Service + QLocal IPC (DONE)

## Completed
- Preserved M0-M3 behavior, including `coredesk_cli --version`, scanner/index/search tests, and FrameProtocol tests.
- Added pure C++ `ServiceController` with service state, scan handling, search handling, cancellation, current snapshot ownership, and thread-safe snapshot swap.
- Kept scanning and index building on a background coordinator thread; Qt event callbacks do not run full filesystem scans.
- Kept search serving the old immutable snapshot while a new scan is running; the new snapshot replaces the current snapshot only after a successful full build.
- Added `LocalIpcServer` using `QLocalServer`, per-connection `FrameDecoder`, `readyRead` dispatch, disconnect cleanup, and M3 `FrameEncoder` for responses.
- Added `LocalIpcClient` using `QLocalSocket`, a client-side `FrameDecoder`, monotonic `RequestId`, and minimal request send helpers.
- Implemented M4 IPC handling for `Ping`, `ScanRequest`, `ScanAccepted`, `ScanProgress`, `ScanCompleted`, `ScanFailed`, `CancelScanRequest`, `SearchRequest`, and `SearchResponse`.
- Kept `ScanAccepted` payload empty because the specification does not define a JSON schema for it.
- Did not implement `StatusRequest` / `StatusResponse` business behavior because M4 DoD does not require it and no schema is fixed by the specification.
- Did not auto-cancel a scan when a client disconnects; disconnected sockets stop receiving progress/completion messages.
- Converted `coredesk_service_lib` from an INTERFACE placeholder into a real pure C++ service library.
- Converted `coredesk_qt_ipc` from an INTERFACE placeholder into a real Qt local IPC client adapter library.
- Added `coredesk_qt_ipc_server` as the service-side Qt local IPC adapter target so `coredesk_desktop` does not transitively link `coredesk_service_lib`.
- Converted `coredesk_service` into a minimal `QCoreApplication` service executable that owns `ServiceController` and `LocalIpcServer`.
- Added M4 service unit tests and local IPC integration tests.
- Fixed the first M4 audit Major findings:
  - `LocalIpcServer` destruction now closes IPC connections and asks `ServiceController` to shutdown/join any active scan before the server object can disappear under queued scan callbacks.
  - The service scan coordinator thread has a top-level exception boundary and converts unexpected exceptions into `ErrorCode::InternalError` without installing a partial snapshot.
  - `coredesk_qt_ipc` and `coredesk_qt_ipc_server` are split so the desktop-side client target does not carry service/index/filesystem dependencies.
  - Qt Core/Network discovery now follows the M4 Local IPC targets instead of being tied to the UI, TCP network, or tests options.
  - MSVC builds enable `/EHsc` through the shared warning helper so exception handling is compiled with proper unwind semantics.

## Files Added
- `include/coredesk/service/ServiceController.h`
- `src/service/ServiceController.cpp`
- `adapters/qt_ipc/LocalIpcServer.h`
- `adapters/qt_ipc/LocalIpcServer.cpp`
- `adapters/qt_ipc/LocalIpcClient.h`
- `adapters/qt_ipc/LocalIpcClient.cpp`
- `tests/unit/test_service_controller.cpp`
- `tests/integration/test_local_ipc.cpp`

## Files Modified
- `CMakeLists.txt`
- `cmake/CompilerWarnings.cmake`
- `apps/service/main.cpp`
- `IMPLEMENTATION_STATUS.md`

## Files Removed
- `include/coredesk/service/.gitkeep`
- `src/service/.gitkeep`
- `adapters/qt_ipc/.gitkeep`
- `tests/integration/.gitkeep`

## Tests / Commands Run
- `git branch --show-current`
  - Result: `feat/m4-service-local-ipc`.
- `git status --short`
  - Result before implementation: clean.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m4-msvc -G "Visual Studio 17 2022" -A x64 -DQt6_DIR="D:\Qt\6.11.2\msvc2022_64\lib\cmake\Qt6" -DCOREDESK_BUILD_TESTS=ON -DCOREDESK_FETCH_DEPS=ON -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="D:\Projects\CoreDesk\build-m0-clean-verify\_deps\nlohmann_json-src" -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="D:\Projects\CoreDesk\build-m0-clean-verify\_deps\googletest-src"`
  - Result: configure passed with Qt 6.11.2, MSVC v143 / x64, and the existing local dependency cache.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m4-msvc --config Debug`
  - Result after first audit fixes: build passed. The generated targets include `coredesk_service_lib`, `coredesk_qt_ipc`, `coredesk_qt_ipc_server`, `coredesk_service.exe`, `coredesk_integration_tests.exe`, and `coredesk_tests.exe`.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m4-msvc -C Debug --output-on-failure`
  - Result after first audit fixes: passed, 104 total registered / 0 failed / 2 skipped, with `coredesk_integration_tests` passing.
  - Skipped tests: the two existing FileScanner directory symlink tests, because directory symlink creation is unavailable in this Windows environment.
  - The `coredesk_integration_tests` CTest entry passed and contains the M4 Local IPC scenarios for Ping/Pong, Ping -> Scan -> Search, search before index ready, multiple frame writes, partial raw frame input, malformed protocol disconnect, and active-scan server shutdown.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m4-msvc-ui-off -G "Visual Studio 17 2022" -A x64 -DQt6_DIR="D:\Qt\6.11.2\msvc2022_64\lib\cmake\Qt6" -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_TESTS=ON -DCOREDESK_FETCH_DEPS=ON -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="D:\Projects\CoreDesk\build-m0-clean-verify\_deps\nlohmann_json-src" -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="D:\Projects\CoreDesk\build-m0-clean-verify\_deps\googletest-src"`
  - Result: configure passed. M4 Local IPC targets still configure when the M5 UI and M6 TCP transfer options are disabled.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m4-msvc-ui-off --config Debug --target coredesk_service coredesk_integration_tests`
  - Result: build passed. This verifies `coredesk_service`, `coredesk_qt_ipc`, `coredesk_qt_ipc_server`, and the local IPC integration tests can build with `COREDESK_BUILD_UI=OFF` and `COREDESK_BUILD_NETWORK=OFF`.
- `.\build-m4-msvc\Debug\coredesk_cli.exe --version`
  - Result: `CoreDesk 1.0.0`.
- `Select-String -Path build-m4-msvc\coredesk_desktop.vcxproj,build-m4-msvc\coredesk_qt_ipc.vcxproj -Pattern 'coredesk_service_lib|coredesk_filesystem|coredesk_index'`
  - Result: no matches. `coredesk_desktop` no longer transitively depends on service core, filesystem, or index through `coredesk_qt_ipc`.
- `Select-String -Path include\coredesk\service\*.h,src\service\*.cpp -Pattern '#include <Q|#include "Q|QString|QByteArray|QFile|QThread|QObject|QLocal|QTcp|Qt'`
  - Result: no matches.
- `Select-String -Path include\coredesk\service\*.h,src\service\*.cpp,adapters\qt_ipc\*.h,adapters\qt_ipc\*.cpp,apps\service\main.cpp,CMakeLists.txt -Pattern 'QTcpServer|QTcpSocket|TcpTransfer|FileOffer|FileChunk|FileFinish|SHA-256|MainWindow|SearchWidget|TransferWidget|QWidget'`
  - Result: no matches.
- `git diff --check`
  - Result: no whitespace errors; only CRLF working-copy warnings for `CMakeLists.txt` and `apps/service/main.cpp`.

## Known Issues
- Qt runtime tests require `D:\Qt\6.11.2\msvc2022_64\bin` on the current process `PATH`, so that `Qt6Cored.dll` and `Qt6Networkd.dll` can be found. This is a local runtime environment requirement; Qt DLLs were not copied into the project or system directories.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent source cache allowed M4 configure/build/test verification to complete.
- The two directory symlink FileScanner tests remain skipped on this machine because directory symlink creation is not available in the current Windows environment.
- The M3 non-blocking performance note remains: `FrameDecoder::push()` uses `vector::erase(begin, ...)`, which can add data movement for many tiny frames. This does not affect M4 correctness or DoD.

## Deviations from Spec
- None. M4 was implemented as Service + QLocal IPC only; no M5 Qt Widgets UI or M6 TCP/file transfer functionality was started.

## Next Milestone
M5 - Qt Desktop Thin UI (NOT STARTED)
