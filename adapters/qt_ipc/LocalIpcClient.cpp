#include "LocalIpcClient.h"

#include <QByteArray>
#include <QLocalSocket>

#include <cstddef>
#include <string>
#include <span>
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

} // namespace

LocalIpcClient::LocalIpcClient(QObject* parent)
    : QObject(parent)
    , socket_(std::make_unique<QLocalSocket>())
{
    socket_->setParent(this);
    QObject::connect(socket_.get(), &QLocalSocket::readyRead, this, [this]() {
        handle_ready_read();
    });
    QObject::connect(socket_.get(), &QLocalSocket::connected, this, [this]() {
        handle_connected();
    });
    QObject::connect(socket_.get(), &QLocalSocket::disconnected, this, [this]() {
        handle_disconnected();
    });
    QObject::connect(socket_.get(), &QLocalSocket::errorOccurred, this, [this]() {
        handle_socket_error();
    });
}

LocalIpcClient::~LocalIpcClient()
{
    disconnect_from_server();
}

void LocalIpcClient::set_frame_callback(FrameCallback callback)
{
    frame_callback_ = std::move(callback);
}

void LocalIpcClient::set_error_callback(ErrorCallback callback)
{
    error_callback_ = std::move(callback);
}

void LocalIpcClient::set_connected_callback(ConnectionCallback callback)
{
    connected_callback_ = std::move(callback);
}

void LocalIpcClient::set_disconnected_callback(ConnectionCallback callback)
{
    disconnected_callback_ = std::move(callback);
}

Result<void> LocalIpcClient::connect_to_server(const QString& name, int timeout_ms)
{
    decoder_.reset();
    socket_->connectToServer(name);
    if (!socket_->waitForConnected(timeout_ms)) {
        return Result<void>::failure({ErrorCode::ConnectionFailed, socket_->errorString().toStdString()});
    }
    return Result<void>::success();
}

void LocalIpcClient::connect_to_server_async(const QString& name)
{
    decoder_.reset();
    if (socket_->state() != QLocalSocket::UnconnectedState) {
        socket_->abort();
    }
    socket_->connectToServer(name);
}

void LocalIpcClient::disconnect_from_server()
{
    if (socket_ && socket_->state() != QLocalSocket::UnconnectedState) {
        socket_->disconnectFromServer();
        if (socket_->state() != QLocalSocket::UnconnectedState) {
            socket_->abort();
        }
    }
    decoder_.reset();
}

bool LocalIpcClient::is_connected() const
{
    return socket_ && socket_->state() == QLocalSocket::ConnectedState;
}

RequestId LocalIpcClient::send_ping()
{
    const auto request_id = next_request_id();
    send_frame(protocol::Frame{protocol::MessageType::Ping, 0, request_id, {}});
    return request_id;
}

RequestId LocalIpcClient::send_scan_request(const protocol::ScanRequestPayload& payload)
{
    const auto request_id = next_request_id();
    auto encoded = protocol::encode_scan_request_payload(payload);
    if (!encoded.ok()) {
        if (error_callback_) {
            error_callback_(encoded.error());
        }
        return request_id;
    }
    send_frame(protocol::Frame{protocol::MessageType::ScanRequest, 0, request_id, std::move(encoded).value()});
    return request_id;
}

RequestId LocalIpcClient::send_cancel_scan()
{
    const auto request_id = next_request_id();
    send_frame(protocol::Frame{protocol::MessageType::CancelScanRequest, 0, request_id, {}});
    return request_id;
}

RequestId LocalIpcClient::send_search_request(const protocol::SearchRequestPayload& payload)
{
    const auto request_id = next_request_id();
    auto encoded = protocol::encode_search_request_payload(payload);
    if (!encoded.ok()) {
        if (error_callback_) {
            error_callback_(encoded.error());
        }
        return request_id;
    }
    send_frame(protocol::Frame{protocol::MessageType::SearchRequest, 0, request_id, std::move(encoded).value()});
    return request_id;
}

