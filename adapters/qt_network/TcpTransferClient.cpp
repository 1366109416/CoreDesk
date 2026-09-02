#include "TcpTransferClient.h"

#include "coredesk/protocol/JsonPayload.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
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

std::vector<std::byte> bytes_from_qbytearray_payload(const QByteArray& bytes)
{
    std::vector<std::byte> output;
    output.reserve(static_cast<std::size_t>(bytes.size()));
    for (const auto ch : bytes) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return output;
}

std::string to_lower_hex(const QByteArray& bytes)
{
    const auto hex = bytes.toHex();
    return std::string(hex.constData(), static_cast<std::size_t>(hex.size()));
}

Result<std::string> compute_sha256_hex_for_path(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<std::string>::failure({ErrorCode::IoError, file.errorString().toStdString()});
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const auto chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            return Result<std::string>::failure({ErrorCode::IoError, file.errorString().toStdString()});
        }
        hash.addData(chunk);
    }
    return Result<std::string>::success(to_lower_hex(hash.result()));
}

} // namespace

TcpTransferClient::TcpTransferClient(QString node_name, QObject* parent)
    : QObject(parent)
    , node_name_(std::move(node_name))
    , socket_(std::make_unique<QTcpSocket>())
{
    QObject::connect(socket_.get(), &QTcpSocket::connected, this, [this]() {
        handle_connected();
    });
    QObject::connect(socket_.get(), &QTcpSocket::readyRead, this, [this]() {
        handle_ready_read();
    });
    QObject::connect(socket_.get(), &QTcpSocket::disconnected, this, [this]() {
        handle_disconnected();
    });
    QObject::connect(socket_.get(), &QTcpSocket::bytesWritten, this, [this](qint64) {
        handle_bytes_written();
    });
    QObject::connect(socket_.get(), &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        handle_error(socket_->errorString());
    });
}

TcpTransferClient::~TcpTransferClient()
{
    disconnect_from_host();
    cleanup_hash_worker();
}

void TcpTransferClient::set_handshake_callback(HandshakeCallback callback)
{
    handshake_callback_ = std::move(callback);
}

void TcpTransferClient::set_file_accept_callback(FileAcceptCallback callback)
{
    file_accept_callback_ = std::move(callback);
}

void TcpTransferClient::set_file_reject_callback(FileRejectCallback callback)
{
    file_reject_callback_ = std::move(callback);
}

void TcpTransferClient::set_file_result_callback(FileResultCallback callback)
{
    file_result_callback_ = std::move(callback);
}

void TcpTransferClient::set_error_callback(ErrorCallback callback)
{
    error_callback_ = std::move(callback);
}

void TcpTransferClient::set_logger(Logger* logger) noexcept
{
    logger_ = logger;
}

void TcpTransferClient::connect_to_host(const QString& host, quint16 port)
{
    handshake_complete_ = false;
    transfer_state_ = TransferState::Connecting;
    pending_hello_request_id_ = 0;
    write_remainder_.clear();
    remainder_flush_scheduled_ = false;
    decoder_.reset();
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->abort();
    }
    socket_->connectToHost(host, port);
}

void TcpTransferClient::disconnect_from_host()
{
    handshake_complete_ = false;
    transfer_state_ = TransferState::Idle;
    offer_state_ = OfferState::None;
    pending_offer_request_id_ = 0;
    pending_finish_request_id_ = 0;
    pending_offer_transfer_id_.clear();
    send_pump_scheduled_ = false;
    write_remainder_.clear();
    remainder_flush_scheduled_ = false;
    cleanup_transfer_file();
    decoder_.reset();
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
    }
}

bool TcpTransferClient::is_connected() const
{
    return socket_->state() == QAbstractSocket::ConnectedState;
}

bool TcpTransferClient::handshake_complete() const noexcept
{
    return handshake_complete_;
}

TcpTransferClient::TransferState TcpTransferClient::transfer_state() const noexcept
{
    return transfer_state_;
}

TcpTransferClient::OfferState TcpTransferClient::offer_state() const noexcept
{
    return offer_state_;
}

Result<RequestId> TcpTransferClient::send_file(const QString& path)
{
    if (hash_thread_ || transfer_state_ == TransferState::Offering || transfer_state_ == TransferState::Sending ||
        transfer_state_ == TransferState::Finishing) {
        return Result<RequestId>::failure({ErrorCode::Busy, "file transfer already active"});
    }

    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return Result<RequestId>::failure({ErrorCode::PathNotFound, "file does not exist"});
    }

    cleanup_transfer_file();
    send_offset_ = 0;
    send_file_size_ = static_cast<std::uint64_t>(info.size());
    transfer_state_ = TransferState::Offering;
    offer_state_ = OfferState::None;
    start_hash_worker(path);
    return Result<RequestId>::success(0);
}

