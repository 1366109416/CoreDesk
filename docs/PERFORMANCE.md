# CoreDesk Performance and Stability Evidence

This document records the measurements collected during the Pre-M8 stability
corrective pass. The numbers below are copied from retained raw output; they
are not estimates and they are not pass/fail thresholds.

## 1. Methodology and environment

Windows benchmarks were collected on 2026-09-01 with the following setup:

| Item | Value |
|---|---|
| CPU | Intel Core i9-14900HX |
| Logical processors | 32 |
| Installed RAM | 34,070,192,128 bytes (31.73 GiB) |
| OS | Windows 11 Home, version 10.0.26200, build 26200 |
| Compiler | MSVC 19.44.35228, v143 |
| Qt | 6.11.2 |
| CMake | 4.3.1-msvc1 |
| Build type | Release |
| Git HEAD | `141089da0f2626782f045e427b7ac4a0210059aa` |
| Branch | `fix/pre-m8-stability-hardening` |

The benchmark executables were built from that HEAD plus the uncommitted
Pre-M8 corrective working tree. These results therefore do not claim to come
from a clean or already published commit. Datasets, build trees, logs, and raw
outputs were kept on `D:`. The raw local artifacts are intentionally excluded
from Git.

Unless stated otherwise, aggregates are the median of three independently
recorded runs. Search and scan exercise the production core implementations.
Transfer uses the production `TcpTransferClient`, `TcpTransferServer`, frame
protocol, filesystem writes, and SHA-256 verification over loopback.

## 2. Search benchmark

The search benchmark builds an index from 100,000 synthetic records and runs
30 query iterations for the linear, indexed, and cached paths.

All runs produced 100,000 records, 100,006 tokens, 603,900 postings, one
indexed hit, a linear check count of 20, and checksum 856.

| Run | Index build (us) | Linear avg (us) | Linear median (us) | Indexed avg (us) | Indexed median (us) | Cached avg (us) | Cached median |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 500,248 | 1,142 | 1,112 | 2 | 2 | 0 | 200 ns |
| 2 | 468,494 | 1,255 | 1,159 | 3 | 2 | 0 | 200 ns |
| 3 | 462,158 | 1,249 | 1,278 | 3 | 2 | 0 | 200 ns |
| Median of runs | 468,494 | 1,249 | 1,159 | 3 | 2 | 0 | 200 ns |

The cached microsecond columns display `0 us` because of output precision; the
raw nanosecond measurement is approximately 200 ns and is included to avoid
presenting the cache result as zero-cost.

## 3. Scan benchmark

The scan benchmark calls the production `FileScanner` with worker counts 1,
2, 4, and 8. Every run processed all discovered records with zero skipped and
zero failed entries.

### 3.1 1,000-file dataset

Dataset: 1,000 files, 10 directories, 1,010 discovered/processed records.

| Workers | Run 1 (ms) | Run 2 (ms) | Run 3 (ms) | Median (ms) |
|---:|---:|---:|---:|---:|
| 1 | 18 | 18 | 18 | 18 |
| 2 | 13 | 13 | 12 | 13 |
| 4 | 9 | 9 | 9 | 9 |
| 8 | 8 | 6 | 7 | 7 |

### 3.2 10,000-file dataset

Dataset: 10,000 files, 100 directories, 10,100 discovered/processed records.

| Workers | Run 1 (ms) | Run 2 (ms) | Run 3 (ms) | Median (ms) |
|---:|---:|---:|---:|---:|
| 1 | 192 | 194 | 191 | 192 |
| 2 | 131 | 136 | 128 | 131 |
| 4 | 85 | 83 | 84 | 84 |
| 8 | 65 | 60 | 64 | 64 |

A 100,000-file scan was **NOT RUN**. Normative M7 does not prescribe a fixed
100,000-file scan dataset; the 1,000- and 10,000-file datasets provide the
recorded worker-scaling evidence. This is a measurement limitation, not a
failed test.

