# CoreDesk Protocol

This document records the M3 FrameProtocol wire format. It must stay consistent
with `include/coredesk/protocol/MessageTypes.h`,
`include/coredesk/protocol/Frame.h`, and
`include/coredesk/protocol/FrameCodec.h`.

## Frame Header

Each frame starts with a fixed 24-byte header.

```text
Offset  Size  Field
0       4     magic = ASCII "CDSK"
4       2     version = 1
6       2     message_type
8       4     flags
12      8     request_id
20      4     payload_length
-----------------------------
Total   24
```

All multi-byte integer fields use network byte order, also called big endian.

The maximum payload length is 1 MiB. Future `FileChunk` payloads must also stay
within this limit.

## Message Types

Message type values are fixed wire values and must not be renumbered.

```text
1    Ping
2    Pong
10   ScanRequest
11   ScanAccepted
12   ScanProgress
13   ScanCompleted
14   ScanFailed
15   CancelScanRequest
20   SearchRequest
21   SearchResponse
30   StatusRequest
31   StatusResponse
100  Hello
101  HelloAck
110  FileOffer
111  FileAccept
112  FileReject
113  FileChunk
114  FileFinish
115  FileResult
```

Unknown message type values are protocol errors. The decoder validates the
numeric value before accepting a frame.

## Streaming Rules

The protocol is designed for byte streams. A transport read can contain part of
a header, part of a payload, one complete frame, multiple complete frames, or a
complete frame followed by part of another frame.

`FrameDecoder` keeps an internal buffer for incomplete bytes. It returns a frame
only after the full header and full payload for that frame have arrived.

`FrameDecoder::reset()` clears any buffered partial frame state.

## Error Rules

Malformed frame input maps to the shared CoreDesk `ErrorCode` values:

```text
bad magic             ProtocolError
bad version           ProtocolError
unknown message_type  ProtocolError
payload too large     PayloadTooLarge
```

After a severe protocol error, future transports should close the connection.
Connection management is implemented in later milestones, not in M3.

## JSON Payloads

JSON payload text is UTF-8. Field names are fixed by
`docs/COREDESK_SPEC.md`.

M3 provides helpers that convert between C++ payload structs and UTF-8 JSON
bytes for:

- `ScanRequest`
- `ScanProgress`
- `ScanCompleted`
- `SearchRequest`
- `SearchResponse`
- error response payloads

Malformed JSON syntax maps to `ProtocolError`. Valid JSON that does not satisfy
the expected schema maps to `InvalidArgument`.

M3 does not implement service routing, local IPC, Qt UI, TCP transfer, or file
transfer behavior.
