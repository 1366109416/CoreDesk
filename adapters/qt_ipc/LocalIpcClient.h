#pragma once

#include "coredesk/common/Result.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/protocol/JsonPayload.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QLocalSocket;

namespace coredesk::qt_ipc {

class LocalIpcClient : public QObject {
public:
    using FrameCallback = std::function<void(const protocol::Frame&)>;
    using ErrorCallback = std::function<void(const Error&)>;
    using ConnectionCallback = std::function<void()>;

    explicit LocalIpcClient(QObject* parent = nullptr);
    ~LocalIpcClient() override;

    void set_frame_callback(FrameCallback callback);
    void set_error_callback(ErrorCallback callback);
    void set_connected_callback(ConnectionCallback callback);
    void set_disconnected_callback(ConnectionCallback callback);

    Result<void> connect_to_server(const QString& name = QString::fromLatin1("CoreDesk.Service.v1"),
                                   int timeout_ms = 3000);
    void connect_to_server_async(const QString& name = QString::fromLatin1("CoreDesk.Service.v1"));
    void disconnect_from_server();
    bool is_connected() const;

    RequestId send_ping();
    RequestId send_scan_request(const protocol::ScanRequestPayload& payload);
    RequestId send_cancel_scan();
    RequestId send_search_request(const protocol::SearchRequestPayload& payload);
    Result<void> send_frame(protocol::Frame frame);

private:
    void handle_ready_read();
    void handle_connected();
    void handle_disconnected();
    void handle_socket_error();
    RequestId next_request_id();

    std::unique_ptr<QLocalSocket> socket_;
    protocol::FrameDecoder decoder_;
    RequestId next_request_id_{1};
    FrameCallback frame_callback_;
    ErrorCallback error_callback_;
    ConnectionCallback connected_callback_;
    ConnectionCallback disconnected_callback_;
};

} // namespace coredesk::qt_ipc
