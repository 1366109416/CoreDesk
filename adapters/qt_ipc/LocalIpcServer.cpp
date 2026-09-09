#include "LocalIpcServer.h"

#include "coredesk/protocol/JsonPayload.h"

#include <QByteArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>
#include <QPointer>

#include <cstddef>
#include <exception>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace coredesk::qt_ipc {
namespace {

std::vector<std::byte> bytes_from_qbytearray(const QByteArray& bytes)
{
    std::vector<std::byte> output;
    output.reserve(static_cast<std::size_t>(bytes.size()));
    for (const auto ch : bytes) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return output;
}

QByteArray qbytearray_from_bytes(const std::vector<std::byte>& bytes)
{
    QByteArray output;
    output.resize(static_cast<qsizetype>(bytes.size()));
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        output[static_cast<qsizetype>(i)] = static_cast<char>(bytes[i]);
    }
    return output;
}

Error invalid_request(std::string message)
{
    return {ErrorCode::InvalidArgument, std::move(message)};
}

Error transfer_unavailable()
{
    return {ErrorCode::ConnectionFailed, "LAN transfer feature is unavailable in this build"};
}

} // namespace

LocalIpcServer::LocalIpcServer(service::ServiceController& controller, QObject* parent)
    : QObject(parent)
    , controller_(controller)
    , server_(std::make_unique<QLocalServer>())
{
    server_->setParent(this);
    QObject::connect(server_.get(), &QLocalServer::newConnection, this, [this]() {
        handle_new_connection();
    });
}

LocalIpcServer::~LocalIpcServer()
{
    search_pool_.shutdown();
    close();
    controller_.shutdown();
}

Result<void> LocalIpcServer::listen(const QString& name)
{
    if (server_->listen(name)) {
        return Result<void>::success();
    }

    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(100)) {
        probe.disconnectFromServer();
        return Result<void>::failure({ErrorCode::TargetExists, "CoreDesk service is already running"});
    }

    QLocalServer::removeServer(name);
    if (server_->listen(name)) {
        return Result<void>::success();
    }

    return Result<void>::failure({ErrorCode::ConnectionFailed, server_->errorString().toStdString()});
}

void LocalIpcServer::close()
{
    std::vector<QLocalSocket*> sockets;
    sockets.reserve(connections_.size());
    for (const auto& [socket, connection] : connections_) {
        sockets.push_back(socket);
    }
    for (auto* socket : sockets) {
        if (socket) {
            socket->disconnect(this);
            socket->disconnectFromServer();
            socket->deleteLater();
        }
    }
    connections_.clear();
    if (server_->isListening()) {
        server_->close();
    }
}

bool LocalIpcServer::is_listening() const
{
    return server_->isListening();
}

void LocalIpcServer::set_logger(Logger* logger) noexcept
{
    logger_ = logger;
}

void LocalIpcServer::set_transfer_management_handlers(TransferManagementHandlers handlers)
{
    transfer_handlers_ = std::move(handlers);
}

void LocalIpcServer::handle_new_connection()
{
    while (server_->hasPendingConnections()) {
        auto* socket = server_->nextPendingConnection();
        auto connection = std::make_unique<Connection>();
        connection->socket = socket;
        connections_.emplace(socket, std::move(connection));

        if (logger_) {
            logger_->log(LogLevel::Info, "ipc", "client connected");
        }

        QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            handle_ready_read(socket);
        });
        QObject::connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            handle_disconnected(socket);
        });
    }
}

void LocalIpcServer::handle_ready_read(QLocalSocket* socket)
{
    auto it = connections_.find(socket);
    if (it == connections_.end()) {
        return;
    }

    const auto raw = bytes_from_qbytearray(socket->readAll());
    auto frames = it->second->decoder.push(std::span<const std::byte>(raw.data(), raw.size()));
    if (!frames.ok()) {
        close_protocol_error(socket, frames.error());
        return;
    }

    for (const auto& frame : frames.value()) {
        if (!connections_.contains(socket)) {
            return;
        }
        dispatch_frame(socket, frame);
    }
}

void LocalIpcServer::handle_disconnected(QLocalSocket* socket)
{
    if (logger_) {
        logger_->log(LogLevel::Info, "ipc", "client disconnected");
    }
    remove_connection(socket);
    socket->deleteLater();
}

