# CoreDesk Build and Platform Verification Guide

This guide describes the supported CMake feature combinations, reproducible
build commands, and the scope that has actually been verified. It does not
claim that an untested platform configuration is unsupported.

## Dependencies

CoreDesk requires CMake 3.24 or newer and a C++20 compiler. The approved
third-party dependencies are:

- nlohmann/json for protocol JSON payloads.
- GoogleTest when `COREDESK_BUILD_TESTS=ON`.
- Qt 6 Core and Network when either the UI or network feature is enabled.
- Qt 6 Widgets when `COREDESK_BUILD_UI=ON`.

CMake first tries installed package configurations. With
`COREDESK_FETCH_DEPS=ON` (the default), it uses FetchContent for nlohmann/json
or GoogleTest when the corresponding package is not found, so a normal fresh
configure may require network access. An offline build can point FetchContent
at existing source trees without changing this repository:

```text
-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=<path-to-nlohmann-json-source>
-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<path-to-googletest-source>
```

These are optional configure-time overrides. No machine-local dependency path
is a CoreDesk build requirement.

## CMake feature matrix

| UI | Network | Resulting scope | Qt requirement |
|---|---|---|---|
| OFF | OFF | Pure C++ Core, CLI, Core benchmarks, and Core tests | None |
| OFF | ON | Service, Local IPC, Qt Network/TCP transfer, no Desktop | Qt Core + Network |
| ON | OFF | Desktop, Service, and Local IPC; no TCP transfer backend | Qt Core + Widgets + Network |
| ON | ON | Full Windows application graph: Desktop, Service, Local IPC, and TCP transfer | Qt Core + Widgets + Network |

The `UI=ON, NETWORK=OFF` combination was rebuilt successfully after M8-A.
The Desktop target links `coredesk_qt_ipc` and Qt Widgets; it does not link
`coredesk_qt_network` or `coredesk_service_lib`. Local IPC itself uses the Qt
Network module because `QLocalSocket` and `QLocalServer` are provided there.

## Windows build

The full application was verified on Windows 11 with Visual Studio 2022
Community, MSVC v143 x64, Qt 6.11.2 `msvc2022_64`, and a multi-config CMake
generator. Replace the example paths with local paths; they are not product
defaults or permanent environment requirements.

```powershell
$Repo = "C:\path\to\CoreDesk"
$QtPrefix = "C:\path\to\Qt\6.x\msvc2022_64"
$Build = Join-Path $Repo "build-windows"

cmake -S $Repo -B $Build `
  -G "Visual Studio 17 2022" -A x64 -T v143 `
  -DCOREDESK_BUILD_UI=ON `
  -DCOREDESK_BUILD_NETWORK=ON `
  -DCOREDESK_BUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH=$QtPrefix
cmake --build $Build --config Debug
ctest --test-dir $Build -C Debug --output-on-failure
```

To run the Qt executables directly from that shell without installing Qt
system-wide, temporarily prepend its binary directory:

```powershell
$env:PATH = "$QtPrefix\bin;$env:PATH"
& "$Build\Debug\coredesk_service.exe"
& "$Build\Debug\coredesk_desktop.exe"
```

Set `COREDESK_BUILD_UI` and `COREDESK_BUILD_NETWORK` to the desired matrix row
when validating a smaller graph. Keep `COREDESK_BUILD_TESTS=ON` when test
evidence is required.

### Verified Windows result

The full `UI=ON, NETWORK=ON, TESTS=ON` Debug configuration produced:

- CTest: 132 total, 130 passed, 0 failed, 2 skipped.
- Skipped tests:
  - `FileScannerTest.SymlinkDoesNotForceRecursiveFollow`
  - `FileScannerTest.FollowDirectorySymlinksAvoidsCycles`

Those skips reflect that the Windows test environment could not create the
required directory symlinks. Both tests executed and passed in the Linux
normal and ASan runs; they are not known product failures.

## Linux Core + tests

The verified Linux scope is **Core + tests**, not the full Qt application. The
post-M8-A regression used Ubuntu 24.04.3 LTS under WSL 2, GCC/G++ 13.3, CMake
3.28.3, and Ninja 1.11.1. It configured with UI and network disabled, so Qt was
not required, and passed 130/130 tests.

```bash
repo=/path/to/CoreDesk
build=/path/to/build-coredesk-linux

cmake -S "$repo" \
  -B "$build" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCOREDESK_BUILD_UI=OFF \
  -DCOREDESK_BUILD_NETWORK=OFF \
  -DCOREDESK_BUILD_TESTS=ON \
  -DCOREDESK_ENABLE_ASAN=OFF \
  -DCOREDESK_ENABLE_TSAN=OFF
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
"$build/coredesk_cli" --version
```

If approved dependency sources must be reused offline, append the two
`FETCHCONTENT_SOURCE_DIR_*` overrides shown in the dependency section.

## Linux AddressSanitizer

The project-provided `COREDESK_ENABLE_ASAN` option adds
`-fsanitize=address -fno-omit-frame-pointer` to compilation and
`-fsanitize=address` to linking for non-MSVC targets.

```bash
repo=/path/to/CoreDesk
build=/path/to/build-coredesk-linux-asan

cmake -S "$repo" \
  -B "$build" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCOREDESK_BUILD_UI=OFF \
  -DCOREDESK_BUILD_NETWORK=OFF \
  -DCOREDESK_BUILD_TESTS=ON \
  -DCOREDESK_ENABLE_ASAN=ON \
  -DCOREDESK_ENABLE_TSAN=OFF
cmake --build "$build" --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  ctest --test-dir "$build" --output-on-failure
```

The Pre-M8 Linux ASan run passed 127/127 tests, linked `libasan.so.8`, and
reported no AddressSanitizer error or LeakSanitizer leak. After M8-A changed
pure-Core protocol code, the non-sanitized Linux Core regression passed
130/130. M8-A closure then added only Qt integration tests and documentation,
so ASan was not rerun and this guide does not claim an ASan 130/130 result.

## Platform verification status

| Platform/configuration | Status | Evidence boundary |
|---|---|---|
| Windows full application (`UI=ON`, `NETWORK=ON`) | VERIFIED | Build, full CTest, TCP integration, and Desktop outgoing-transfer smoke |
| Linux Core + tests (`UI=OFF`, `NETWORK=OFF`) | VERIFIED | Post-M8-A Core regression: 130/130 |
| Linux Core + tests with ASan | VERIFIED | Pre-M8 run: 127/127; `libasan.so.8`; LeakSanitizer clean |
| Linux Qt Desktop | NOT VERIFIED | Static review found no known blocker; no Linux Qt build/runtime evidence |
| Linux Qt Local IPC/service | NOT VERIFIED | Static review found no known blocker; no Linux Qt build/runtime evidence |
| Linux Qt TCP adapter | NOT VERIFIED | Static review found no known blocker; no Linux Qt build/runtime evidence |

Normative v1.0 requires one platform to run completely and the second platform
to compile Core + tests, with a complete second-platform run only when that
environment is already available. Windows supplies the complete application
evidence and Linux supplies the required second-platform Core evidence. Linux
Qt installation and GUI smoke are therefore not required for this closure.

## Deferred verification and features

Linux Qt build/runtime verification, Linux GUI smoke, TSan, and installer or
package generation remain deferred. Product features such as outgoing
progress, cancellation, timeout handling, transfer history, resume, discovery,
and logger rotation are also outside this build-closure work.
