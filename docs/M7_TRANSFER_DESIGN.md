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
TransferManager
    |
    +----------------------+
    |                      |
    v                      v
TcpTransferServer   TcpTransferClient
```

Responsibilities:

-   Desktop communicates only through Local IPC.
-   LocalIpcServer handles IPC protocol.
-   TransferManager manages transfer lifecycle.
-   TcpTransferServer handles receiving files.
-   TcpTransferClient handles sending files.

------------------------------------------------------------------------

## 4. Component Responsibility

### TransferManager

TransferManager coordinates TCP transfer features.

Responsibilities:

-   Start TCP transfer service.
-   Stop TCP transfer service.
-   Configure receive directory.
-   Manage TcpTransferServer lifecycle.
-   Manage TcpTransferClient lifecycle.
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

New commands:

### EnableLanTransfer

Request:

``` json
{
    "type": "EnableLanTransfer"
}
```

Response:

``` json
{
    "success": true,
    "port": 45827
}
```

### DisableLanTransfer

Request:

``` json
{
    "type": "DisableLanTransfer"
}
```

Response:

``` json
{
    "success": true
}
```

### SetReceiveDirectory

Request:

``` json
{
    "type": "SetReceiveDirectory",
    "path": "D:/CoreDeskReceived"
}
```

Response:

``` json
{
    "success": true
}
```

### GetTransferStatus

Request:

``` json
{
    "type": "GetTransferStatus"
}
```

Response:

``` json
{
    "enabled": true,
    "activeTransfers": 0
}
```

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
TransferManager
        |
 +----------------+
 |                |
 v                v
TcpTransferServer TcpTransferClient
```

Rules:

-   Service main composition root owns TransferManager.
-   TransferManager owns TCP adapters.
-   ServiceController does not know TCP exists.
-   Desktop communicates through IPC only.

------------------------------------------------------------------------

## 7. Receive Directory Strategy

Current M6 implementation uses:

``` text
std::filesystem::temp_directory_path()
/
CoreDeskReceived
```

M7 introduces user configuration.

Future flow:

``` text
Desktop Settings
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

-   EnableLanTransfer
-   DisableLanTransfer
-   SetReceiveDirectory
-   GetTransferStatus

Step 2: Create TransferManager.

Location:

service composition layer.

Step 3: Move TcpTransferServer lifecycle control into TransferManager.

Provide:

-   start()
-   stop()
-   status()

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
 +----------------+
 |                |
 v                v
TCP Receiver   TCP Sender
```

The system will provide a complete user-controlled LAN transfer workflow
while preserving existing core / adapter architecture boundaries.