void LocalIpcServer::dispatch_frame(QLocalSocket* socket, const protocol::Frame& frame)
{
    switch (frame.type) {
    case protocol::MessageType::Ping:
        send_frame(socket, protocol::Frame{protocol::MessageType::Pong, 0, frame.request_id, {}});
        return;

    case protocol::MessageType::ScanRequest: {
        auto payload = protocol::decode_scan_request_payload(frame.payload);
        if (!payload.ok()) {
            send_error(socket, protocol::MessageType::ScanFailed, frame.request_id, payload.error());
            return;
        }
        QPointer<QLocalSocket> requester(socket);
        auto started = controller_.start_scan(
            frame.request_id,
            payload.value(),
            [this, requester](RequestId request_id, const protocol::ScanProgressPayload& progress) {
                QMetaObject::invokeMethod(this, [this, requester, request_id, progress]() {
                    if (!requester || !connections_.contains(requester)) {
                        return;
                    }
                    auto payload_bytes = protocol::encode_scan_progress_payload(progress);
                    if (payload_bytes.ok()) {
                        send_frame(requester,
                                   protocol::Frame{protocol::MessageType::ScanProgress,
                                                   0,
                                                   request_id,
                                                   std::move(payload_bytes).value()});
                    } else {
                        send_error(requester, protocol::MessageType::ScanFailed, request_id, payload_bytes.error());
                    }
                });
            },
            [this, requester](RequestId request_id, Result<protocol::ScanCompletedPayload> completed) {
                QMetaObject::invokeMethod(this, [this, requester, request_id, completed = std::move(completed)]() mutable {
                    if (!requester || !connections_.contains(requester)) {
                        return;
                    }
                    if (completed.ok()) {
                        auto payload_bytes = protocol::encode_scan_completed_payload(completed.value());
                        if (payload_bytes.ok()) {
                            send_frame(requester,
                                       protocol::Frame{protocol::MessageType::ScanCompleted,
                                                       0,
                                                       request_id,
                                                       std::move(payload_bytes).value()});
                        } else {
                            send_error(requester, protocol::MessageType::ScanFailed, request_id, payload_bytes.error());
                        }
                    } else {
                        send_error(requester, protocol::MessageType::ScanFailed, request_id, completed.error());
                    }
                });
            });
        if (!started.ok()) {
            send_error(socket, protocol::MessageType::ScanFailed, frame.request_id, started.error());
            return;
        }
        send_frame(socket, protocol::Frame{protocol::MessageType::ScanAccepted, 0, frame.request_id, {}});
        return;
    }

    case protocol::MessageType::CancelScanRequest: {
        auto cancelled = controller_.cancel_scan();
        if (!cancelled.ok()) {
            send_error(socket, protocol::MessageType::ScanFailed, frame.request_id, cancelled.error());
        }
        return;
    }

    case protocol::MessageType::SearchRequest: {
        dispatch_search_request(socket, frame);
        return;
    }

    case protocol::MessageType::EnableLanTransferRequest:
        dispatch_enable_lan_transfer_request(socket, frame);
        return;

    case protocol::MessageType::DisableLanTransferRequest:
        dispatch_disable_lan_transfer_request(socket, frame);
        return;

    case protocol::MessageType::SetReceiveDirectoryRequest:
        dispatch_set_receive_directory_request(socket, frame);
        return;

    case protocol::MessageType::GetTransferStatusRequest:
        dispatch_get_transfer_status_request(socket, frame);
        return;

    case protocol::MessageType::SendFileRequest:
        dispatch_send_file_request(socket, frame);
        return;

    default:
        send_error(socket, frame.type, frame.request_id, invalid_request("message type is not supported"));
        return;
    }
}

void LocalIpcServer::dispatch_search_request(QLocalSocket* socket, const protocol::Frame& frame)
{
    auto payload = protocol::decode_search_request_payload(frame.payload);
    if (!payload.ok()) {
        send_error(socket, protocol::MessageType::SearchResponse, frame.request_id, payload.error());
        return;
    }

    QPointer<QLocalSocket> requester(socket);
    const auto request_id = frame.request_id;
    const auto submitted = search_pool_.submit([this, requester, request_id, payload = std::move(payload).value()]() mutable {
        Result<protocol::SearchResponsePayload> response =
            Result<protocol::SearchResponsePayload>::failure({ErrorCode::InternalError, "search failed"});
        try {
            response = controller_.search(payload);
        } catch (const std::exception& ex) {
            response = Result<protocol::SearchResponsePayload>::failure({ErrorCode::InternalError, ex.what()});
        } catch (...) {
            response = Result<protocol::SearchResponsePayload>::failure(
                {ErrorCode::InternalError, "search failed with an unknown exception"});
        }

        QMetaObject::invokeMethod(this, [this, requester, request_id, response = std::move(response)]() mutable {
            if (!requester || !connections_.contains(requester)) {
                return;
            }
            if (!response.ok()) {
                send_error(requester, protocol::MessageType::SearchResponse, request_id, response.error());
                return;
            }
            auto encoded = protocol::encode_search_response_payload(response.value());
            if (!encoded.ok()) {
                send_error(requester, protocol::MessageType::SearchResponse, request_id, encoded.error());
                return;
            }
            send_frame(requester,
                       protocol::Frame{protocol::MessageType::SearchResponse, 0, request_id, std::move(encoded).value()});
        }, Qt::QueuedConnection);
    });
    if (!submitted) {
        send_error(socket,
                   protocol::MessageType::SearchResponse,
                   request_id,
                   {ErrorCode::Cancelled, "search executor is shutting down"});
    }
}

