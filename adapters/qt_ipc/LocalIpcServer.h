#pragma once

#include "coredesk/common/Result.h"
#include "coredesk/common/Logger.h"
#include "coredesk/concurrency/ThreadPool.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/protocol/JsonPayload.h"
#include "coredesk/service/ServiceController.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <unordered_map>

class QLocalServer;
class QLocalSocket;

namespace coredesk::qt_ipc {

inline constexpr const char* kLocalServerName = "CoreDesk.Service.v1";

struct TransferManagementHandlers {
    using SendFileCompletion = std::function<void(Result<void>)>;
    std::function<Result<protocol::EnableLanTransferResponsePayload>()> enable;
    std::function<Result<protocol::DisableLanTransferResponsePayload>()> disable;
    std::function<Result<protocol::SetReceiveDirectoryResponsePayload>(
        const protocol::SetReceiveDirectoryRequestPayload&)>
        set_receive_directory;
    std::function<Result<protocol::GetTransferStatusResponsePayload>()> status;
    std::function<Result<void>(const protocol::SendFileRequestPayload&, SendFileCompletion)> send_file;
};

class LocalIpcServer : public QObject {
public:
    explicit LocalIpcServer(service::ServiceController& controller, QObject* parent = nullptr);
    ~LocalIpcServer() override;

    Result<void> listen(const QString& name = QString::fromLatin1(kLocalServerName));
    void close();
    bool is_listening() const;
    void set_logger(Logger* logger) noexcept;
    void set_transfer_management_handlers(TransferManagementHandlers handlers);

private:
    struct Connection {
        QLocalSocket* socket{};
        protocol::FrameDecoder decoder;
    };

    void handle_new_connection();
    void handle_ready_read(QLocalSocket* socket);
    void handle_disconnected(QLocalSocket* socket);
    void dispatch_frame(QLocalSocket* socket, const protocol::Frame& frame);
    void dispatch_search_request(QLocalSocket* socket, const protocol::Frame& frame);
    void dispatch_enable_lan_transfer_request(QLocalSocket* socket, const protocol::Frame& frame);
    void dispatch_disable_lan_transfer_request(QLocalSocket* socket, const protocol::Frame& frame);
    void dispatch_set_receive_directory_request(QLocalSocket* socket, const protocol::Frame& frame);
    void dispatch_get_transfer_status_request(QLocalSocket* socket, const protocol::Frame& frame);
    void dispatch_send_file_request(QLocalSocket* socket, const protocol::Frame& frame);
    void send_frame(QLocalSocket* socket, protocol::Frame frame);
    void send_error(QLocalSocket* socket, protocol::MessageType type, RequestId request_id, const Error& error);
    void close_protocol_error(QLocalSocket* socket, const Error& error);
    void remove_connection(QLocalSocket* socket);

    service::ServiceController& controller_;
    std::unique_ptr<QLocalServer> server_;
    std::unordered_map<QLocalSocket*, std::unique_ptr<Connection>> connections_;
    concurrency::ThreadPool search_pool_{2};
    TransferManagementHandlers transfer_handlers_;
    Logger* logger_{};
};

} // namespace coredesk::qt_ipc
