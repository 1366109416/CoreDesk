#pragma once

#include "coredesk/common/Result.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/service/ServiceController.h"

#include <QObject>
#include <QString>

#include <memory>
#include <unordered_map>

class QLocalServer;
class QLocalSocket;

namespace coredesk::qt_ipc {

inline constexpr const char* kLocalServerName = "CoreDesk.Service.v1";

class LocalIpcServer : public QObject {
public:
    explicit LocalIpcServer(service::ServiceController& controller, QObject* parent = nullptr);
    ~LocalIpcServer() override;

    Result<void> listen(const QString& name = QString::fromLatin1(kLocalServerName));
    void close();
    bool is_listening() const;

private:
    struct Connection {
        QLocalSocket* socket{};
        protocol::FrameDecoder decoder;
    };

    void handle_new_connection();
    void handle_ready_read(QLocalSocket* socket);
    void handle_disconnected(QLocalSocket* socket);
    void dispatch_frame(QLocalSocket* socket, const protocol::Frame& frame);
    void send_frame(QLocalSocket* socket, protocol::Frame frame);
    void send_error(QLocalSocket* socket, protocol::MessageType type, RequestId request_id, const Error& error);
    void close_protocol_error(QLocalSocket* socket, const Error& error);
    void remove_connection(QLocalSocket* socket);

    service::ServiceController& controller_;
    std::unique_ptr<QLocalServer> server_;
    std::unordered_map<QLocalSocket*, std::unique_ptr<Connection>> connections_;
};

} // namespace coredesk::qt_ipc
