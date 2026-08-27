#include "TcpTransferServer.h"

#include "coredesk/protocol/JsonPayload.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace coredesk::qt_network {
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
        output[static_cast<qsizetype>(i)] = static_cast<char>(std::to_integer<unsigned char>(bytes[i]));
    }
    return output;
}

std::string utf8_string(const QString& value)
{
    const auto bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::string unknown_transfer_id()
{
    return "00000000000000000000000000000000";
}

QString path_to_qstring(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::string to_lower_hex(const QByteArray& bytes)
{
    const auto hex = bytes.toHex();
    return std::string(hex.constData(), static_cast<std::size_t>(hex.size()));
}

} // namespace

namespace detail {

Result<void> validate_chunk_bounds(std::uint64_t expected_offset,
                                   std::uint64_t file_size,
                                   std::size_t chunk_size)
{
    if (expected_offset > file_size) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "expected_offset exceeds file_size"});
    }
    if (static_cast<std::uint64_t>(chunk_size) > file_size - expected_offset) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "chunk exceeds offered file_size"});
    }
    return Result<void>::success();
}

} // namespace detail

TcpTransferServer::TcpTransferServer(QString node_name, QObject* parent)
    : QObject(parent)
    , node_name_(std::move(node_name))
    , receive_directory_(std::filesystem::temp_directory_path())
    , server_(std::make_unique<QTcpServer>())
{
    QObject::connect(server_.get(), &QTcpServer::newConnection, this, [this]() {
        handle_new_connection();
    });
}

TcpTransferServer::~TcpTransferServer()
{
    close();
}

void TcpTransferServer::set_receive_directory(std::filesystem::path receive_directory)
{
    receive_directory_ = std::move(receive_directory);
}

Result<void> TcpTransferServer::listen(quint16 port, const QHostAddress& address)
{
    if (server_->listen(address, port)) {
        return Result<void>::success();
    }
    return Result<void>::failure({ErrorCode::ConnectionFailed, server_->errorString().toStdString()});
}

void TcpTransferServer::close()
{
    std::vector<QTcpSocket*> sockets;
    sockets.reserve(connections_.size());
    for (const auto& [socket, connection] : connections_) {
        sockets.push_back(socket);
    }
    for (auto* socket : sockets) {
        if (socket) {
            socket->disconnect(this);
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }
    connections_.clear();
    if (server_->isListening()) {
        server_->close();
    }
    cleanup_active_transfer(true);
}

bool TcpTransferServer::is_listening() const
{
    return server_->isListening();
}

quint16 TcpTransferServer::server_port() const
{
    return server_->serverPort();
}

std::uint64_t TcpTransferServer::active_transfer_count() const
{
    return active_transfer_ ? 1U : 0U;
}

void TcpTransferServer::handle_new_connection()
{
    while (server_->hasPendingConnections()) {
        auto* socket = server_->nextPendingConnection();
        auto connection = std::make_unique<Connection>();
        connection->socket = socket;
        connections_.emplace(socket, std::move(connection));

        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handle_ready_read(socket);
        });
        QObject::connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            handle_disconnected(socket);
        });
    }
}

void TcpTransferServer::handle_ready_read(QTcpSocket* socket)
{
    const auto found = connections_.find(socket);
    if (found == connections_.end()) {
        return;
    }

    const auto raw = socket->readAll();
    const auto bytes = bytes_from_qbytearray(raw);
    auto frames = found->second->decoder.push(bytes);
    if (!frames.ok()) {
        close_protocol_error(socket);
        return;
    }

    for (const auto& frame : frames.value()) {
        if (!connections_.contains(socket)) {
            return;
        }
        dispatch_frame(socket, frame);
    }
}

void TcpTransferServer::handle_disconnected(QTcpSocket* socket)
{
    remove_connection(socket);
    socket->deleteLater();
}

void TcpTransferServer::dispatch_frame(QTcpSocket* socket, const protocol::Frame& frame)
{
    if (frame.type == protocol::MessageType::Hello) {
        handle_hello(socket, frame);
        return;
    }
    if (frame.type == protocol::MessageType::FileOffer) {
        handle_file_offer(socket, frame);
        return;
    }
    if (frame.type == protocol::MessageType::FileChunk) {
        handle_file_chunk(socket, frame);
        return;
    }
    if (frame.type == protocol::MessageType::FileFinish) {
        handle_file_finish(socket, frame);
        return;
    }
    close_protocol_error(socket);
}

void TcpTransferServer::handle_hello(QTcpSocket* socket, const protocol::Frame& frame)
{
    auto hello = protocol::decode_hello_payload(frame.payload);
    if (!hello.ok()) {
        close_protocol_error(socket);
        return;
    }

    auto payload = protocol::encode_hello_ack_payload(protocol::HelloAckPayload{protocol::kFrameVersion, utf8_string(node_name_)});
    if (!payload.ok()) {
        close_protocol_error(socket);
        return;
    }
    send_frame(socket, protocol::Frame{protocol::MessageType::HelloAck, 0, frame.request_id, std::move(payload).value()});
}

