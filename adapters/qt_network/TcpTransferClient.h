#pragma once

#include "coredesk/common/Result.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/protocol/JsonPayload.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <string>

class QTcpSocket;
class QFile;
class QThread;

namespace coredesk::qt_network {

class TcpTransferClient : public QObject {
public:
    using HandshakeCallback = std::function<void()>;
    using FileAcceptCallback = std::function<void(RequestId, const protocol::FileAcceptPayload&)>;
    using FileRejectCallback = std::function<void(RequestId, const protocol::FileRejectPayload&)>;
    using FileResultCallback = std::function<void(RequestId, const protocol::FileResultPayload&)>;
    using ErrorCallback = std::function<void(const Error&)>;

    enum class TransferState {
        Idle,
        Connecting,
        HelloSent,
        Offering,
        Sending,
        Finishing,
        Completed,
        Failed
    };

    enum class OfferState {
        None,
        Offered,
        Accepted,
        Rejected
    };

    explicit TcpTransferClient(QString node_name = QStringLiteral("CoreDesk"), QObject* parent = nullptr);
    ~TcpTransferClient() override;

    void set_handshake_callback(HandshakeCallback callback);
    void set_file_accept_callback(FileAcceptCallback callback);
    void set_file_reject_callback(FileRejectCallback callback);
    void set_file_result_callback(FileResultCallback callback);
    void set_error_callback(ErrorCallback callback);

    void connect_to_host(const QString& host, quint16 port);
    void disconnect_from_host();
    bool is_connected() const;
    bool handshake_complete() const noexcept;
    TransferState transfer_state() const noexcept;
    OfferState offer_state() const noexcept;
    RequestId send_file_offer(const protocol::FileOfferPayload& offer);
    Result<RequestId> send_file(const QString& path);

private:
    void handle_connected();
    void handle_ready_read();
    void handle_disconnected();
    void handle_error(const QString& message);
    void dispatch_frame(const protocol::Frame& frame);
    void handle_hello_ack(const protocol::Frame& frame);
    void handle_file_accept(const protocol::Frame& frame);
    void handle_file_reject(const protocol::Frame& frame);
    void handle_file_result(const protocol::Frame& frame);
    void send_hello();
    void start_hash_worker(const QString& path);
    void handle_hash_result(std::uint64_t generation, const QString& path, std::int64_t file_size, const QString& file_name, Result<std::string> sha256);
    void cleanup_hash_worker();
    std::string generate_transfer_id() const;
    void send_next_chunk();
    void send_file_finish();
    void cleanup_transfer_file();
    void fail_transfer(Error error);
    void send_frame(protocol::Frame frame);

    QString node_name_;
    std::unique_ptr<QTcpSocket> socket_;
    protocol::FrameDecoder decoder_;
    bool handshake_complete_{false};
    TransferState transfer_state_{TransferState::Idle};
    OfferState offer_state_{OfferState::None};
    RequestId next_request_id_{1};
    RequestId pending_offer_request_id_{0};
    RequestId pending_finish_request_id_{0};
    std::string pending_offer_transfer_id_;
    std::uint64_t hash_generation_{0};
    std::uint64_t send_offset_{0};
    std::uint64_t send_file_size_{0};
    std::unique_ptr<QThread> hash_thread_;
    std::unique_ptr<QFile> send_file_;
    HandshakeCallback handshake_callback_;
    FileAcceptCallback file_accept_callback_;
    FileRejectCallback file_reject_callback_;
    FileResultCallback file_result_callback_;
    ErrorCallback error_callback_;
};

} // namespace coredesk::qt_network
