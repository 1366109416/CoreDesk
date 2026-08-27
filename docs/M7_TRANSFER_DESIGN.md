# M7 Transfer Management Design

## 1. Overview

M6 completed the implementation of TCP file transfer capability.

Current M6 components include:

-   TcpTransferServer
-   TcpTransferClient
-   FrameProtocol reuse
-   FileOffer / FileAccept / FileReject
-   FileChunk transfer
-   FileFinish / FileResult handling
-   SHA256 verification
-   Service composition integration

The purpose of M7 is to introduce a transfer management layer between
Desktop, Local IPC and TCP transport.

M7 focuses on exposing TCP transfer capability through the service
lifecycle and desktop control workflow.

------------------------------------------------------------------------

## 2. Current Limitation After M6

Although TCP transfer is available, the current system still has the
following limitations:

-   Desktop cannot enable LAN transfer.
-   Desktop cannot disable LAN transfer.
-   Desktop cannot configure receive directory.
-   Desktop cannot query transfer status.
-   Service has no unified transfer lifecycle management layer.

The current relationship:

``` text
Desktop
    |
 Local IPC
    |
 Service
    |
 TcpTransferServer
    |
 TCP Network
    |
 TcpTransferClient
```

M7 introduces TransferManager to manage these capabilities.

------------------------------------------------------------------------

## 3. Proposed Architecture

``` text
Desktop UI
    |
 Local IPC
    |
LocalIpcServer
    |
TransferManagementHandlers
    |
TransferManager
    |
TcpTransferServer
```

Responsibilities:

-   Desktop communicates only through Local IPC.
-   LocalIpcServer handles IPC protocol.
-   LocalIpcServer reaches transfer management through injected handlers.
-   TransferManager manages transfer lifecycle.
-   TcpTransferServer handles receiving files.
-   TcpTransferClient desktop send workflow is not part of M7.

------------------------------------------------------------------------

## 4. Component Responsibility

### TransferManager

TransferManager coordinates TCP transfer features.

Responsibilities:

-   Start TCP transfer service.
-   Stop TCP transfer service.
-   Configure receive directory.
-   Manage TcpTransferServer lifecycle.
-   Provide transfer status information.

TransferManager belongs to Qt service composition layer.

It should not be placed inside ServiceController or
coredesk_service_lib.

------------------------------------------------------------------------

### TcpTransferServer

Responsibilities:

-   Accept TCP connections.
-   Process FrameProtocol messages.
-   Handle Hello / HelloAck.
-   Handle FileOffer.
-   Handle FileChunk.
-   Handle FileFinish.
-   Verify SHA256.
-   Manage .part temporary files.

------------------------------------------------------------------------

### TcpTransferClient

Responsibilities:

-   Establish TCP connection.
-   Send Hello.
-   Send FileOffer.
-   Send file chunks.
-   Send FileFinish.
-   Handle FileResult.

------------------------------------------------------------------------

### ServiceController

ServiceController remains unchanged.

It continues to handle:

-   scan
-   search
-   index snapshot
-   service core logic

It should not own TCP transfer components.

------------------------------------------------------------------------

## 5. IPC Design

M7 extends Local IPC protocol.

Message type is carried by FrameProtocol. Payloads do not include an
additional `"type"` routing field.

Fixed wire values:

``` text
40 EnableLanTransferRequest
41 EnableLanTransferResponse
42 DisableLanTransferRequest
43 DisableLanTransferResponse
44 SetReceiveDirectoryRequest
45 SetReceiveDirectoryResponse
46 GetTransferStatusRequest
47 GetTransferStatusResponse
```

Failures use the matching response MessageType plus the existing
ErrorResponsePayload schema.

New requests:

### EnableLanTransfer

Request:

``` json
{}
```

Success response:

``` json
{
    "success": true,
    "port": 45827
}
```

### DisableLanTransfer

Request:

``` json
{}
```

Success response:

``` json
{
    "success": true
}
```

### SetReceiveDirectory

Request:

``` json
{
    "path": "D:/CoreDeskReceived"
}
```

Success response:

``` json
{
    "success": true,
    "path": "D:/CoreDeskReceived"
}
```

### GetTransferStatus

Request:

``` json
{}
```

Success response:

``` json
{
    "enabled": true,
    "port": 45827,
    "receive_directory": "D:/CoreDeskReceived",
    "active_transfers": 0
}
```

`active_transfers` reflects the current single-active-receive model:
`0` when no receive is active and `1` while one receive is active.

------------------------------------------------------------------------

## 6. Ownership Design

``` text
coredesk_service process

QCoreApplication
        |
ServiceController
        |
LocalIpcServer
        |
TransferManagementHandlers
        |
TransferManager
        |
TcpTransferServer
```

Rules:

-   Service main composition root owns TransferManager.
-   TransferManager owns TcpTransferServer.
-   ServiceController does not know TCP exists.
-   Desktop communicates through IPC only.
-   Desktop exit does not disable the LAN receiver.
-   Local IPC disconnect does not disable the LAN receiver.
-   When COREDESK_BUILD_NETWORK is OFF, transfer management requests return an
    unavailable/error response instead of using TCP adapters.

------------------------------------------------------------------------

## 7. Receive Directory Strategy

Current M6 implementation uses:

``` text
std::filesystem::temp_directory_path()
/
CoreDeskReceived
```

M7 introduces user configuration.

Flow:

``` text
Desktop LAN Transfer controls
        |
       IPC
        |
TransferManager
        |
TcpTransferServer
```

No hardcoded machine-specific paths should be introduced.

------------------------------------------------------------------------

## 8. Implementation Plan

Step 1: Extend IPC protocol.

Add:

-   EnableLanTransferRequest / EnableLanTransferResponse
-   DisableLanTransferRequest / DisableLanTransferResponse
-   SetReceiveDirectoryRequest / SetReceiveDirectoryResponse
-   GetTransferStatusRequest / GetTransferStatusResponse

Step 2: Create TransferManager.

Location:

service composition layer.

Step 3: Move TcpTransferServer lifecycle control into TransferManager.

Provide:

-   start()
-   stop()
-   status()
-   set_receive_directory()

Step 4: Connect LocalIpcServer with TransferManager.

Step 5: Add Desktop UI controls.

Possible features:

-   Enable LAN Transfer switch.
-   Receive directory selection.
-   Transfer status display.

------------------------------------------------------------------------

## 9. Non Goals

M7 does not include:

-   Redesign TCP protocol.
-   Replace FrameProtocol.
-   Modify ServiceController.
-   Add third-party dependency.
-   Move Qt dependency into core modules.
-   Desktop TcpTransferClient send workflow.
-   Device discovery.
-   Transfer history or persistent settings.

------------------------------------------------------------------------

## 10. Final Goal

``` text
Desktop
    |
 Local IPC
    |
 Service
    |
 TransferManager
    |
TCP Receiver
```

The system will provide a complete user-controlled LAN transfer workflow
while preserving existing core / adapter architecture boundaries.
The service starts with LAN transfer disabled; TCP listening starts only after
EnableLanTransferRequest succeeds.
