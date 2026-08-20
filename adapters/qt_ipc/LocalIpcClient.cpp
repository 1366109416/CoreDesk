#include "LocalIpcClient.h"

#include <QByteArray>
#include <QLocalSocket>

#include <cstddef>
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

Result<void> LocalIpcClient::connect_to_server(const QString& name, int timeout_ms)
{
    socket_->connectToServer(name);
    if (!socket_->waitForConnected(timeout_ms)) {
        return Result<void>::failure({ErrorCode::ConnectionFailed, socket_->errorString().toStdString()});
    }
    return Result<void>::success();
}

void LocalIpcClient::disconnect_from_server()
{
    if (socket_ && socket_->state() != QLocalSocket::UnconnectedState) {
        socket_->disconnectFromServer();
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

Result<void> LocalIpcClient::send_frame(protocol::Frame frame)
{
    auto encoded = protocol::FrameEncoder::encode(frame);
    if (!encoded.ok()) {
        return Result<void>::failure(encoded.error());
    }
    if (!is_connected()) {
        return Result<void>::failure({ErrorCode::ConnectionFailed, "local IPC client is not connected"});
    }
    socket_->write(qbytearray_from_bytes(encoded.value()));
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

RequestId LocalIpcClient::next_request_id()
{
    return next_request_id_++;
}

} // namespace coredesk::qt_ipc