void LocalIpcServer::dispatch_enable_lan_transfer_request(QLocalSocket* socket, const protocol::Frame& frame)
{
    auto payload = protocol::decode_enable_lan_transfer_request_payload(frame.payload);
    if (!payload.ok()) {
        send_error(socket, protocol::MessageType::EnableLanTransferResponse, frame.request_id, payload.error());
        return;
    }
    if (!transfer_handlers_.enable) {
        send_error(socket, protocol::MessageType::EnableLanTransferResponse, frame.request_id, transfer_unavailable());
        return;
    }

    auto response = transfer_handlers_.enable();
    if (!response.ok()) {
        send_error(socket, protocol::MessageType::EnableLanTransferResponse, frame.request_id, response.error());
        return;
    }

    auto encoded = protocol::encode_enable_lan_transfer_response_payload(response.value());
    if (!encoded.ok()) {
        send_error(socket, protocol::MessageType::EnableLanTransferResponse, frame.request_id, encoded.error());
        return;
    }
    send_frame(socket,
               protocol::Frame{protocol::MessageType::EnableLanTransferResponse,
                               0,
                               frame.request_id,
                               std::move(encoded).value()});
}

void LocalIpcServer::dispatch_disable_lan_transfer_request(QLocalSocket* socket, const protocol::Frame& frame)
{
    auto payload = protocol::decode_disable_lan_transfer_request_payload(frame.payload);
    if (!payload.ok()) {
        send_error(socket, protocol::MessageType::DisableLanTransferResponse, frame.request_id, payload.error());
        return;
    }
    if (!transfer_handlers_.disable) {
        send_error(socket, protocol::MessageType::DisableLanTransferResponse, frame.request_id, transfer_unavailable());
        return;
    }

    auto response = transfer_handlers_.disable();
    if (!response.ok()) {
        send_error(socket, protocol::MessageType::DisableLanTransferResponse, frame.request_id, response.error());
        return;
    }

    auto encoded = protocol::encode_disable_lan_transfer_response_payload(response.value());
    if (!encoded.ok()) {
        send_error(socket, protocol::MessageType::DisableLanTransferResponse, frame.request_id, encoded.error());
        return;
    }
    send_frame(socket,
               protocol::Frame{protocol::MessageType::DisableLanTransferResponse,
                               0,
                               frame.request_id,
                               std::move(encoded).value()});
}

void LocalIpcServer::dispatch_set_receive_directory_request(QLocalSocket* socket, const protocol::Frame& frame)
{
    auto payload = protocol::decode_set_receive_directory_request_payload(frame.payload);
    if (!payload.ok()) {
        send_error(socket, protocol::MessageType::SetReceiveDirectoryResponse, frame.request_id, payload.error());
        return;
    }
    if (!transfer_handlers_.set_receive_directory) {
        send_error(socket, protocol::MessageType::SetReceiveDirectoryResponse, frame.request_id, transfer_unavailable());
        return;
    }

    auto response = transfer_handlers_.set_receive_directory(payload.value());
    if (!response.ok()) {
        send_error(socket, protocol::MessageType::SetReceiveDirectoryResponse, frame.request_id, response.error());
        return;
    }

    auto encoded = protocol::encode_set_receive_directory_response_payload(response.value());
    if (!encoded.ok()) {
        send_error(socket, protocol::MessageType::SetReceiveDirectoryResponse, frame.request_id, encoded.error());
        return;
    }
    send_frame(socket,
               protocol::Frame{protocol::MessageType::SetReceiveDirectoryResponse,
                               0,
                               frame.request_id,
                               std::move(encoded).value()});
}

void LocalIpcServer::dispatch_get_transfer_status_request(QLocalSocket* socket, const protocol::Frame& frame)
{
    auto payload = protocol::decode_get_transfer_status_request_payload(frame.payload);
    if (!payload.ok()) {
        send_error(socket, protocol::MessageType::GetTransferStatusResponse, frame.request_id, payload.error());
        return;
    }
    if (!transfer_handlers_.status) {
        send_error(socket, protocol::MessageType::GetTransferStatusResponse, frame.request_id, transfer_unavailable());
        return;
    }

    auto response = transfer_handlers_.status();
    if (!response.ok()) {
        send_error(socket, protocol::MessageType::GetTransferStatusResponse, frame.request_id, response.error());
        return;
    }

    auto encoded = protocol::encode_get_transfer_status_response_payload(response.value());
    if (!encoded.ok()) {
        send_error(socket, protocol::MessageType::GetTransferStatusResponse, frame.request_id, encoded.error());
        return;
    }
    send_frame(socket,
               protocol::Frame{protocol::MessageType::GetTransferStatusResponse,
                               0,
                               frame.request_id,
                               std::move(encoded).value()});
}

