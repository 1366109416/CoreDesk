# CoreDesk Implementation Status

## Current Milestone
M5 - Qt Desktop Thin UI (DONE)

## Completed
- Preserved M0-M4 behavior, including `coredesk_cli --version`, scanner/index/search tests, FrameProtocol tests, and Service + Local IPC integration tests.
- Replaced the desktop placeholder with a real Qt Widgets `MainWindow`.
- Added a thin desktop UI for root directory selection, Scan / Cancel, service status, scan progress summary, search elapsed time, index generation, and tabbed Search / LAN Transfer areas.
- Added `SearchWidget` with a `QLineEdit`, a single-shot 150 ms debounce timer, and a `QTableWidget` results table.
- Limited rendered search results to at most 100 rows.
- Kept the desktop thin: UI code does not include or call `FileScanner`, `IndexBuilder`, `SearchEngine`, or `ServiceController`.
- Kept `MainWindow` as the owner of `LocalIpcClient`; `SearchWidget` only emits debounced search requests and renders search responses.
- Added M5 request correlation: `MainWindow` stores the latest search `RequestId` and ignores stale `SearchResponse` frames.
- Fixed the first M5 audit search-correlation findings: any user query text change now immediately invalidates the previous pending search request, including the window before the next 150 ms debounce fires.
- Fixed empty-query correlation: clearing the search box immediately invalidates the previous pending search request and keeps old responses from repopulating the table.
- Fixed the manual GUI acceptance search issue found on a 100100-entry index:
  - `LocalIpcServer` now dispatches `SearchRequest` work to a bounded service-side `ThreadPool` and returns immediately to the Qt IPC event loop.
  - Search responses are posted back to the Qt event thread with `QMetaObject::invokeMethod`; worker threads never write `QLocalSocket` directly.
  - `SearchEngine` broad-query ranking now caches per-candidate tie-break keys and uses top-k ordering for the requested limit instead of sorting every matching candidate before truncating to 100.
  - Non-empty query changes now clear stale visible result rows immediately while preserving the 150 ms debounce before sending the next `SearchRequest`.
  - Search result `modified_ms` now converts `std::filesystem::file_time_type` to `system_clock` / Unix epoch milliseconds instead of assuming the file clock epoch matches Unix epoch.
  - Successful scan completion now clears prior scan cancel/failure text from the status line.
- Added scan request correlation for `ScanAccepted`, `ScanProgress`, `ScanCompleted`, and `ScanFailed`.
- Added non-blocking `LocalIpcClient::connect_to_server_async()` and minimal connected / disconnected / error callbacks for desktop use, while preserving the existing blocking `connect_to_server()` API used by M4 tests.
- Implemented service auto-start / retry flow in `MainWindow` using `QProcess::startDetached`, `QTimer`, and `QCoreApplication::applicationDirPath()`.
- Implemented Offline / Retry behavior for service disconnects and connection failures.
- Added a minimal LAN Transfer tab placeholder only. No TCP, file-transfer, `FileChunk`, SHA-256, `.part`, or M6 behavior was implemented.
- Added M5 desktop UI tests using GoogleTest and a Qt Widgets event loop, without adding Qt Test or new third-party dependencies.

## Files Added
- `ui/MainWindow.h`
- `ui/MainWindow.cpp`
- `ui/SearchWidget.h`
- `ui/SearchWidget.cpp`
- `tests/unit/test_desktop_ui.cpp`

## Files Modified
- `CMakeLists.txt`
- `adapters/qt_ipc/LocalIpcClient.h`
- `adapters/qt_ipc/LocalIpcClient.cpp`
- `adapters/qt_ipc/LocalIpcServer.h`
- `adapters/qt_ipc/LocalIpcServer.cpp`
- `apps/desktop/main.cpp`
- `src/index/SearchEngine.cpp`
- `src/service/ServiceController.cpp`
- `tests/unit/test_search_engine.cpp`
- `tests/unit/test_service_controller.cpp`
- `IMPLEMENTATION_STATUS.md`

## Files Removed
- `ui/.gitkeep`

## Tests / Commands Run
- `git branch --show-current`
  - Result: `feat/m5-qt-desktop-ui`.
