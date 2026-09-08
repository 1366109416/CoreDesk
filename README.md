# CoreDesk

CoreDesk is a C++20 desktop file indexer that pairs a thin Qt client with a
local service for responsive search and verified single-file transfers across
a trusted LAN.

## Highlights

- **Process separation:** the Qt Desktop handles interaction and presentation;
  a long-lived local Service owns scanning, indexes, and transfers.
- **Correlated IPC:** Desktop and Service communicate through a framed
  `QLocalSocket` / `QLocalServer` protocol with explicit request correlation.
- **Responsive indexed search:** scans build a replacement index snapshot while
  the current snapshot remains available for search.
- **Bounded execution and transfer:** scanning uses bounded worker execution;
  TCP sending uses chunked reads, backpressure, and bounded application queues.
- **Integrity and failure handling:** received files use temporary targets,
  SHA-256 verification, structured errors, and terminal-state recovery.
- **Evidence over claims:** the repository includes documented build
  instructions, real benchmarks, Windows and Linux Core test results,
  sanitizer evidence, and a pre-merge bug postmortem.

## Core Features

- Recursively scan a user-selected directory and build a filename/path index.
- Search from the Desktop while the Service owns the active index lifecycle.
- Enable or disable a LAN receiver and select its destination directory.
- Send one regular file to a manually entered host and port through the
  Desktop-to-Service IPC path.
- Reject existing targets, validate the completed byte count and SHA-256, and
  report a clear terminal success or error to the Desktop.

## Architecture

The Desktop is intentionally thin: it owns widgets, user input, status display,
service startup, and the Local IPC client. The Service owns scan/index state,
the Local IPC server, and both receiving and outgoing TCP transfer lifecycles.
The Desktop does not perform direct TCP file transfer.

```text
Qt Desktop -> Local IPC -> Local Service
                              |-> Core scan, index, and search
                              `-> TransferManager -> Qt TCP receiver/sender
```

Pure C++ Core targets can be built and tested without Qt when both UI and
network features are disabled. See
[Architecture](docs/ARCHITECTURE.md) for the component model, thread model,
ownership boundaries, and scan/search and transfer flows.

## Performance

Representative Release measurements from the documented Windows 11 system are
summarized below. They characterize that environment and are not throughput or
latency guarantees.

| Area | Recorded evidence |
|---|---|
| Search | 100,000 records, 100,006 tokens, and 603,900 postings: linear median 1,159 us, indexed median 2 us, cached median about 200 ns |
| Scan | 10,000 files plus 100 directories: 192 / 131 / 84 / 64 ms median with 1 / 2 / 4 / 8 workers |
| Transfer | 100 MiB three-run median: 51.4403 MiB/s with matching SHA-256 |
| Sender buffering | 256 KiB chunks, 2 MiB Qt write high-water, and 2,097,696 bytes maximum observed combined application-level pending data; no whole-file buffering |

The 1 GiB loopback benchmark showed two observed modes: 53.384 MiB/s and
13.863–14.1097 MiB/s. Every completed run matched SHA-256, but the cause of the
run-to-run performance variability has not been identified. The buffering
figures above do not include operating-system kernel socket memory. See the
[Performance Report](docs/PERFORMANCE.md) for methodology, complete results,
limitations, and responsiveness measurements.

## Reliability and Testing

| Validation | Result |
|---|---|
| Windows full configuration | 132 total, 130 passed, 0 failed, 2 skipped |
| Linux Core regression after outgoing-transfer work | 130 / 130 passed |
| Earlier Linux Core ASan/LSan run | 127 / 127 passed; no sanitizer or leak report |

The two Windows skips require directory-symlink creation that was unavailable
in that test environment. Both tests executed and passed in the Linux normal
and ASan runs; they are not known product failures.

The repository also retains a
[pre-merge bug postmortem](docs/BUG_POSTMORTEM.md) for a progress-callback
exception that could cross a `std::thread` entry boundary, including the fix
and regression coverage.

## Build and Run

The full application is verified on Windows 11 with Visual Studio 2022 and Qt
6.11.2. The Core-only configuration does not require Qt. See the
[Build and Platform Verification Guide](docs/BUILD.md) for dependencies,
copyable commands, feature combinations, and tested boundaries.

For a short scan/search and loopback file-transfer walkthrough, use the
[1–2 minute Demo Guide](docs/DEMO.md).

## Platform Verification

| Scope | Status |
|---|---|
| Windows full application | **VERIFIED** |
| Linux Core + tests | **VERIFIED** |
| Linux Core ASan/LSan | **VERIFIED** |
| Linux Qt Desktop | **NOT VERIFIED** |
| Linux Qt Local IPC/service | **NOT VERIFIED** |
| Linux Qt TCP | **NOT VERIFIED** |

Linux Qt application and network verification remains deferred; `NOT VERIFIED`
does not mean that those paths are declared unsupported. Exact toolchains and
commands are recorded in the [Build Guide](docs/BUILD.md).

## Security Boundary and Non-goals

The current networking scope assumes a trusted LAN and must not be exposed as
an authenticated public file-sharing service. CoreDesk v1.0 does not provide:

- TLS or peer authentication;
- automatic peer discovery;
- transfer resume or transfer history;
- cloud synchronization; or
- an installer/package workflow.

Targets are entered manually, and the index covers filenames and paths rather
than file contents.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — components, threads, ownership, and
  data flows
- [Build and Platform Verification](docs/BUILD.md) — dependencies, commands,
  feature matrix, and validation scope
- [Protocol](docs/PROTOCOL.md) — framing, payloads, Local IPC, and TCP transfer
- [Performance](docs/PERFORMANCE.md) — benchmark methodology, results, and
  limitations
- [Demo](docs/DEMO.md) — short reproducible scan/search and transfer walkthrough
- [Bug Postmortem](docs/BUG_POSTMORTEM.md) — pre-merge failure analysis and
  regression coverage
- [Implementation Status](IMPLEMENTATION_STATUS.md) — milestone and engineering
  history