void LocalIpcServer::dispatch_send_file_request(QLocalSocket* socket, const protocol::Frame& frame)
{
    auto payload = protocol::decode_send_file_request_payload(frame.payload);
    if (!payload.ok()) {
        send_error(socket, protocol::MessageType::SendFileAccepted, frame.request_id, payload.error());
        return;
    }
    if (!transfer_handlers_.send_file) {
        send_error(socket, protocol::MessageType::SendFileAccepted, frame.request_id, transfer_unavailable());
        return;
    }

    QPointer<QLocalSocket> requester(socket);
    QPointer<LocalIpcServer> server_guard(this);
    const auto request_id = frame.request_id;
    auto started = transfer_handlers_.send_file(
        payload.value(),
        [server_guard, requester, request_id](Result<void> result) mutable {
            if (!server_guard) {
                return;
            }
            QMetaObject::invokeMethod(server_guard, [server_guard, requester, request_id, result = std::move(result)]() mutable {
                if (!server_guard || !requester || !server_guard->connections_.contains(requester)) {
                    return;
                }
                protocol::SendFileResultPayload response;
                response.success = result.ok();
                response.code = result.ok() ? ErrorCode::Ok : result.error().code;
                response.message = result.ok() ? std::string{} : result.error().message;
                auto encoded = protocol::encode_send_file_result_payload(response);
                if (!encoded.ok()) {
                    server_guard->send_error(requester, protocol::MessageType::SendFileResult, request_id, encoded.error());
                    return;
                }
                server_guard->send_frame(requester,
                                         protocol::Frame{protocol::MessageType::SendFileResult,
                                                         0,
                                                         request_id,
                                                         std::move(encoded).value()});
            }, Qt::QueuedConnection);
        });
    if (!started.ok()) {
        if (logger_) {
            logger_->log(LogLevel::Warning,
                         "network",
                         "outgoing request rejected request_id=" + std::to_string(request_id) +
                             " error_code=" + std::string(to_string(started.error().code)));
        }
        send_error(socket, protocol::MessageType::SendFileAccepted, request_id, started.error());
        return;
    }

    auto accepted = protocol::encode_send_file_accepted_payload({true});
    if (!accepted.ok()) {
        send_error(socket, protocol::MessageType::SendFileAccepted, request_id, accepted.error());
        return;
    }
    if (logger_) {
        logger_->log(LogLevel::Info,
                     "network",
                     "outgoing request accepted request_id=" + std::to_string(request_id));
    }
    send_frame(socket,
               protocol::Frame{protocol::MessageType::SendFileAccepted,
                               0,
                               request_id,
                               std::move(accepted).value()});
}

void LocalIpcServer::send_frame(QLocalSocket* socket, protocol::Frame frame)
{
    if (!socket || !connections_.contains(socket)) {
        return;
    }
    auto encoded = protocol::FrameEncoder::encode(frame);
    if (!encoded.ok()) {
        close_protocol_error(socket, encoded.error());
        return;
    }
    socket->write(qbytearray_from_bytes(encoded.value()));
}

void LocalIpcServer::send_error(QLocalSocket* socket,
                                protocol::MessageType type,
                                RequestId request_id,
                                const Error& error)
{
    if (logger_) {
        std::ostringstream message;
        message << "response error request_id=" << request_id << " error_code=" << to_string(error.code)
                << " message=" << error.message;
        logger_->log(LogLevel::Error, "ipc", message.str());
    }
    auto payload = protocol::encode_error_response_payload(protocol::ErrorResponsePayload{false, error.code, error.message});
    if (!payload.ok()) {
        close_protocol_error(socket, payload.error());
        return;
    }
    send_frame(socket, protocol::Frame{type, 0, request_id, std::move(payload).value()});
}

void LocalIpcServer::close_protocol_error(QLocalSocket* socket, const Error& error)
{
    if (logger_) {
        logger_->log(LogLevel::Error,
                     "ipc",
                     "protocol parse error error_code=" + std::string(to_string(error.code)) +
                         " message=" + error.message);
    }
    send_error(socket, protocol::MessageType::Pong, 0, error);
    remove_connection(socket);
    if (socket) {
        socket->disconnectFromServer();
    }
}

void LocalIpcServer::remove_connection(QLocalSocket* socket)
{
    connections_.erase(socket);
}

} // namespace coredesk::qt_ipc