- `git status`
  - Result before implementation: clean.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m4-msvc -DQt6_DIR="D:\Qt\6.11.2\msvc2022_64\lib\cmake\Qt6" -DCOREDESK_BUILD_TESTS=ON -DCOREDESK_FETCH_DEPS=ON -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="D:\Projects\CoreDesk\build-m0-clean-verify\_deps\nlohmann_json-src" -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="D:\Projects\CoreDesk\build-m0-clean-verify\_deps\googletest-src"`
  - Result after manual acceptance fix: configure passed using the existing MSVC build directory and current M5 source. The first fresh Visual Studio generator attempt in `build-m5-msvc` failed because the current shell did not expose a usable MSVC compiler to CMake; this was an environment/generator issue, not a source-code error.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m4-msvc --config Debug`
  - Result after manual acceptance fix: build passed after loading a sanitized MSBuild environment. Generated targets include `coredesk_desktop.exe`, `coredesk_desktop_tests.exe`, `coredesk_service.exe`, `coredesk_integration_tests.exe`, and `coredesk_tests.exe`.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m4-msvc -C Debug --output-on-failure`
  - Result after manual acceptance fix: passed, 107 total registered / 105 passed / 0 failed / 2 skipped.
  - Skipped tests: the two existing FileScanner directory symlink tests, because directory symlink creation is unavailable in this Windows environment.
  - M5 CTest entry: `coredesk_desktop_tests`, passed.
- `.\build-m4-msvc\Debug\coredesk_cli.exe --version`
  - Result: `CoreDesk 1.0.0`.
- `Select-String -Path ui\*.h,ui\*.cpp,apps\desktop\main.cpp -Pattern 'FileScanner|SearchEngine|IndexBuilder|ServiceController|coredesk/service|coredesk/filesystem|coredesk/index|std::thread|QTcpServer|QTcpSocket|FileChunk|FileOffer|SHA-256|TcpTransfer'`
  - Result: no matches.
- `Select-String -Path build-m4-msvc\coredesk_desktop.vcxproj,build-m4-msvc\coredesk_qt_ipc.vcxproj -Pattern 'coredesk_service_lib|coredesk_filesystem|coredesk_index'`
  - Result: no matches. `coredesk_desktop` does not depend on service core, filesystem, or index.
- `git diff --check`
  - Result: no whitespace errors; only CRLF working-copy warnings on modified text files.
- `.\build-m4-msvc\Debug\coredesk_cli.exe search D:\CoreDesk_M5_ManualTest project`
  - Result after manual acceptance fix: `indexed_records: 100100`, `hits: 100`, `elapsed_us: 601533`.
- `.\build-m4-msvc\Debug\coredesk_cli.exe search D:\CoreDesk_M5_ManualTest project_report_0999`
  - Result after manual acceptance fix: `indexed_records: 100100`, `hits: 100`, `elapsed_us: 1604`.

## M5 Automated Test Coverage
- Search debounce: rapid `a` -> `ab` -> `abc` input produces only one debounced request for `abc`.
- Empty trimmed query clears results and does not send a search request.
- Search results table renders at most 100 rows.
- Stale search response does not overwrite the newer request's displayed results.
- Query text changes before the next debounce fires invalidate the previous active search request, so the previous response cannot update the table during the debounce window.
- Clearing the query invalidates the previous active search request, clears the table, and prevents a delayed old response from repopulating results.
- Non-empty query changes clear visible stale result rows immediately while the next debounced search is pending.
- Shared `SearchEngine` concurrent searches on one immutable snapshot complete correctly while exercising cache paths.
- Search result `modified_ms` is validated against `system_clock` epoch semantics.
- Successful scan completion clears a previous scan cancelled/failure status message.
- Scan progress and completion frames update scan UI state only when the `request_id` matches the active scan.
- Disconnect invalidates pending UI state and shows Offline.

## Manual UI Acceptance
- PASS after human re-acceptance.
- Service auto-start / reconnect: PASS. With `coredesk_service` not running, `coredesk_desktop` recovered connection through Retry and returned the UI to Ready.
- 100,000 file scan responsiveness: PASS. `D:\CoreDesk_M5_ManualTest` contained 100100 entries; scan completed successfully, the window remained responsive, and no Not Responding state appeared.
- Scan Cancel: PASS. Cancel during scan showed `scan cancelled`, and a later Scan started and completed normally.
- Search debounce automatic search: PASS. Typing a search keyword sent `SearchRequest` automatically after debounce without clicking Scan, and results displayed normally.
- Search performance: PASS. Broad-query performance improved, search no longer blocked the UI, and the previous long wait did not recur.
- Request correlation / stale response protection: PASS. Changing the search keyword showed the new query results, and old search results did not overwrite newer results.

## Known Issues
- Qt runtime tests require `D:\Qt\6.11.2\msvc2022_64\bin` on the current process `PATH`, so that Qt Debug DLLs can be found. Qt DLLs were not copied into the project or system directories.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent source cache allowed configure/build/test verification to complete.
- The two directory symlink FileScanner tests remain skipped on this machine because directory symlink creation is not available in the current Windows environment.
- The M3 non-blocking performance note remains: `FrameDecoder::push()` uses `vector::erase(begin, ...)`, which can add data movement for many tiny frames. This does not affect M5 correctness or DoD.

## Deviations from Spec
- None.

## Next Milestone
M6 - TCP LAN Transfer (NOT STARTED)