## 4. TCP transfer benchmark

All recorded transfers completed successfully and the source and received
SHA-256 values matched. The transfer timing includes sender preparation/hash,
handshake/offer, chunk transfer, finish, and the final `FileResult`. The v2
measurements freeze the completion timestamp in the terminal callback rather
than waiting for a subsequent timer sample.

### 4.1 Throughput runs

| Size | Run | Elapsed (ms) | Throughput (MiB/s) | SHA-256 |
|---:|---:|---:|---:|---|
| 10 MiB | 1 | 215 | 46.5116 | Match |
| 10 MiB | 2 | 205 | 48.7805 | Match |
| 10 MiB | 3 | 234 | 42.7350 | Match |
| 100 MiB | 1 | 1,944 | 51.4403 | Match |
| 100 MiB | 2 | 1,942 | 51.4933 | Match |
| 100 MiB | 3 | 1,979 | 50.5306 | Match |
| 1 GiB, original run | 1 | 72,574 | 14.1097 | Match |
| 1 GiB, v2 | 1 | 19,181.897 | 53.384 | Match |
| 1 GiB, v2 | 2 | 73,864.472 | 13.863 | Match |

The three-run medians were 215 ms / 46.5116 MiB/s for 10 MiB and
1,944 ms / 51.4403 MiB/s for the original 100 MiB series. The v2 100 MiB
series, collected with the corrected completion boundary and responsiveness
sampler, measured 43.422, 43.913, and 39.359 MiB/s.

### 4.2 Bounded-memory evidence

| Property | Value |
|---|---:|
| File chunk | 262,144 bytes (256 KiB) |
| Sender high-water mark | 2,097,152 bytes (2 MiB) |
| Maximum observed Qt pending bytes | 2,097,696 |
| Maximum observed application remainder | 0 |
| Maximum observed combined pending bytes | 2,097,696 |

At the measured pending peak, the send offset remained far below total file
size (for example 4,194,304 bytes in the fast 1 GiB v2 run and 19,136,512
bytes in the slow run). The sender therefore did not buffer the whole file.
The application-side partial-write remainder is structurally limited to one
encoded frame; zero observed remainder means loopback did not produce a
partial Qt `write()` acceptance in these runs, not that the remainder path is
unused or absent.

The guarantee covers `QTcpSocket::bytesToWrite()` plus the bounded
application-side frame remainder. It does not claim to bound memory owned by
the operating-system kernel socket buffers.

## 5. Known 1 GiB performance variability

The 1 GiB Windows loopback result is bimodal:

- Fast v2 run: 53.384 MiB/s.
- Slow v2 run: 13.863 MiB/s.
- Earlier slow run: 14.1097 MiB/s.

The slow mode was therefore reproduced, but a fast mode also exists. All runs
completed with matching SHA-256 and the same bounded sender queue. The root
cause is unknown. Filesystem cache, disk behavior, scheduling, antivirus,
thermal state, sender activity, and receiver IO/hash are plausible factors,
but none was isolated by this benchmark. This is a known performance
variability finding, not a demonstrated correctness failure.

## 6. Event-loop responsiveness

The v2 harness uses `Qt::PreciseTimer` with a 5 ms cadence and a monotonic
`std::chrono::steady_clock` nanosecond time source. It records two distinct
metrics:

- **Callback interval:** current callback timestamp minus the previous
  callback timestamp.
- **Deadline lateness:** `max(actual callback - expected fixed-cadence
  deadline, 0)`. Missed deadlines are skipped while the cadence grid is
  preserved, avoiding a catch-up event storm.

The values below are milliseconds. For the short baseline and 100 MiB rows,
each column is the median of the corresponding statistic from three runs.

