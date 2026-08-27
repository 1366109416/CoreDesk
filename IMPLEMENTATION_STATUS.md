# CoreDesk Implementation Status

## Current Milestone
M7 - Transfer Management (DONE)

## Completed
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
- `TcpTransferClient::send_file()` starts an async hash worker before the real `FileOffer` request is sent. It returns success with a sentinel request id rather than the final `FileOffer` request id. Callback-based transfer completion works, but future Desktop send workflow may need clearer request-id semantics.
- `QTcpSocket::write()` results are not fully surfaced as structured transfer errors, and sender chunking currently does not implement explicit backpressure based on `bytesWritten`.
- The default receive directory remains `temp/CoreDeskReceived` until the user changes it through the M7 Desktop control.
- Desktop transfer sending workflow is not implemented in this M7 step. There is no Send File button, peer list, device discovery, transfer progress UI, transfer history, or persistent settings.
- Qt runtime tests require `D:\Qt\6.11.2\msvc2022_64\bin` on the current process `PATH`, so that Qt Debug DLLs can be found. Qt DLLs were not copied into the project or system directories.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent source cache allowed configure/build/test verification to complete.
- The M3 non-blocking performance note remains: `FrameDecoder::push()` uses `vector::erase(begin, ...)`, which can add data movement for many tiny frames.

## Deviations from Spec
- None for implemented M7 transfer-management behavior.

## Next Milestone
M8 - Packaging / Finalization (NOT STARTED)