RequestId TcpTransferClient::send_file_offer(const protocol::FileOfferPayload& offer)
{
    if (!handshake_complete_ || (transfer_state_ != TransferState::Idle && transfer_state_ != TransferState::Offering)) {
        return 0;
    }
    auto payload = protocol::encode_file_offer_payload(offer);
    if (!payload.ok()) {
        handle_error(QString::fromStdString(payload.error().message));
        return 0;
    }

    const auto request_id = next_request_id_++;
    pending_offer_request_id_ = request_id;
    pending_offer_transfer_id_ = offer.transfer_id;
    transfer_state_ = TransferState::Offering;
    offer_state_ = OfferState::Offered;
    auto written = send_frame(protocol::Frame{protocol::MessageType::FileOffer, 0, request_id, std::move(payload).value()});
    if (!written.ok()) {
        fail_transfer(written.error());
        return 0;
    }
    return request_id;
}

void TcpTransferClient::handle_connected()
{
    if (logger_) {
        logger_->log(LogLevel::Info, "network", "connection established");
    }
    send_hello();
}

void TcpTransferClient::handle_ready_read()
{
    const auto raw = socket_->readAll();
    const auto bytes = bytes_from_qbytearray(raw);
    auto frames = decoder_.push(bytes);
    if (!frames.ok()) {
        handle_error(frames.error().message.empty() ? QStringLiteral("protocol error") : QString::fromStdString(frames.error().message));
        socket_->disconnectFromHost();
        return;
    }

    for (const auto& frame : frames.value()) {
        dispatch_frame(frame);
    }
}

void TcpTransferClient::handle_disconnected()
{
    if (logger_) {
        logger_->log(LogLevel::Info, "network", "connection disconnected");
    }
    handshake_complete_ = false;
    ++hash_generation_;
    if (transfer_state_ == TransferState::Offering || transfer_state_ == TransferState::Sending ||
        transfer_state_ == TransferState::Finishing) {
        transfer_state_ = TransferState::Failed;
    }
    offer_state_ = OfferState::None;
    pending_offer_request_id_ = 0;
    pending_finish_request_id_ = 0;
    pending_offer_transfer_id_.clear();
    send_pump_scheduled_ = false;
    write_remainder_.clear();
    remainder_flush_scheduled_ = false;
    cleanup_transfer_file();
    decoder_.reset();
}

void TcpTransferClient::handle_error(const QString& message)
{
    if (transfer_state_ == TransferState::Completed || transfer_state_ == TransferState::Failed) {
        return;
    }
    if (transfer_state_ == TransferState::Idle) {
        if (error_callback_) {
            error_callback_({ErrorCode::ConnectionFailed, message.toStdString()});
        }
        return;
    }
    fail_transfer({ErrorCode::ConnectionFailed, message.toStdString()});
}

void TcpTransferClient::dispatch_frame(const protocol::Frame& frame)
{
    switch (frame.type) {
    case protocol::MessageType::HelloAck:
        handle_hello_ack(frame);
        break;
    case protocol::MessageType::FileAccept:
        handle_file_accept(frame);
        break;
    case protocol::MessageType::FileReject:
        handle_file_reject(frame);
        break;
    case protocol::MessageType::FileResult:
        handle_file_result(frame);
        break;
    default:
        handle_error(QStringLiteral("unexpected TCP transfer frame"));
        socket_->disconnectFromHost();
        break;
    }
}

void TcpTransferClient::handle_hello_ack(const protocol::Frame& frame)
{
    if (transfer_state_ != TransferState::HelloSent || frame.request_id != pending_hello_request_id_) {
        fail_transfer({ErrorCode::ProtocolError, "unexpected HelloAck"});
        socket_->abort();
        return;
    }
    auto ack = protocol::decode_hello_ack_payload(frame.payload);
    if (!ack.ok()) {
        handle_error(QString::fromStdString(ack.error().message));
        socket_->disconnectFromHost();
        return;
    }

    handshake_complete_ = true;
    pending_hello_request_id_ = 0;
    transfer_state_ = TransferState::Idle;
    if (handshake_callback_) {
        handshake_callback_();
    }
}

