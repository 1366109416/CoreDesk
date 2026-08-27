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
40   EnableLanTransferRequest
41   EnableLanTransferResponse
42   DisableLanTransferRequest
43   DisableLanTransferResponse
44   SetReceiveDirectoryRequest
45   SetReceiveDirectoryResponse
46   GetTransferStatusRequest
47   GetTransferStatusResponse
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

### Local IPC Transfer Management Payloads

M7 adds local IPC management messages for the service-side LAN transfer
receiver. These messages use the shared FrameProtocol and are separate from
the TCP transfer wire messages in the `100+` range.

`EnableLanTransferRequest`:

```json
{}
```

`EnableLanTransferResponse` success:

```json
{
  "success": true,
  "port": 45827
}
```

`DisableLanTransferRequest`:

```json
{}
```

`DisableLanTransferResponse` success:

```json
{
  "success": true
}
```

`SetReceiveDirectoryRequest`:

```json
{
  "path": "D:/CoreDeskReceived"
}
```

`path` must be a non-empty UTF-8 string. Filesystem policy checks are performed
by the service transfer manager.

`SetReceiveDirectoryResponse` success:

```json
{
  "success": true,
  "path": "D:/CoreDeskReceived"
}
```

`GetTransferStatusRequest`:

```json
{}
```

`GetTransferStatusResponse` success:

```json
{
  "enabled": true,
  "port": 45827,
  "receive_directory": "D:/CoreDeskReceived",
  "active_transfers": 0
}
```

`active_transfers` is the real count exposed by the current
`TcpTransferServer` single-active-receive model: `0` when no receive is active,
and `1` while one receive is active.

Failure responses reuse the existing `ErrorResponsePayload` schema and are sent
with the matching response message type, for example
`EnableLanTransferResponse` or `SetReceiveDirectoryResponse`.

M6 adds protocol-layer helpers for LAN transfer control payloads. These helpers
only validate wire/schema details; TCP sockets, filesystem writes, SHA-256
calculation, service routing, and UI behavior are implemented outside the
protocol layer.

### LAN Transfer JSON Payloads

`Hello` and `HelloAck`:

```json
{ "protocol_version": 1, "node_name": "MyPC" }
```

`protocol_version` must be `1`.

`FileOffer`:

```json
{
  "transfer_id": "32-lowercase-hex-chars",
  "file_name": "report.pdf",
  "file_size": 123456789,
  "chunk_size": 262144,
  "sha256": "64-lowercase-hex-chars"
}
```

Protocol-layer validation requires:

- `transfer_id` is exactly 32 lowercase hex characters.
- `sha256` is exactly 64 lowercase hex characters.
- `file_name` is a basename-style wire value and must not be empty or contain
  `/`, `\`, or `..`.
- `chunk_size` is nonzero and must fit within the maximum frame payload once
  the `FileChunk` binary prefix is included.

`FileAccept`:

```json
{ "transfer_id": "32-lowercase-hex-chars", "start_offset": 0 }
```

For v1.0, `start_offset` must be `0`. Nonzero resume offsets are reserved for a
future milestone.

`FileReject`:

```json
{
  "transfer_id": "32-lowercase-hex-chars",
  "code": "TARGET_EXISTS",
  "message": "target already exists"
}
```

`FileFinish`:

```json
{ "transfer_id": "32-lowercase-hex-chars" }
```

`FileResult`:

```json
{
  "transfer_id": "32-lowercase-hex-chars",
  "ok": false,
  "code": "HASH_MISMATCH",
  "message": "hash mismatch"
}
```

`code` values reuse the shared CoreDesk error-code strings used by error
response payloads.

### FileChunk Binary Payload

`FileChunk` is not JSON. Its payload is binary:

```text
32 bytes  transfer_id ASCII lowercase hex
8 bytes   offset, big-endian uint64
4 bytes   data_length, big-endian uint32
N bytes   file data
```

The full binary payload, including the 44-byte prefix, must not exceed the
global 1 MiB frame payload limit. `data_length` must exactly match the remaining
payload byte count.

M6 protocol-layer work does not implement TCP transport, file IO, SHA-256
calculation, `.part` files, service integration, or UI integration.
