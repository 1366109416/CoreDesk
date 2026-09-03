# CoreDesk Implementation Status

## Current Milestone
M8 - Cross-platform Support + Portfolio/Interview Packaging (IN PROGRESS)

M8-A - v1.0 Outgoing Transfer Gap Closure (CLOSED)

Pre-M8 Stability Corrective Pass (DONE)

## Completed
- Implemented the M8-A single-file outgoing path from Desktop through Local IPC
  and the service-owned `TransferManager` to the existing bounded-memory
  `TcpTransferClient`.
- Added correlated Local IPC `SendFileRequest`, `SendFileAccepted`, and
  `SendFileResult` messages without changing the TCP transfer protocol.
- Added minimal Desktop file/host/port controls and Ready, Sending, Sent, and
  Error states; progress, cancellation, queuing, and transfer history remain
  intentionally out of scope.
- Added automated protocol, payload, Local IPC, TransferManager, Desktop, and
  end-to-end loopback coverage, including multi-chunk SHA verification, Busy,
  connection failure recovery, TargetExists recovery, and repeat sends.
- Completed the Windows Desktop manual outgoing-transfer smoke against
  `127.0.0.1:45827`:
  - Normal localhost send passed from
    `D:\Temp\CoreDeskSmoke\send\smoke-1.txt` to
    `D:\Temp\CoreDeskSmoke\receive\smoke-1.txt`; source and received SHA-256
    both equal `F66BF0D9CC00521B787046873C5A29F3831818F4EC5E525CDBDCE87CACCBAFC0`.
  - Re-sending `smoke-1.txt` passed the TargetExists check and displayed
    `target file already exists`.
  - Sending to port `45828` passed the failure-path check and displayed
    `Connection refused`; the application did not crash or hang and the send UI
    remained usable.
  - Retrying on `127.0.0.1:45827` passed for `smoke-2.txt`; source and received
    SHA-256 both equal
    `CBB94D493FC2591BA7BDC3E2C3CD8D54CD978F14AD37FCAC9383875519565BD6`.
- Preserved M0-M6 behavior, including CLI, scanner/index/search tests, FrameProtocol tests, Service + Local IPC integration tests, Qt Desktop UI tests, and TCP loopback transfer tests.
- Reused the existing M3 `FrameProtocol` for all Local IPC transfer-management messages and TCP LAN transfer messages.
- Implemented M6 TCP LAN transfer:
  - `TcpTransferServer`
  - `TcpTransferClient`
  - Hello / HelloAck
  - FileOffer / FileAccept / FileReject
  - FileChunk
  - FileFinish / FileResult
  - receiver-side `.part` file handling
  - receiver-side SHA-256 verification
  - sender-side streaming SHA-256 worker
  - sender-side chunked file sending
- Implemented M7 transfer-management protocol:
  - `EnableLanTransferRequest = 40`
  - `EnableLanTransferResponse = 41`
  - `DisableLanTransferRequest = 42`
  - `DisableLanTransferResponse = 43`
  - `SetReceiveDirectoryRequest = 44`
  - `SetReceiveDirectoryResponse = 45`
  - `GetTransferStatusRequest = 46`
  - `GetTransferStatusResponse = 47`
- Implemented JSON payload helpers for all M7 Local IPC transfer-management messages.
- Implemented `TransferManager` in the service composition layer.
- Integrated `TransferManager` into `coredesk_service` without modifying `ServiceController`.
- Integrated M7 transfer-management dispatch into `LocalIpcServer` through injected callbacks.
- Integrated M7 transfer-management request APIs into `LocalIpcClient`.
- Implemented `TcpTransferServer::active_transfer_count()` with real 0 / 1 receive-state semantics.
- Implemented Desktop LAN Transfer controls:
  - status display
  - port display
  - receive directory display
  - active transfer count display
  - Enable LAN Transfer
  - Disable LAN Transfer
  - Choose Folder while disabled
  - unavailable/offline/error states
