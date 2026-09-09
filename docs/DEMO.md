# CoreDesk Short Demo Guide

This guide presents CoreDesk's two main user-visible capabilities in a target
duration of 1–2 minutes: indexed filename/path search and a single-file TCP
transfer. It is a reproducible presentation flow, not a benchmark or test
report.

## Before the demo

Use a verified Windows full-application build with
`COREDESK_BUILD_UI=ON` and `COREDESK_BUILD_NETWORK=ON`. See
[BUILD.md](BUILD.md) for build and runtime setup instead of duplicating those
instructions here.

Prepare these items before presenting:

- A small `<demo-data>` directory containing several files with recognizable
  names, including one such as `architecture-notes.md`.
- A small regular file such as `<demo-data>/transfer-sample.txt`.
- An empty `<receive-dir>` that is different from `<demo-data>`.
- A unique transfer filename, so the main path does not start with a
  `TargetExists` rejection.

Do not generate a benchmark dataset during the presentation. Recorded
performance evidence is available in [PERFORMANCE.md](PERFORMANCE.md).

## Target 1–2 minute flow

The time ranges below are pacing targets, not a claim that every machine has
already completed the flow in exactly that time.

### 0:00–0:35 — Start and scan

1. Launch `coredesk_desktop` from the full Windows build.
2. Wait for the main status to show `Ready`. The Desktop connects to an
   existing local Service or starts `coredesk_service` automatically when it
   is not running. If it reaches `Offline`, use `Retry`.
3. Next to `Root`, select `<demo-data>` with `Browse`.
4. Select `Scan` and wait for scanning to finish. The status line should return
   to an idle state and show a nonzero entry count and index generation.

### 0:35–0:55 — Search

1. In the `Search` tab, enter `architecture`.
2. Show `architecture-notes.md` in the result table.
3. Briefly point out that the Desktop requested the search through Local IPC;
   the index remains owned by the Service.

The search is filename/path indexing. CoreDesk does not index file contents or
provide advanced content filters or previews.

### 0:55–1:30 — Transfer a file

1. Open the `LAN Transfer` tab.
2. While the receiver is disabled, use `Choose Folder` to select
   `<receive-dir>`. If it is already enabled, select `Disable LAN Transfer`
   first.
3. Select `Enable LAN Transfer`. Confirm that `Status` becomes `Enabled` and
   note the actual displayed `Port`.
4. In the outgoing section, use `Browse...` to select
   `<demo-data>/transfer-sample.txt`.
5. Enter `127.0.0.1` as `Host` and enter the receiver's displayed port.
6. Select `Send File`. The send state should move from `Sending...` to `Sent`.
7. Confirm that `transfer-sample.txt` exists in `<receive-dir>`.

`127.0.0.1` provides a deterministic single-machine demonstration of the same
TCP transfer path. It is not evidence that Linux Qt or cross-machine LAN
interoperability was exercised in this demo.

## Success checklist

- Scan completes and installs an index generation.
- The expected search result appears.
- The LAN receiver shows `Enabled` and a concrete port.
- `Send File` reaches `Sent`.
- The destination file exists with the expected name.

### Optional integrity check

The transfer protocol performs SHA-256 verification before reporting success.
If presentation time permits, compare the source and destination separately:

```powershell
Get-FileHash "<demo-data>\transfer-sample.txt" -Algorithm SHA256
Get-FileHash "<receive-dir>\transfer-sample.txt" -Algorithm SHA256
```

The two hash values should match. This optional command is not required for the
1–2 minute main flow.

## Optional TargetExists demonstration

Send the same file to the same receive directory a second time. The UI should
show `target file already exists`; the existing destination must not be
silently overwritten. Keep this outside the main timed flow unless the extra
failure-path demonstration is useful.

## Short fallback

- If the Desktop is `Offline`, select `Retry` and confirm that the Service
  executable is beside the Desktop executable.
- If the receiver is not `Enabled`, disable and enable it again, then use the
  port currently displayed by the UI.
- If a previous run left the destination file in `<receive-dir>`, remove or
  rename that demo artifact before restarting the main flow.

CoreDesk v1.0 is intended for a trusted LAN. It does not provide TLS,
authentication, or automatic peer discovery; the destination host and port
are entered explicitly.