RequestId LocalIpcClient::send_enable_lan_transfer_request(const protocol::EnableLanTransferRequestPayload& payload)
{
    const auto request_id = next_request_id();
    auto encoded = protocol::encode_enable_lan_transfer_request_payload(payload);
    if (!encoded.ok()) {
        if (error_callback_) {
            error_callback_(encoded.error());
        }
        return request_id;
    }
    send_frame(protocol::Frame{protocol::MessageType::EnableLanTransferRequest, 0, request_id, std::move(encoded).value()});
    return request_id;
}

RequestId LocalIpcClient::send_disable_lan_transfer_request(const protocol::DisableLanTransferRequestPayload& payload)
{
    const auto request_id = next_request_id();
    auto encoded = protocol::encode_disable_lan_transfer_request_payload(payload);
    if (!encoded.ok()) {
        if (error_callback_) {
            error_callback_(encoded.error());
        }
        return request_id;
    }
    send_frame(protocol::Frame{protocol::MessageType::DisableLanTransferRequest, 0, request_id, std::move(encoded).value()});
    return request_id;
}

RequestId LocalIpcClient::send_set_receive_directory_request(const protocol::SetReceiveDirectoryRequestPayload& payload)
{
    const auto request_id = next_request_id();
    auto encoded = protocol::encode_set_receive_directory_request_payload(payload);
    if (!encoded.ok()) {
        if (error_callback_) {
            error_callback_(encoded.error());
        }
        return request_id;
    }
    send_frame(protocol::Frame{protocol::MessageType::SetReceiveDirectoryRequest, 0, request_id, std::move(encoded).value()});
    return request_id;
}

RequestId LocalIpcClient::send_get_transfer_status_request(const protocol::GetTransferStatusRequestPayload& payload)
{
    const auto request_id = next_request_id();
    auto encoded = protocol::encode_get_transfer_status_request_payload(payload);
    if (!encoded.ok()) {
        if (error_callback_) {
            error_callback_(encoded.error());
        }
        return request_id;
    }
    send_frame(protocol::Frame{protocol::MessageType::GetTransferStatusRequest, 0, request_id, std::move(encoded).value()});
    return request_id;
}

RequestId LocalIpcClient::send_file_request(const protocol::SendFileRequestPayload& payload)
{
    const auto request_id = next_request_id();
    auto encoded = protocol::encode_send_file_request_payload(payload);
    if (!encoded.ok()) {
        if (error_callback_) {
            error_callback_(encoded.error());
        }
        return request_id;
    }
    send_frame(protocol::Frame{protocol::MessageType::SendFileRequest, 0, request_id, std::move(encoded).value()});
    return request_id;
}

Result<void> LocalIpcClient::send_frame(protocol::Frame frame)
{
    auto encoded = protocol::FrameEncoder::encode(frame);
    if (!encoded.ok()) {
        return Result<void>::failure(encoded.error());
    }
    if (!is_connected()) {
        return Result<void>::failure({ErrorCode::ConnectionFailed, "local IPC client is not connected"});
    }
    const auto bytes = qbytearray_from_bytes(encoded.value());
    const auto written = socket_->write(bytes);
    if (written != bytes.size()) {
        return Result<void>::failure({ErrorCode::IoError, "failed to queue full local IPC frame"});
    }
    return Result<void>::success();
}

void LocalIpcClient::handle_ready_read()
{
    const auto raw = bytes_from_qbytearray(socket_->readAll());
    auto frames = decoder_.push(std::span<const std::byte>(raw.data(), raw.size()));
    if (!frames.ok()) {
        if (error_callback_) {
            error_callback_(frames.error());
        }
        disconnect_from_server();
        return;
    }

    for (const auto& frame : frames.value()) {
        if (frame_callback_) {
            frame_callback_(frame);
        }
    }
}

void LocalIpcClient::handle_connected()
{
    decoder_.reset();
    if (connected_callback_) {
        connected_callback_();
    }
}

void LocalIpcClient::handle_disconnected()
{
    decoder_.reset();
    if (disconnected_callback_) {
        disconnected_callback_();
    }
}

void LocalIpcClient::handle_socket_error()
{
    decoder_.reset();
    if (error_callback_) {
        error_callback_({ErrorCode::ConnectionFailed, socket_->errorString().toStdString()});
    }
}

RequestId LocalIpcClient::next_request_id()
{
    return next_request_id_++;
}

} // namespace coredesk::qt_ipc