void TcpTransferServer::handle_file_offer(QTcpSocket* socket, const protocol::Frame& frame)
{
    auto offer = protocol::decode_file_offer_payload(frame.payload);
    if (!offer.ok()) {
        send_file_reject(socket, frame.request_id, unknown_transfer_id(), offer.error().code, offer.error().message);
        return;
    }

    auto valid = validate_offer_target(offer.value());
    if (!valid.ok()) {
        send_file_reject(socket,
                         frame.request_id,
                         offer.value().transfer_id,
                         valid.error().code,
                         valid.error().message);
        return;
    }

    auto ready = begin_receive(socket, frame.request_id, offer.value());
    if (!ready.ok()) {
        send_file_reject(socket,
                         frame.request_id,
                         offer.value().transfer_id,
                         ready.error().code,
                         ready.error().message);
        return;
    }

    auto payload = protocol::encode_file_accept_payload(protocol::FileAcceptPayload{offer.value().transfer_id, 0});
    if (!payload.ok()) {
        close_protocol_error(socket);
        return;
    }
    send_frame(socket, protocol::Frame{protocol::MessageType::FileAccept, 0, frame.request_id, std::move(payload).value()});
}

void TcpTransferServer::handle_file_chunk(QTcpSocket* socket, const protocol::Frame& frame)
{
    auto chunk = protocol::decode_file_chunk_payload(frame.payload);
    if (!chunk.ok()) {
        fail_receive(socket, frame.request_id, chunk.error().code, chunk.error().message);
        return;
    }

    if (!active_transfer_ || active_transfer_->socket != socket || chunk.value().transfer_id != active_transfer_->transfer_id) {
        fail_receive(socket, frame.request_id, ErrorCode::InvalidArgument, "transfer_id does not match active transfer");
        return;
    }
    if (chunk.value().offset != active_transfer_->expected_offset) {
        fail_receive(socket, frame.request_id, ErrorCode::InvalidArgument, "unexpected chunk offset");
        return;
    }
    auto bounds = detail::validate_chunk_bounds(active_transfer_->expected_offset,
                                                active_transfer_->file_size,
                                                chunk.value().data.size());
    if (!bounds.ok()) {
        fail_receive(socket, frame.request_id, bounds.error().code, bounds.error().message);
        return;
    }

    QByteArray data;
    data.resize(static_cast<qsizetype>(chunk.value().data.size()));
    for (std::size_t i = 0; i < chunk.value().data.size(); ++i) {
        data[static_cast<qsizetype>(i)] = static_cast<char>(std::to_integer<unsigned char>(chunk.value().data[i]));
    }

    const auto written = active_transfer_->part_file->write(data);
    if (written != data.size()) {
        fail_receive(socket, frame.request_id, ErrorCode::IoError, "failed to write chunk");
        return;
    }
    active_transfer_->hash->addData(data);
    active_transfer_->expected_offset += static_cast<std::uint64_t>(chunk.value().data.size());
    active_transfer_->state = ReceiveState::Receiving;
}

void TcpTransferServer::handle_file_finish(QTcpSocket* socket, const protocol::Frame& frame)
{
    auto finish = protocol::decode_file_finish_payload(frame.payload);
    if (!finish.ok()) {
        fail_receive(socket, frame.request_id, finish.error().code, finish.error().message);
        return;
    }
    if (!active_transfer_ || active_transfer_->socket != socket || finish.value().transfer_id != active_transfer_->transfer_id) {
        fail_receive(socket, frame.request_id, ErrorCode::InvalidArgument, "transfer_id does not match active transfer");
        return;
    }

    active_transfer_->state = ReceiveState::Finishing;
    complete_receive(socket, frame.request_id);
}

Result<void> TcpTransferServer::validate_offer_target(const protocol::FileOfferPayload& offer) const
{
    if (active_transfer_.has_value()) {
        return Result<void>::failure({ErrorCode::Busy, "another transfer is already active"});
    }

    std::error_code ec;
    if (!std::filesystem::exists(receive_directory_, ec) || !std::filesystem::is_directory(receive_directory_, ec)) {
        return Result<void>::failure({ErrorCode::PathNotFound, "receive directory is not available"});
    }

    const auto target = receive_directory_ / std::filesystem::path(offer.file_name);
    if (std::filesystem::exists(target, ec)) {
        return Result<void>::failure({ErrorCode::TargetExists, "target file already exists"});
    }
    if (ec) {
        return Result<void>::failure({ErrorCode::IoError, ec.message()});
    }

    return Result<void>::success();
}

