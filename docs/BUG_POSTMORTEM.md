# Bug Postmortem: Exception Escape from the Scan Worker

## 1. Summary

During M4 service work, review of the asynchronous scan path identified a
dangerous exception boundary: a user-supplied progress callback executes from
the `ServiceController` scan `std::thread`. If that callback throws and the
exception escapes the thread entry function, the C++ runtime calls
`std::terminate`.

This was a pre-merge implementation defect, not a production incident. The
earliest committed M4 implementation already contains the corrected thread
boundary, so this document does not invent an outage timeline or claim that a
released build crashed.

## 2. Symptom

A throwing progress callback could terminate the entire service process
instead of completing the scan with a structured error. Because uncaught
exceptions in a `std::thread` entry function do not propagate back to the
thread creator, normal caller-side exception handling cannot recover the
operation.

## 3. Impact

The potential impact was disproportionate to the originating failure:

- The long-lived service process could exit through `std::terminate`.
- The active scan would not produce a normal completion result.
- Service scan state could remain externally unobservable or inconsistent.
- The existing index and subsequent scan availability could be lost with the
  process.

No data-corruption incident was observed. The risk was abrupt process
termination and loss of service availability.

## 4. Root cause

`ServiceController::start_scan()` launches a `std::thread`. The thread invokes
`FileScanner::scan()`, which calls the injected progress callback. A callback
is arbitrary C++ code and can throw. In C++, an exception that escapes the
top-level function of a `std::thread` causes `std::terminate`; it is not
transported to the initiating thread.

The defect was therefore an incomplete ownership/error boundary: the service
owned the thread but had not yet guaranteed that every exception crossing its
entry point was converted into the project's `Result`/`Error` model.

## 5. Why it was dangerous

The callback normally originates in an adapter, so the failure could look like
an IPC or reporting problem while actually crossing a process-critical C++
thread boundary. Catching only scanner-returned `Result` errors would not help:
a C++ exception bypasses that return path entirely.

The completion callback is another extension point. It must also be protected
so an exception raised while reporting completion cannot escape the worker
thread after the scan state has been restored.

## 6. Fix

The scan worker entry is protected with both:

- `catch (const std::exception&)`, converted to `ErrorCode::InternalError`
  with the exception message.
- `catch (...)`, converted to `ErrorCode::InternalError` with a stable fallback
  message.

Before completing the failed operation, `finish_scan_state(false)` restores
the controller to `Ready` when an older snapshot exists, or `NoIndex` when it
does not. `complete_scan()` is `noexcept` and catches exceptions thrown by the
completion callback itself.

The result is a failed scan operation, not a terminated service process. The
controller remains able to accept later scans.

## 7. Regression test

The production path is covered by:

`ServiceControllerTest.ProgressCallbackExceptionReturnsInternalErrorAndStateRecovers`

The test deliberately throws `std::runtime_error` from the progress callback
and verifies that:

1. The completion result is a failure with `ErrorCode::InternalError`.
2. With no previous snapshot, controller state returns to `NoIndex`.
3. A second scan request is accepted.
4. The second scan completes successfully and controller state becomes
   `Ready`.

## 8. Verification

The regression test passed in all of the latest relevant suites:

- Windows Debug full CTest: 128 total, 126 passed, 0 failed, 2 environment
  skips.
- Linux normal Core + tests: 127/127 passed.
- Linux ASan Core + tests: 127/127 passed with no sanitizer report.

The Windows skips are unrelated directory-symlink environment checks.

## 9. Lessons learned

- Every `std::thread` entry point is an exception boundary, even when all
  ordinary operations return `Result<T>`.
- Injected callbacks are untrusted control flow and require the same boundary
  protection as filesystem or protocol input.
- Error conversion must be paired with state restoration; merely catching the
  exception would still leave the service stuck in `Scanning`.
- A useful regression test verifies recovery with a subsequent operation, not
  only the first error code.

## 10. Interview takeaway

This bug demonstrates a practical distinction between synchronous exception
propagation and C++ thread failure semantics. The fix is intentionally small:
protect the thread entry, convert failures into the existing structured error
model, restore invariants, make completion reporting non-throwing, and prove
continued service availability with a second scan.