| Mode | Samples | Interval median | Interval p95 | Interval p99 | Interval max | Lateness median | Lateness p95 | Lateness p99 | Lateness max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Idle short baseline | 720-726/run | 5.042 | 16.265 | 18.108 | 26.953 | 0.305 | 14.464 | 16.222 | 21.984 |
| 100 MiB v2 | 433-489/run | 5.014 | 7.793 | 11.098 | 28.965 | 0.637 | 4.065 | 9.521 | 26.569 |
| Idle long baseline | 5,383 | 15.264 | 19.343 | 22.249 | 37.364 | 12.540 | 17.351 | 20.243 | 33.219 |
| 1 GiB v2 fast | 3,821 | 5.031 | 6.728 | 8.166 | 21.619 | 0.571 | 2.595 | 3.762 | 18.506 |
| 1 GiB v2 slow | 3,604 | 15.700 | 59.348 | 99.611 | 167.691 | 13.209 | 56.622 | 95.828 | 165.997 |

The 100 MiB workload does not show stable percentile degradation relative to
the short idle baseline. The 1 GiB runs show the same fast/slow split as
throughput. The sampler, client pump, server, receiver file writes, receiver
incremental hash, and socket callbacks share the benchmark Qt event thread;
the slow result cannot be attributed to the receiver alone.

Normative M7 defines no fixed event-loop latency threshold. These observations
are retained as evidence and as a known variability finding, not converted
into an invented pass/fail threshold.

## 7. Linux Core and sanitizer verification

Linux evidence was collected on 2026-09-02 using Ubuntu 24.04.3 LTS under WSL
2, kernel `6.6.87.2-microsoft-standard-WSL2`, GCC 13.3.0, CMake 3.28.3, and
Ninja 1.11.1. The build used `RelWithDebInfo`, `COREDESK_BUILD_UI=OFF`,
`COREDESK_BUILD_NETWORK=OFF`, and `COREDESK_BUILD_TESTS=ON`.

The accurate scope is **Linux Core + tests verified**, not the full Linux
application:

| Configuration | Total | Passed | Failed | Skipped |
|---|---:|---:|---:|---:|
| Normal | 127 | 127 | 0 | 0 |
| ASan | 127 | 127 | 0 | 0 |

Core-only configuration initially exposed a real CMake portability defect:
Qt was still required when both UI and network were disabled. The fix gates Qt
lookup, Qt application targets, and Qt integration tests behind
`COREDESK_BUILD_UI OR COREDESK_BUILD_NETWORK`.

### 7.1 AddressSanitizer

The ASan build used `-fsanitize=address -fno-omit-frame-pointer`, and the test
binary linked `libasan.so.8`. CTest ran with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`.

- AddressSanitizer errors: none.
- Heap/stack buffer overflow: none.
- Use-after-free: none.
- Double free: none.
- LeakSanitizer report: none (clean).
- TSan: **NOT RUN**. Normative M7 requires a clean Linux ASan core run when the
  environment is available; it does not require a TSan run.

### 7.2 Symlink coverage

On Windows,
`FileScannerTest.SymlinkDoesNotForceRecursiveFollow` and
`FileScannerTest.FollowDirectorySymlinksAvoidsCycles` are skipped when the
environment cannot create directory symlinks. Both tests executed and passed
in the Linux normal build and again in the Linux ASan build. This closes the
environment-specific Windows coverage gap; it is not a known functional
failure.

## 8. Limitations and interpretation

- Only Linux Core + tests were verified. Linux Qt Desktop, Qt Local IPC service
  executable, and Qt TCP adapter were not built or run.
- The root cause of the Windows 1 GiB loopback performance variability remains
  unknown.
- The transfer benchmark exercises the TCP adapters directly and does not
  inject the complete service lifecycle logger. Production logging is limited
  to lifecycle/aggregate/error events rather than per-chunk logging, so this is
  considered a measurement limitation rather than evidence of hidden
  per-chunk overhead.
- TSan was not run.
- A 100,000-file scan dataset was not run.
- These results describe this machine, build, and corrective working tree;
  they are not cross-platform throughput guarantees.