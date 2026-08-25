# CoreDesk Implementation Status

## Current Milestone
M6 - TCP LAN Transfer (DONE)

## Completed
- Preserved M0-M5 behavior, including CLI, scanner/index/search tests, FrameProtocol tests, Service + Local IPC integration tests, and Qt Desktop UI tests.
- Reused the existing M3 `FrameProtocol` for all TCP LAN transfer messages; TCP transport does not implement a second frame header/parser.
- Implemented M6 protocol helpers for LAN transfer payloads:
  - `Hello`
  - `HelloAck`
  - `FileOffer`
  - `FileAccept`
  - `FileReject`
  - `FileFinish`
  - `FileResult`
  - binary `FileChunk`
- Implemented `TcpTransferServer` with `QTcpServer` / `QTcpSocket`.
- Implemented `TcpTransferClient` with `QTcpSocket`.
- Implemented Hello / HelloAck handshake over TCP.
- Implemented FileOffer / FileAccept / FileReject offer flow.
- Implemented FileChunk receive and send flow.
- Implemented FileFinish / FileResult completion flow.
- Implemented receiver-side `.part` file writing and cleanup.
- Implemented receiver-side SHA-256 verification before final rename.
- Implemented sender-side streaming SHA-256 calculation in a worker `QThread`, so full-file hashing does not block the Qt event loop.
- Implemented sender-side chunked file sending without reading the whole file into memory.
- Added receive-side protections for:
  - target exists
  - path traversal / unsafe basename
  - active transfer busy
  - wrong offset
  - file size overflow boundary
  - hash mismatch
  - disconnect cleanup
- Integrated TCP receive into the service composition:
  - `coredesk_service` links `coredesk_qt_network` only when `COREDESK_BUILD_NETWORK=ON`.
  - `apps/service/main.cpp` conditionally owns a `TcpTransferServer`.
  - `apps/service/main.cpp` creates the default receive directory and starts TCP listening.
- Added service composition integration coverage proving `ServiceController`, `LocalIpcServer`, and `TcpTransferServer` can coexist in the service process while a loopback TCP transfer succeeds.

## Architecture
- `coredesk_service` is the composition root for M6 TCP receive.
- `apps/service/main.cpp` owns:
  - `coredesk::service::ServiceController`
  - `coredesk::qt_ipc::LocalIpcServer`
  - `coredesk::qt_network::TcpTransferServer` when `COREDESK_BUILD_NETWORK=1`
- `ServiceController` was not modified for TCP transfer.
- No `TransferManager` was added.
- `coredesk_service_lib` remains pure C++ and does not depend on Qt Network.
- `coredesk_qt_network` remains an adapter target that depends on `coredesk_protocol`, `Qt6::Core`, and `Qt6::Network`.
- The default TCP receive directory is:
  - `std::filesystem::temp_directory_path() / "CoreDeskReceived"`

## Files Added
- `adapters/qt_network/TcpTransferClient.h`
- `adapters/qt_network/TcpTransferClient.cpp`
- `adapters/qt_network/TcpTransferServer.h`
- `adapters/qt_network/TcpTransferServer.cpp`
- `tests/integration/test_tcp_transfer.cpp`
- `tests/integration/test_service_tcp_integration.cpp`

## Files Modified
- `CMakeLists.txt`
- `apps/service/main.cpp`
- `docs/PROTOCOL.md`
- `include/coredesk/protocol/JsonPayload.h`
- `src/protocol/JsonPayload.cpp`
- `tests/unit/test_json_payload.cpp`
- `IMPLEMENTATION_STATUS.md`

## Files Removed
- `adapters/qt_network/.gitkeep`

## Tests / Commands Run
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S . -B build-m4-msvc -DQt6_DIR=D:\Qt\6.11.2\msvc2022_64\lib\cmake\Qt6 -DCOREDESK_BUILD_TESTS=ON -DCOREDESK_BUILD_NETWORK=ON -DCOREDESK_FETCH_DEPS=ON -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=D:\Projects\CoreDesk\build-m0-clean-verify\_deps\nlohmann_json-src -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=D:\Projects\CoreDesk\build-m0-clean-verify\_deps\googletest-src`
  - Result: configure passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build build-m4-msvc --config Debug`
  - Result: build passed.
- `D:\VSSSS\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir build-m4-msvc -C Debug --output-on-failure`
  - Result: passed.
  - Total: 115
  - Passed: 113
  - Failed: 0
  - Skipped: 2
- Skipped tests:
  - `FileScannerTest.SymlinkDoesNotForceRecursiveFollow`
  - `FileScannerTest.FollowDirectorySymlinksAvoidsCycles`
  - Reason: directory symlink creation is unavailable in the current Windows environment.

## M6 Automated Test Coverage
- Protocol helpers:
  - LAN transfer JSON payload roundtrips.
  - lowercase transfer id and SHA-256 validation.
  - uppercase transfer id / SHA-256 rejection.
  - FileChunk binary big-endian layout.
  - FileChunk payload size and length mismatch rejection.
- TCP transport:
  - Hello / HelloAck loopback handshake.
  - FileOffer accepted.
  - path traversal rejected.
  - target exists rejected.
  - invalid transfer id rejected.
  - busy receive rejected.
  - receiver writes a small file and removes `.part`.
  - wrong offset rejected and `.part` removed.
  - overflow boundary rejected without unsigned addition overflow.
  - disconnect cleanup removes `.part`.
  - hash mismatch rejected.
  - sender transfers small file.
  - sender transfers 10 MiB file.
  - sender SHA-256 preparation does not block Qt timer events.
  - sender file-not-found error.
  - receiver reject handling.
  - connection drop handling.
- Service composition:
  - `ServiceController` + `LocalIpcServer` + `TcpTransferServer` coexist.
  - `TcpTransferClient` loopback sends a file through the service-composed TCP server.
  - final file exists, `.part` is gone, and SHA-256 matches.

## Known Issues
- `TcpTransferClient::send_file()` now starts an async hash worker before the real `FileOffer` request is sent. It returns success with a sentinel request id rather than the final `FileOffer` request id. Callback-based transfer completion works, but future UI/IPC integration may need clearer request-id semantics.
- `QTcpSocket::write()` results are not fully surfaced as structured transfer errors, and sender chunking currently does not implement explicit backpressure based on `bytesWritten`.
- The default receive directory is currently `temp/CoreDeskReceived`; user-configurable receive directory is not implemented yet.
- UI / IPC control for enabling LAN transfer is not implemented yet. The service composition can start the TCP receiver when built with network support, but the M5 LAN Transfer tab remains a placeholder.
- Qt runtime tests require `D:\Qt\6.11.2\msvc2022_64\bin` on the current process `PATH`, so that Qt Debug DLLs can be found. Qt DLLs were not copied into the project or system directories.
- Fresh FetchContent downloads from GitHub may fail in the current environment due TLS credential/network reset errors. Reusing the existing local FetchContent source cache allowed configure/build/test verification to complete.
- The two directory symlink FileScanner tests remain skipped on this machine because directory symlink creation is not available in the current Windows environment.
- The M3 non-blocking performance note remains: `FrameDecoder::push()` uses `vector::erase(begin, ...)`, which can add data movement for many tiny frames.

## Deviations from Spec
- None for implemented M6 TCP transport and service composition behavior.
- LAN Transfer UI enable/disable control and user-configurable receive directory remain future integration work, not completed in this step.

## Next Milestone
M7 - Stability + Performance + Packaging (NOT STARTED)