Result<void> TcpTransferServer::begin_receive(QTcpSocket* socket, RequestId request_id, const protocol::FileOfferPayload& offer)
{
    auto valid = validate_offer_target(offer);
    if (!valid.ok()) {
        return valid;
    }

    const auto target = receive_directory_ / std::filesystem::path(offer.file_name);
    const auto part = receive_directory_ / std::filesystem::path(offer.file_name + ".coredesk.part");
    std::error_code ec;
    std::filesystem::remove(part, ec);

    auto file = std::make_unique<QFile>(path_to_qstring(part));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<void>::failure({ErrorCode::IoError, file->errorString().toStdString()});
    }

    auto hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    active_transfer_.emplace();
    active_transfer_->state = ReceiveState::Accepted;
    active_transfer_->socket = socket;
    active_transfer_->offer_request_id = request_id;
    active_transfer_->transfer_id = offer.transfer_id;
    active_transfer_->sha256 = offer.sha256;
    active_transfer_->file_size = offer.file_size;
    active_transfer_->expected_offset = 0;
    active_transfer_->target_path = target;
    active_transfer_->part_path = part;
    active_transfer_->part_file = std::move(file);
    active_transfer_->hash = std::move(hash);
    return Result<void>::success();
}

void TcpTransferServer::complete_receive(QTcpSocket* socket, RequestId request_id)
{
    if (!active_transfer_) {
        send_file_result(socket, request_id, unknown_transfer_id(), false, ErrorCode::InvalidArgument, "no active transfer");
        return;
    }

    const auto transfer_id = active_transfer_->transfer_id;
    if (active_transfer_->expected_offset != active_transfer_->file_size) {
        fail_receive(socket, request_id, ErrorCode::InvalidArgument, "received size does not match file_size");
        return;
    }

    if (!active_transfer_->part_file->flush()) {
        fail_receive(socket, request_id, ErrorCode::IoError, active_transfer_->part_file->errorString().toStdString());
        return;
    }
    active_transfer_->part_file->close();

    const auto actual_sha256 = to_lower_hex(active_transfer_->hash->result());
    if (actual_sha256 != active_transfer_->sha256) {
        fail_receive(socket, request_id, ErrorCode::HashMismatch, "sha256 mismatch");
        return;
    }

    if (!QFile::rename(path_to_qstring(active_transfer_->part_path), path_to_qstring(active_transfer_->target_path))) {
        fail_receive(socket, request_id, ErrorCode::IoError, "failed to rename completed transfer");
        return;
    }

    send_file_result(socket, request_id, transfer_id, true, ErrorCode::Ok, {});
    cleanup_active_transfer(false);
}

void TcpTransferServer::fail_receive(QTcpSocket* socket, RequestId request_id, ErrorCode code, std::string message)
{
    const auto transfer_id = active_transfer_ ? active_transfer_->transfer_id : unknown_transfer_id();
    send_file_result(socket, request_id, transfer_id, false, code, std::move(message));
    cleanup_active_transfer(true);
}

void TcpTransferServer::cleanup_active_transfer(bool remove_part)
{
    if (!active_transfer_) {
        return;
    }
    const auto part_path = active_transfer_->part_path;
    if (active_transfer_->part_file) {
        active_transfer_->part_file->close();
    }
    active_transfer_.reset();
    if (remove_part) {
        QFile::remove(path_to_qstring(part_path));
    }
}

void TcpTransferServer::send_frame(QTcpSocket* socket, protocol::Frame frame)
{
    if (!socket || !connections_.contains(socket)) {
        return;
    }
    auto encoded = protocol::FrameEncoder::encode(frame);
    if (!encoded.ok()) {
        close_protocol_error(socket);
        return;
    }
    socket->write(qbytearray_from_bytes(encoded.value()));
}

void TcpTransferServer::send_file_reject(QTcpSocket* socket,
                                         RequestId request_id,
                                         std::string transfer_id,
                                         ErrorCode code,
                                         std::string message)
{
    auto payload = protocol::encode_file_reject_payload(protocol::FileRejectPayload{std::move(transfer_id), code, std::move(message)});
    if (!payload.ok()) {
        close_protocol_error(socket);
        return;
    }
    send_frame(socket, protocol::Frame{protocol::MessageType::FileReject, 0, request_id, std::move(payload).value()});
}

void TcpTransferServer::send_file_result(QTcpSocket* socket,
                                         RequestId request_id,
                                         std::string transfer_id,
                                         bool ok,
                                         ErrorCode code,
                                         std::string message)
{
    auto payload =
        protocol::encode_file_result_payload(protocol::FileResultPayload{std::move(transfer_id), ok, code, std::move(message)});
    if (!payload.ok()) {
        close_protocol_error(socket);
        return;
    }
    send_frame(socket, protocol::Frame{protocol::MessageType::FileResult, 0, request_id, std::move(payload).value()});
}

void TcpTransferServer::close_protocol_error(QTcpSocket* socket)
{
    remove_connection(socket);
    if (socket) {
        socket->disconnectFromHost();
    }
}

void TcpTransferServer::remove_connection(QTcpSocket* socket)
{
    if (active_transfer_ && socket == active_transfer_->socket) {
        cleanup_active_transfer(true);
    }
    connections_.erase(socket);
}

} // namespace coredesk::qt_network