void TcpTransferClient::handle_file_accept(const protocol::Frame& frame)
{
    if (transfer_state_ != TransferState::Offering) {
        fail_transfer({ErrorCode::ProtocolError, "unexpected FileAccept state"});
        socket_->abort();
        return;
    }
    auto accept = protocol::decode_file_accept_payload(frame.payload);
    if (!accept.ok()) {
        handle_error(QString::fromStdString(accept.error().message));
        socket_->disconnectFromHost();
        return;
    }
    if (frame.request_id != pending_offer_request_id_ || accept.value().transfer_id != pending_offer_transfer_id_) {
        handle_error(QStringLiteral("unexpected FileAccept"));
        socket_->disconnectFromHost();
        return;
    }

    offer_state_ = OfferState::Accepted;
    if (logger_) {
        logger_->log(LogLevel::Info,
                     "network",
                     "accept request_id=" + std::to_string(frame.request_id) + " transfer_id=" +
                         accept.value().transfer_id);
    }
    if (file_accept_callback_) {
        file_accept_callback_(frame.request_id, accept.value());
    }
    if (transfer_state_ == TransferState::Offering && send_file_) {
        transfer_state_ = TransferState::Sending;
        schedule_send_pump();
    } else {
        transfer_state_ = TransferState::Idle;
    }
}

void TcpTransferClient::handle_file_reject(const protocol::Frame& frame)
{
    if (transfer_state_ != TransferState::Offering) {
        fail_transfer({ErrorCode::ProtocolError, "unexpected FileReject state"});
        socket_->abort();
        return;
    }
    auto reject = protocol::decode_file_reject_payload(frame.payload);
    if (!reject.ok()) {
        handle_error(QString::fromStdString(reject.error().message));
        socket_->disconnectFromHost();
        return;
    }
    if (frame.request_id != pending_offer_request_id_ || reject.value().transfer_id != pending_offer_transfer_id_) {
        handle_error(QStringLiteral("unexpected FileReject"));
        socket_->disconnectFromHost();
        return;
    }

    offer_state_ = OfferState::Rejected;
    if (logger_) {
        logger_->log(LogLevel::Warning,
                     "network",
                     "reject request_id=" + std::to_string(frame.request_id) + " transfer_id=" +
                         reject.value().transfer_id + " error_code=" + std::string(to_string(reject.value().code)));
    }
    const auto automatic_transfer = static_cast<bool>(send_file_);
    cleanup_transfer_file();
    send_pump_scheduled_ = false;
    write_remainder_.clear();
    remainder_flush_scheduled_ = false;
    transfer_state_ = automatic_transfer ? TransferState::Failed : TransferState::Idle;
    if (file_reject_callback_) {
        file_reject_callback_(frame.request_id, reject.value());
    }
}

void TcpTransferClient::handle_file_result(const protocol::Frame& frame)
{
    if (transfer_state_ == TransferState::Completed || transfer_state_ == TransferState::Failed) {
        return;
    }
    if (transfer_state_ != TransferState::Finishing) {
        fail_transfer({ErrorCode::ProtocolError, "unexpected FileResult state"});
        socket_->abort();
        return;
    }
    auto result = protocol::decode_file_result_payload(frame.payload);
    if (!result.ok()) {
        fail_transfer(result.error());
        socket_->disconnectFromHost();
        return;
    }
    if (frame.request_id != pending_finish_request_id_ || result.value().transfer_id != pending_offer_transfer_id_) {
        fail_transfer({ErrorCode::InvalidArgument, "unexpected FileResult"});
        socket_->disconnectFromHost();
        return;
    }

    cleanup_transfer_file();
    transfer_state_ = result.value().ok ? TransferState::Completed : TransferState::Failed;
    if (logger_) {
        logger_->log(result.value().ok ? LogLevel::Info : LogLevel::Error,
                     "network",
                     std::string(result.value().ok ? "transfer success" : "transfer fail") +
                         " request_id=" + std::to_string(frame.request_id) + " transfer_id=" +
                         result.value().transfer_id + " error_code=" + std::string(to_string(result.value().code)));
    }
    if (file_result_callback_) {
        file_result_callback_(frame.request_id, result.value());
    }
}

void TcpTransferClient::send_hello()
{
    auto payload = protocol::encode_hello_payload(protocol::HelloPayload{protocol::kFrameVersion, utf8_string(node_name_)});
    if (!payload.ok()) {
        handle_error(QString::fromStdString(payload.error().message));
        return;
    }
    transfer_state_ = TransferState::HelloSent;
    pending_hello_request_id_ = next_request_id_++;
    auto written = send_frame(
        protocol::Frame{protocol::MessageType::Hello, 0, pending_hello_request_id_, std::move(payload).value()});
    if (!written.ok()) {
        fail_transfer(written.error());
        socket_->abort();
    }
}

