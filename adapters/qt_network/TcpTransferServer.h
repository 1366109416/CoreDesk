#pragma once

#include "coredesk/common/Logger.h"
#include "coredesk/common/Result.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/protocol/JsonPayload.h"

#include <QHostAddress>
#include <QObject>
#include <QByteArray>
#include <QString>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class QTcpServer;
class QTcpSocket;
class QFile;
class QCryptographicHash;

namespace coredesk::qt_network {

inline constexpr quint16 kDefaultTcpTransferPort = 45827;

namespace detail {
Result<void> validate_chunk_bounds(std::uint64_t expected_offset,
                                   std::uint64_t file_size,
                                   std::size_t chunk_size);
} // namespace detail

class TcpTransferServer : public QObject {
public:
    explicit TcpTransferServer(QString node_name = QStringLiteral("CoreDesk"), QObject* parent = nullptr);
    ~TcpTransferServer() override;

    void set_receive_directory(std::filesystem::path receive_directory);
    Result<void> listen(quint16 port = kDefaultTcpTransferPort,
                        const QHostAddress& address = QHostAddress::Any);
    void close();
    bool is_listening() const;
    quint16 server_port() const;
    std::uint64_t active_transfer_count() const;
    void set_logger(Logger* logger) noexcept;

private:
    static constexpr qint64 kPendingControlWriteLimit = 256 * 1024;

    enum class ReceiveState {
        Idle,
        Accepted,
        Receiving,
        Finishing
    };

    struct ActiveTransfer {
        ReceiveState state{ReceiveState::Idle};
        QTcpSocket* socket{};
        RequestId offer_request_id{};
        std::string transfer_id;
        std::string sha256;
        std::uint64_t file_size{};
        std::uint64_t expected_offset{};
        std::filesystem::path target_path;
        std::filesystem::path part_path;
        std::unique_ptr<QFile> part_file;
        std::unique_ptr<QCryptographicHash> hash;
    };

    struct Connection {
        QTcpSocket* socket{};
        protocol::FrameDecoder decoder;
        bool hello_complete{false};
        QByteArray write_remainder;
        bool remainder_flush_scheduled{false};
        bool disconnect_after_write{false};
    };

    void handle_new_connection();
    void handle_ready_read(QTcpSocket* socket);
    void handle_disconnected(QTcpSocket* socket);
    void dispatch_frame(QTcpSocket* socket, const protocol::Frame& frame);
    void handle_hello(QTcpSocket* socket, const protocol::Frame& frame);
    void handle_file_offer(QTcpSocket* socket, const protocol::Frame& frame);
    void handle_file_chunk(QTcpSocket* socket, const protocol::Frame& frame);
    void handle_file_finish(QTcpSocket* socket, const protocol::Frame& frame);
    Result<void> validate_offer_target(const protocol::FileOfferPayload& offer) const;
    Result<void> begin_receive(QTcpSocket* socket, RequestId request_id, const protocol::FileOfferPayload& offer);
    void complete_receive(QTcpSocket* socket, RequestId request_id);
    void fail_receive(QTcpSocket* socket, RequestId request_id, ErrorCode code, std::string message);
    void reject_connection_receive(QTcpSocket* socket,
                                   RequestId request_id,
                                   std::string transfer_id,
                                   ErrorCode code,
                                   std::string message);
    void cleanup_active_transfer(bool remove_part);
    bool send_frame(QTcpSocket* socket, protocol::Frame frame);
    void handle_bytes_written(QTcpSocket* socket);
    void schedule_remainder_flush(QTcpSocket* socket);
    void flush_write_remainder(QTcpSocket* socket);
    void handle_send_failure(QTcpSocket* socket);
    void disconnect_after_pending_write(QTcpSocket* socket);
    void send_file_reject(QTcpSocket* socket,
                          RequestId request_id,
                          std::string transfer_id,
                          ErrorCode code,
                          std::string message);
    void send_file_result(QTcpSocket* socket,
                          RequestId request_id,
                          std::string transfer_id,
                          bool ok,
                          ErrorCode code,
                          std::string message);
    void close_protocol_error(QTcpSocket* socket);
    void remove_connection(QTcpSocket* socket);

    QString node_name_;
    std::filesystem::path receive_directory_;
    std::optional<ActiveTransfer> active_transfer_;
    std::unique_ptr<QTcpServer> server_;
    std::unordered_map<QTcpSocket*, std::unique_ptr<Connection>> connections_;
    Logger* logger_{};
};

} // namespace coredesk::qt_network
