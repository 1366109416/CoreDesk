# CoreDesk

CoreDesk is a C++20 desktop file indexing and LAN transfer client. The
normative implementation specification lives in `docs/COREDESK_SPEC.md`.

The completed stability work includes bounded-memory TCP sending, checked
partial socket writes, connection-local receive isolation, a thread-safe file
logger, real Windows benchmarks, and Linux Core/ASan evidence. M8 is currently
in progress; its outgoing-transfer and build-closure stages are complete.

## Verified stability and performance

Representative Release measurements on an Intel Core i9-14900HX Windows 11
machine are shown below. They describe this machine and corrective working
tree, not universal performance guarantees.

| Area | Representative result |
|---|---|
| Search | 100,000 records: linear median 1,159 us; indexed median 2 us; cached median approximately 200 ns |
| Scan | 10,000 files + 100 directories: median 192/131/84/64 ms with 1/2/4/8 workers |
| Transfer | 100 MiB original three-run median: 51.4403 MiB/s; SHA-256 matched |
| Large transfer | 1 GiB loopback observed both 53.384 MiB/s and 13.863-14.1097 MiB/s modes; SHA-256 matched |

The 1 GiB variation is a known, non-correctness performance finding. Its root
cause has not been isolated, and the slower runs are intentionally retained.
The sender uses 256 KiB chunks and a 2 MiB high-water mark; measured maximum
Qt pending plus application remainder was 2,097,696 bytes, with no whole-file
buffering.

Full methodology, raw-run summaries, event-loop percentile terminology,
limitations, and Linux sanitizer evidence are in
[`docs/PERFORMANCE.md`](docs/PERFORMANCE.md). The real scan-worker exception
case study is in [`docs/BUG_POSTMORTEM.md`](docs/BUG_POSTMORTEM.md).

## Linux verification scope

Linux verification currently covers **Core + tests**, not the full Qt
application:

- Ubuntu 24.04.3 LTS / GCC 13.3 post-M8-A Core regression: 130/130 passed.
- Ubuntu 24.04.3 LTS / GCC 13.3 Pre-M8 ASan: 127/127 passed, no sanitizer report.
- Both directory-symlink tests skipped by the Windows environment executed and
  passed in Linux normal and ASan builds.

Linux Qt Desktop, Qt Local IPC service execution, and Qt TCP adapters have not
yet been verified.

## Logging

The service writes to
`QStandardPaths::AppLocalDataLocation/logs/coredesk_service.log` by default.
Set `COREDESK_LOG_FILE` to override the destination, for example to a path on
`D:` when the system drive is constrained:

```powershell
$env:COREDESK_LOG_FILE = "D:\CoreDeskLogs\coredesk_service.log"
```

The override is not a hard-coded product default. Log rotation is not
implemented in v1.0.

## Security boundary and non-goals

CoreDesk v1.0 is intended for a trusted LAN demonstration. It does not provide
TLS, authentication, automatic device discovery, cloud accounts, P2P
traversal, database persistence, directory synchronization, or conflict
resolution. It does not index file contents. Transfer input still uses framed
schema validation, basename/path-traversal checks, `.part` files, target-exists
rejection, and SHA-256 verification.

## Build

See the [Build and Platform Verification Guide](docs/BUILD.md) for the Windows
feature matrix, full Windows build commands, Linux Core-only commands, Linux
ASan commands, dependency overrides, and exact tested-versus-not-verified
boundaries. Windows full application behavior is verified; Linux verification
covers Core + tests. Linux Qt Desktop, Local IPC/service, and TCP paths remain
not verified rather than being declared unsupported.