void TcpTransferClient::start_hash_worker(const QString& path)
{
    cleanup_hash_worker();

    const auto generation = ++hash_generation_;
    const QFileInfo info(path);
    const auto file_size = info.size();
    const auto file_name = info.fileName();
    const QPointer<TcpTransferClient> guard(this);
    hash_thread_ = std::unique_ptr<QThread>(QThread::create([guard, generation, path, file_size, file_name]() {
        auto sha256 = compute_sha256_hex_for_path(path);
        if (!guard) {
            return;
        }

        QMetaObject::invokeMethod(
            guard,
            [guard, generation, path, file_size, file_name, sha256 = std::move(sha256)]() mutable {
                if (!guard) {
                    return;
                }
                guard->handle_hash_result(generation, path, file_size, file_name, std::move(sha256));
            },
            Qt::QueuedConnection);
    }));
    hash_thread_->start();
}

void TcpTransferClient::handle_hash_result(std::uint64_t generation,
                                           const QString& path,
                                           std::int64_t file_size,
                                           const QString& file_name,
                                           Result<std::string> sha256)
{
    cleanup_hash_worker();
    if (generation != hash_generation_ || transfer_state_ != TransferState::Offering) {
        return;
    }
    if (!sha256.ok()) {
        fail_transfer(sha256.error());
        return;
    }

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        fail_transfer({ErrorCode::IoError, file->errorString().toStdString()});
        return;
    }

    send_file_ = std::move(file);
    send_offset_ = 0;
    send_file_size_ = static_cast<std::uint64_t>(file_size);
    const auto offer =
        protocol::FileOfferPayload{generate_transfer_id(), utf8_string(file_name), send_file_size_, 256U * 1024U, sha256.value()};

    const auto request_id = send_file_offer(offer);
    if (request_id == 0) {
        cleanup_transfer_file();
        transfer_state_ = TransferState::Failed;
    }
}

void TcpTransferClient::cleanup_hash_worker()
{
    if (!hash_thread_) {
        return;
    }
    hash_thread_->wait();
    hash_thread_.reset();
}

std::string TcpTransferClient::generate_transfer_id() const
{
    QByteArray bytes;
    bytes.resize(16);
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return to_lower_hex(bytes);
}

void TcpTransferClient::send_next_chunk()
{
    send_pump_scheduled_ = false;
    if (transfer_state_ != TransferState::Sending || !send_file_) {
        return;
    }
    if (!is_connected()) {
        fail_transfer({ErrorCode::ConnectionFailed, "socket disconnected while sending"});
        return;
    }
    if (total_pending_write_bytes() >= kPendingWriteHighWaterMark) {
        return;
    }
    if (send_offset_ >= send_file_size_ || send_file_->atEnd()) {
        send_file_finish();
        return;
    }

    const auto chunk = send_file_->read(kFileChunkSize);
    if (chunk.isEmpty() && send_file_->error() != QFile::NoError) {
        fail_transfer({ErrorCode::IoError, send_file_->errorString().toStdString()});
        return;
    }

    auto payload = protocol::encode_file_chunk_payload(
        protocol::FileChunkPayload{pending_offer_transfer_id_, send_offset_, bytes_from_qbytearray_payload(chunk)});
    if (!payload.ok()) {
        fail_transfer(payload.error());
        return;
    }

    auto written = send_frame(
        protocol::Frame{protocol::MessageType::FileChunk, 0, next_request_id_++, std::move(payload).value()});
    if (!written.ok()) {
        fail_transfer(written.error());
        socket_->abort();
        return;
    }
    send_offset_ += static_cast<std::uint64_t>(chunk.size());
    schedule_send_pump();
}

void TcpTransferClient::schedule_send_pump()
{
    if (send_pump_scheduled_ || transfer_state_ != TransferState::Sending || !is_connected() ||
        !write_remainder_.isEmpty() || total_pending_write_bytes() >= kPendingWriteHighWaterMark) {
        return;
    }
    send_pump_scheduled_ = true;
    QTimer::singleShot(0, this, [this]() {
        send_next_chunk();
    });
}

void TcpTransferClient::handle_bytes_written()
{
    if (!write_remainder_.isEmpty()) {
        schedule_remainder_flush();
    } else {
        schedule_send_pump();
    }
}