- Changed service runtime behavior so LAN transfer is disabled by default and only listens after explicit `EnableLanTransferRequest`.
- Preserved architecture boundaries:
  - Desktop communicates through Local IPC only.
  - `TransferWidget` does not own or include IPC/network/service objects.
  - `coredesk_service_lib` remains pure C++.
  - `ServiceController` remains unchanged.

## Architecture
- `coredesk_desktop` owns:
  - `MainWindow`
  - `LocalIpcClient`
  - `SearchWidget`
  - `TransferWidget`
- `TransferWidget` is UI-only and emits user-operation callbacks to `MainWindow`.
- `MainWindow` owns request-id tracking and dispatches M7 responses to `TransferWidget`.
- `coredesk_service` owns:
  - `ServiceController`
  - `TransferManager` when `COREDESK_BUILD_NETWORK=1`
  - `LocalIpcServer`
- `LocalIpcServer` uses `TransferManagementHandlers` callbacks to access transfer-management capability.
- `TransferManager` owns `TcpTransferServer`.
- `ServiceController` does not know about TCP transfer or transfer management.
- `coredesk_desktop` does not link `coredesk_service_lib` or `coredesk_qt_network`.
- `coredesk_service_lib` does not depend on Qt Network.

## Files Added
- `adapters/qt_network/TcpTransferClient.h`
- `adapters/qt_network/TcpTransferClient.cpp`
- `adapters/qt_network/TcpTransferServer.h`
- `adapters/qt_network/TcpTransferServer.cpp`
- `apps/service/transfer/TransferManager.h`
- `apps/service/transfer/TransferManager.cpp`
- `tests/integration/test_tcp_transfer.cpp`
- `tests/integration/test_service_tcp_integration.cpp`
- `ui/TransferWidget.h`
- `ui/TransferWidget.cpp`

## Files Modified
- `CMakeLists.txt`
- `apps/service/main.cpp`
- `docs/PROTOCOL.md`
- `include/coredesk/protocol/JsonPayload.h`
- `include/coredesk/protocol/MessageTypes.h`
- `src/protocol/FrameCodec.cpp`
- `src/protocol/JsonPayload.cpp`
- `tests/integration/test_local_ipc.cpp`
- `tests/integration/test_tcp_transfer.cpp`
- `tests/unit/test_desktop_ui.cpp`
- `tests/unit/test_frame_protocol.cpp`
- `tests/unit/test_json_payload.cpp`
- `ui/MainWindow.h`
- `ui/MainWindow.cpp`
- `IMPLEMENTATION_STATUS.md`

## Files Removed
- `adapters/qt_network/.gitkeep`

## Tests / Commands Run
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m7-step3-network-on-ui-on -G "Visual Studio 17 2022" -A x64 -T v143 -DCOREDESK_BUILD_NETWORK=ON -DCOREDESK_BUILD_UI=ON -DCOREDESK_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=D:\Qt\6.11.2\msvc2022_64 -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=D:\Projects\CoreDesk\build-m0-clean-verify\_deps\nlohmann_json-src -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=D:\Projects\CoreDesk\build-m0-clean-verify\_deps\googletest-src`
  - Result: configure passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m7-step3-network-on-ui-on --config Debug`
  - Result: build passed.
  - `coredesk_desktop.exe`: generated.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m7-step3-network-on-ui-on -C Debug --output-on-failure`
  - Result: passed.
  - Total: 123
  - Passed: 121
  - Failed: 0
  - Skipped: 2
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m7-step3-network-off-ui-on -G "Visual Studio 17 2022" -A x64 -T v143 -DCOREDESK_BUILD_NETWORK=OFF -DCOREDESK_BUILD_UI=ON -DCOREDESK_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=D:\Qt\6.11.2\msvc2022_64 -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=D:\Projects\CoreDesk\build-m0-clean-verify\_deps\nlohmann_json-src`
  - Result: configure passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m7-step3-network-off-ui-on --config Debug --target coredesk_service coredesk_desktop`
  - Result: build passed.
  - `TransferManager.cpp`: not compiled.
  - `TcpTransferServer.cpp`: not compiled.
  - `TcpTransferClient.cpp`: not compiled.

## Skipped Tests
- `FileScannerTest.SymlinkDoesNotForceRecursiveFollow`
- `FileScannerTest.FollowDirectorySymlinksAvoidsCycles`
- Reason: directory symlink creation is unavailable in the current Windows environment.

## GUI Manual Smoke
- Result: PASS.
- Initial state was Disabled with port `-`, receive directory `D:/Temp/CoreDeskReceived`, and 0 active transfers.
- Enable started the receiver on port 45827 and correctly disabled Enable / Choose Folder while enabling Disable.
- Disable stopped the receiver and correctly enabled Enable / Choose Folder while disabling Disable.
- While disabled, Choose Folder changed the receive directory to `D:/Temp/CoreDeskReceived_M7_Test`; re-enabling preserved the new directory.
- Closing Desktop left `coredesk_service` running.
- Restarting Desktop reconnected to the existing service and restored Enabled, port 45827, the configured receive directory, and 0 active transfers.

## M7 Automated Test Coverage
- M7 protocol message types 40-47 are known and existing wire values remain stable.
- M7 JSON payload roundtrips and schema errors.
- TransferManager idempotent start/stop, receive directory validation, Busy behavior, and status.
- Local IPC management path:
  - Enable LAN transfer.
  - Repeated enable.
  - Disable LAN transfer.
  - Repeated disable.
  - Disable then enable again.
  - Get transfer status before/after enable.
  - Set receive directory while disabled.
  - Set receive directory while enabled returns Busy.
  - Malformed management payloads return typed ErrorResponse payloads.
  - Local IPC disconnect does not stop LAN receiver.
- Desktop UI:
  - TransferWidget offline, disabled, enabled, unavailable, pending, and error states.
  - GetTransferStatusResponse updates TransferWidget through MainWindow production frame path.
  - request-id mismatch does not update TransferWidget.
  - unavailable ErrorResponse updates TransferWidget.
  - Local IPC disconnect invalidates pending transfer status response.

## Known Issues
- `TcpTransferClient::send_file()` starts an async hash worker before the real `FileOffer` request is sent. M8-A keeps Desktop correlation at the Local IPC `request_id` boundary and uses callbacks for TCP completion; the internal `FileOffer` id is not exposed to Desktop.
- The default receive directory remains `temp/CoreDeskReceived` until the user changes it through the M7 Desktop control.
- M8-A provides one explicit file and one manually entered host/port per send. Peer lists, discovery, outgoing progress/cancel, transfer history, queuing, and persistent target settings are not implemented.
- Qt runtime tests require `D:\Qt\6.11.2\msvc2022_64\bin` on the current process `PATH`, so that Qt Debug DLLs can be found. Qt DLLs were not copied into the project or system directories.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent source cache allowed configure/build/test verification to complete.
- The M3 non-blocking performance note remains: `FrameDecoder::push()` uses `vector::erase(begin, ...)`, which can add data movement for many tiny frames.

## Pre-M8 Stability Corrective Pass

Status: DONE

### Step A - TCP correctness and bounded memory

- Added a 2 MiB sender high-water mark with one 256 KiB file chunk read per event-loop pump.
- Added bounded partial-write remainder handling for client data/control frames and server control frames.
- Added strict client response state/request/transfer correlation, including `FileReject` transfer-id correlation.
- Isolated server connection-local protocol failures so a foreign socket cannot clean up another socket's active transfer.
- Added minimum per-connection handshake/message-order enforcement and targeted integration coverage.
- TCP integration result: 32/32 passed.

### Step B1 - Logging

- Added the pure C++ `Logger` with synchronized complete-line file output and Debug/Info/Warning/Error levels.
- Added service, scan/index, Local IPC, TCP lifecycle/protocol, and structured `ErrorCode` logging.
- Added CR/LF sanitization and noexcept-safe logging/close/status behavior.
- Logger tests: 6/6 passed.
- Default service log path is `QStandardPaths::AppLocalDataLocation/logs/coredesk_service.log`; `COREDESK_LOG_FILE` provides an override. Rotation is not implemented.

### Step B2a - Windows benchmark evidence

- Recorded real Release search, scan, and loopback TCP transfer benchmarks on Windows 11; full evidence is summarized in `docs/PERFORMANCE.md`.
- Verified 256 KiB transfer chunks, a 2 MiB high-water mark, maximum measured combined pending data of 2,097,696 bytes, and no whole-file buffering.
- Recorded both 1 GiB performance modes: 53.384 MiB/s fast and 13.863 MiB/s slow, plus an earlier 14.1097 MiB/s slow run. Root cause remains unknown.
- Event-loop measurements use `Qt::PreciseTimer`, a 5 ms cadence, separate callback-interval/deadline-lateness metrics, and p95/p99 reporting.

### Step B2b - Linux Core and ASan evidence

- Fixed a real CMake portability defect: `UI=OFF/NETWORK=OFF` no longer requires Qt or registers Qt integration targets.
- Linux normal Core + tests: 127 total, 127 passed, 0 failed, 0 skipped.
- Linux ASan Core + tests: 127 total, 127 passed, 0 failed, 0 skipped.
- ASan/LeakSanitizer found no UAF, buffer overflow, double free, or leak report.
- Both directory-symlink tests that skip in the Windows environment executed and passed in Linux normal and ASan builds.
- Accurate scope: Linux Core + tests only; Linux Qt Desktop/IPC/TCP were not verified.

### Latest Windows regression

- Debug build with `COREDESK_BUILD_NETWORK=ON`, `COREDESK_BUILD_UI=ON`, and tests enabled: PASS.
- CTest: 132 total, 130 passed, 0 failed, 2 skipped.
- Skips remain the two Windows directory-symlink environment cases closed by Linux coverage.

## Corrective Known Finding

- Windows 1 GiB loopback transfer has reproducible performance variability: approximately 53 MiB/s in one v2 run and approximately 14 MiB/s in two slow runs. All completed with matching SHA-256 and bounded sender queues. Root cause is not isolated; this is not a known correctness failure.

## Deferred Technical Debt and Future Features

- Logger file rotation/retention is not implemented.
- Outgoing transfer progress, cancel, and timeout behavior remain deferred.
- Outgoing transfer history, queuing, peer discovery, and persistent target settings remain deferred product features.
- Receiver QFile write and incremental hash remain on the Qt event thread; workerization is deferred unless future isolated evidence justifies it.
- `FrameDecoder::push()` front erase may be optimized if many-tiny-frame profiling justifies the change.
- Linux Qt Desktop, Qt Local IPC runtime, and Qt TCP adapter verification remain for the formal cross-platform milestone.
- TSan was not run; normative M7 does not require it as a mandatory DoD item.

## Corrective Evidence Documents

- `docs/PERFORMANCE.md`
- `docs/BUG_POSTMORTEM.md`

## Corrective Files Added

- `include/coredesk/common/Logger.h`
- `src/common/Logger.cpp`
- `tests/unit/test_logger.cpp`
- `benchmarks/bench_scan.cpp`
- `benchmarks/bench_transfer.cpp`
- `docs/PERFORMANCE.md`
- `docs/BUG_POSTMORTEM.md`

## Corrective Files Modified

- `.gitignore`
- `CMakeLists.txt`
- `README.md`
- `IMPLEMENTATION_STATUS.md`
- `adapters/qt_ipc/LocalIpcServer.h/.cpp`
- `adapters/qt_network/TcpTransferClient.h/.cpp`
- `adapters/qt_network/TcpTransferServer.h/.cpp`
- `apps/service/main.cpp`
- `apps/service/transfer/TransferManager.h/.cpp`
- `include/coredesk/service/ServiceController.h`
- `src/service/ServiceController.cpp`
- `benchmarks/bench_search.cpp`
- `tests/integration/test_tcp_transfer.cpp`

## Deviations from Spec
- None for implemented M7 transfer-management behavior.

## Next Milestone
M8-B. M8 remains in progress and is not yet complete.