void TcpTransferClient::schedule_remainder_flush()
{
    if (remainder_flush_scheduled_ || write_remainder_.isEmpty() || !is_connected() ||
        transfer_state_ == TransferState::Completed || transfer_state_ == TransferState::Failed ||
        transfer_state_ == TransferState::Idle) {
        return;
    }
    remainder_flush_scheduled_ = true;
    QTimer::singleShot(1, this, [this]() {
        flush_write_remainder();
    });
}

void TcpTransferClient::flush_write_remainder()
{
    remainder_flush_scheduled_ = false;
    if (write_remainder_.isEmpty()) {
        schedule_send_pump();
        return;
    }
    if (!is_connected() || transfer_state_ == TransferState::Completed || transfer_state_ == TransferState::Failed ||
        transfer_state_ == TransferState::Idle) {
        write_remainder_.clear();
        return;
    }

    const auto accepted = write_to_socket(write_remainder_);
    if (accepted < 0) {
        fail_transfer({ErrorCode::ConnectionFailed, socket_->errorString().toStdString()});
        socket_->abort();
        return;
    }
    if (accepted > 0) {
        write_remainder_.remove(0, accepted);
    }
    if (!write_remainder_.isEmpty()) {
        schedule_remainder_flush();
        return;
    }
    schedule_send_pump();
}

qint64 TcpTransferClient::write_to_socket(const QByteArray& bytes)
{
    const auto write_size = test_write_acceptance_limit_ >= 0
        ? std::min(test_write_acceptance_limit_, static_cast<qint64>(bytes.size()))
        : static_cast<qint64>(bytes.size());
    return socket_->write(bytes.constData(), write_size);
}

qint64 TcpTransferClient::total_pending_write_bytes() const
{
    return socket_->bytesToWrite() + write_remainder_.size();
}

void TcpTransferClient::send_file_finish()
{
    if (transfer_state_ != TransferState::Sending) {
        return;
    }

    auto payload = protocol::encode_file_finish_payload(protocol::FileFinishPayload{pending_offer_transfer_id_});
    if (!payload.ok()) {
        fail_transfer(payload.error());
        return;
    }
    cleanup_transfer_file();
    transfer_state_ = TransferState::Finishing;
    pending_finish_request_id_ = next_request_id_++;
    if (logger_) {
        logger_->log(LogLevel::Info,
                     "network",
                     "finish request_id=" + std::to_string(pending_finish_request_id_) + " transfer_id=" +
                         pending_offer_transfer_id_);
    }
    auto written = send_frame(
        protocol::Frame{protocol::MessageType::FileFinish, 0, pending_finish_request_id_, std::move(payload).value()});
    if (!written.ok()) {
        fail_transfer(written.error());
        socket_->abort();
    }
}

void TcpTransferClient::cleanup_transfer_file()
{
    if (send_file_) {
        send_file_->close();
        send_file_.reset();
    }
}

void TcpTransferClient::fail_transfer(Error error)
{
    if (transfer_state_ == TransferState::Completed || transfer_state_ == TransferState::Failed) {
        return;
    }
    cleanup_transfer_file();
    send_pump_scheduled_ = false;
    write_remainder_.clear();
    remainder_flush_scheduled_ = false;
    transfer_state_ = TransferState::Failed;
    if (logger_) {
        logger_->log(LogLevel::Error,
                     "network",
                     "transfer fail error_code=" + std::string(to_string(error.code)) + " message=" + error.message);
    }
    if (error_callback_) {
        error_callback_(std::move(error));
    }
}

Result<void> TcpTransferClient::send_frame(protocol::Frame frame)
{
    if (!is_connected()) {
        return Result<void>::failure({ErrorCode::ConnectionFailed, "socket is not connected"});
    }
    if (!write_remainder_.isEmpty()) {
        return Result<void>::failure({ErrorCode::Busy, "previous frame is not fully accepted"});
    }
    auto encoded = protocol::FrameEncoder::encode(frame);
    if (!encoded.ok()) {
        return Result<void>::failure(encoded.error());
    }
    const auto bytes = qbytearray_from_bytes(encoded.value());
    const auto accepted = write_to_socket(bytes);
    if (accepted < 0) {
        return Result<void>::failure({ErrorCode::ConnectionFailed, socket_->errorString().toStdString()});
    }
    if (accepted < bytes.size()) {
        write_remainder_ = bytes.mid(accepted);
        schedule_remainder_flush();
    }
    return Result<void>::success();
}

} // namespace coredesk::qt_network
