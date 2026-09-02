#include "TcpTransferClient.h"
#include "TcpTransferServer.h"
#include "TransferManager.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/protocol/JsonPayload.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coredesk::qt_network {

class TcpTransferClientTestPeer {
public:
    static void set_write_acceptance_limit(TcpTransferClient& client, qint64 limit)
    {
        client.test_write_acceptance_limit_ = limit;
    }

    static qint64 pending_write_bytes(const TcpTransferClient& client)
    {
        return client.total_pending_write_bytes();
    }

    static qint64 remainder_bytes(const TcpTransferClient& client)
    {
        return client.write_remainder_.size();
    }

    static std::uint64_t send_offset(const TcpTransferClient& client)
    {
        return client.send_offset_;
    }

    static constexpr qint64 high_water_mark()
    {
        return TcpTransferClient::kPendingWriteHighWaterMark;
    }

    static constexpr qint64 chunk_size()
    {
        return TcpTransferClient::kFileChunkSize;
    }
};

} // namespace coredesk::qt_network

namespace {

using coredesk::Error;
using coredesk::ErrorCode;
using coredesk::protocol::FileAcceptPayload;
using coredesk::protocol::FileOfferPayload;
using coredesk::protocol::FileRejectPayload;
using coredesk::protocol::FileResultPayload;
using coredesk::protocol::Frame;
using coredesk::protocol::FrameDecoder;
using coredesk::protocol::FrameEncoder;
using coredesk::protocol::MessageType;
using coredesk::qt_network::TcpTransferClient;
using coredesk::qt_network::TcpTransferClientTestPeer;
using coredesk::qt_network::TcpTransferServer;
using coredesk::service::TransferManager;

QCoreApplication& app()
{
    if (auto* existing = QCoreApplication::instance()) {
        return *existing;
    }

    static int argc = 1;
    static char app_name[] = "coredesk_tcp_transfer_tests";
    static char* argv[] = {app_name, nullptr};
    static QCoreApplication application(argc, argv);
    return application;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 5000)
{
    if (predicate()) {
        return true;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    QTimer poller;
    QObject::connect(&poller, &QTimer::timeout, &loop, [&]() {
        if (predicate()) {
            loop.quit();
        }
    });
    poller.start(5);
    timer.start(timeout_ms);
    loop.exec();
    return predicate();
}

class TempDirectory {
public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("coredesk_m6_tcp_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

    void write_file(std::string_view name, std::string_view contents) const
    {
        std::ofstream file(path_ / std::filesystem::path(name));
        file << contents;
    }

    void write_binary_file(std::string_view name, std::span<const std::byte> contents) const
    {
        std::ofstream file(path_ / std::filesystem::path(name), std::ios::binary);
        for (const auto byte : contents) {
            file.put(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
    }

private:
    std::filesystem::path path_;
};

class BuildTestDirectory {
public:
    BuildTestDirectory()
    {
#ifdef _WIN32
        const auto root = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString());
#else
        const auto root = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
#endif
        path_ = root / "test-data" /
            ("corrective-step-a-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }

    ~BuildTestDirectory()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_pattern_file(const std::filesystem::path& path, std::uint64_t size)
{
    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    std::array<char, 64 * 1024> block{};
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<char>((i * 31U + 17U) & 0xffU);
    }
    std::uint64_t written = 0;
    while (written < size) {
        const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(block.size(), size - written));
        file.write(block.data(), count);
        ASSERT_TRUE(file.good());
        written += static_cast<std::uint64_t>(count);
    }
}

std::vector<std::byte> bytes_from_text(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
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

std::string valid_transfer_id(char suffix)
{
    std::string id(32, 'a');
    id.back() = suffix;
    return id;
}

std::string valid_sha256()
{
    return std::string(64, 'b');
}

std::vector<std::byte> bytes_from_string(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
}

std::vector<std::byte> patterned_bytes(std::size_t size)
{
    std::vector<std::byte> bytes;
    bytes.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>((i * 31U + 17U) & 0xffU)));
    }
    return bytes;
}

std::string sha256_hex(std::span<const std::byte> bytes)
{
    QByteArray data;
    data.resize(static_cast<qsizetype>(bytes.size()));
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        data[static_cast<qsizetype>(i)] = static_cast<char>(std::to_integer<unsigned char>(bytes[i]));
    }
    const auto digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return std::string(digest.constData(), static_cast<std::size_t>(digest.size()));
}

FileOfferPayload make_offer(std::string transfer_id, std::string file_name)
{
    return FileOfferPayload{std::move(transfer_id), std::move(file_name), 128, 65536, valid_sha256()};
}

FileOfferPayload make_offer_for_data(std::string transfer_id, std::string file_name, const std::vector<std::byte>& data)
{
    return FileOfferPayload{std::move(transfer_id),
                            std::move(file_name),
                            static_cast<std::uint64_t>(data.size()),
                            65536,
                            sha256_hex(data)};
}

std::vector<std::byte> read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> it(file), end; it != end; ++it) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(*it)));
    }
    return bytes;
}

void connect_client_and_wait(TcpTransferClient& client, const TcpTransferServer& server)
{
    client.connect_to_host(QStringLiteral("127.0.0.1"), server.server_port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));
}

void write_frame(QTcpSocket& socket, Frame frame)
{
    auto encoded = FrameEncoder::encode(frame);
    ASSERT_TRUE(encoded.ok());
    socket.write(qbytearray_from_bytes(encoded.value()));
    ASSERT_TRUE(socket.flush());
}

void write_payload_frame(QTcpSocket& socket, MessageType type, coredesk::RequestId request_id, std::vector<std::byte> payload)
{
    write_frame(socket, Frame{type, 0, request_id, std::move(payload)});
}

bool wait_for_frame(QTcpSocket& socket,
                    FrameDecoder& decoder,
                    std::vector<Frame>& frames,
                    const std::function<bool(const Frame&)>& predicate)
{
    return wait_until([&]() {
        if (socket.bytesAvailable() > 0) {
            const auto raw = socket.readAll();
            std::vector<std::byte> bytes;
            bytes.reserve(static_cast<std::size_t>(raw.size()));
            for (const unsigned char ch : std::string_view(raw.constData(), static_cast<std::size_t>(raw.size()))) {
                bytes.push_back(static_cast<std::byte>(ch));
            }
            auto decoded = decoder.push(bytes);
            EXPECT_TRUE(decoded.ok());
            if (!decoded.ok()) {
                return true;
            }
            for (auto& frame : decoded.value()) {
                frames.push_back(std::move(frame));
            }
        }

        return std::any_of(frames.begin(), frames.end(), predicate);
    });
}

void raw_handshake(QTcpSocket& socket, FrameDecoder& decoder, std::vector<Frame>& frames, const TcpTransferServer& server)
{
    socket.connectToHost(QStringLiteral("127.0.0.1"), server.server_port());
    ASSERT_TRUE(socket.waitForConnected(3000));

    auto hello = coredesk::protocol::encode_hello_payload(coredesk::protocol::HelloPayload{1, "RawClient"});
    ASSERT_TRUE(hello.ok());
    write_payload_frame(socket, MessageType::Hello, 100, std::move(hello).value());
    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::HelloAck;
    }));
}

void raw_offer(QTcpSocket& socket,
               FrameDecoder& decoder,
               std::vector<Frame>& frames,
               const FileOfferPayload& offer,
               coredesk::RequestId request_id)
{
    auto payload = coredesk::protocol::encode_file_offer_payload(offer);
    ASSERT_TRUE(payload.ok());
    write_payload_frame(socket, MessageType::FileOffer, request_id, std::move(payload).value());
    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [&](const Frame& frame) {
        return frame.type == MessageType::FileAccept && frame.request_id == request_id;
    }));
}

FileResultPayload decode_result_from_frames(const std::vector<Frame>& frames, coredesk::RequestId request_id)
{
    const auto result_frame = std::find_if(frames.begin(), frames.end(), [&](const Frame& frame) {
        return frame.type == MessageType::FileResult && frame.request_id == request_id;
    });
    if (result_frame == frames.end()) {
        return FileResultPayload{};
    }
    auto result = coredesk::protocol::decode_file_result_payload(result_frame->payload);
    EXPECT_TRUE(result.ok());
    if (!result.ok()) {
        return FileResultPayload{};
    }
    return result.value();
}

void complete_raw_transfer(QTcpSocket& socket,
                           FrameDecoder& decoder,
                           std::vector<Frame>& frames,
                           const std::string& transfer_id,
                           const std::vector<std::byte>& data,
                           coredesk::RequestId chunk_request_id,
                           coredesk::RequestId finish_request_id)
{
    auto chunk = coredesk::protocol::encode_file_chunk_payload({transfer_id, 0, data});
    ASSERT_TRUE(chunk.ok());
    write_payload_frame(socket, MessageType::FileChunk, chunk_request_id, std::move(chunk).value());
    auto finish = coredesk::protocol::encode_file_finish_payload({transfer_id});
    ASSERT_TRUE(finish.ok());
    write_payload_frame(socket, MessageType::FileFinish, finish_request_id, std::move(finish).value());
    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [&](const Frame& frame) {
        return frame.type == MessageType::FileResult && frame.request_id == finish_request_id;
    }));
    EXPECT_TRUE(decode_result_from_frames(frames, finish_request_id).ok);
}

class PausingReceiver {
public:
    PausingReceiver()
    {
        QObject::connect(&server_, &QTcpServer::newConnection, [&]() {
            socket_ = server_.nextPendingConnection();
            socket_->setReadBufferSize(64 * 1024);
            QObject::connect(socket_, &QTcpSocket::readyRead, [&]() {
                handle_ready_read();
            });
        });
    }

    bool listen()
    {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const
    {
        return server_.serverPort();
    }

    bool offer_accepted() const noexcept
    {
        return offer_accepted_;
    }

    bool offer_received() const noexcept
    {
        return offer_received_;
    }

    void set_auto_accept_offers(bool enabled)
    {
        auto_accept_offers_ = enabled;
    }

    std::uint64_t received_bytes() const noexcept
    {
        return received_bytes_;
    }

    void resume()
    {
        paused_ = false;
        handle_ready_read();
    }

    void send_unexpected_hello_ack(coredesk::RequestId request_id)
    {
        auto payload = coredesk::protocol::encode_hello_ack_payload({1, "UnexpectedAck"});
        ASSERT_TRUE(payload.ok());
        send(MessageType::HelloAck, request_id, std::move(payload).value());
    }

    void send_unexpected_file_accept(coredesk::RequestId request_id, const std::string& transfer_id)
    {
        auto payload = coredesk::protocol::encode_file_accept_payload({transfer_id, 0});
        ASSERT_TRUE(payload.ok());
        send(MessageType::FileAccept, request_id, std::move(payload).value());
    }

    void send_unexpected_file_result(coredesk::RequestId request_id, const std::string& transfer_id)
    {
        auto payload = coredesk::protocol::encode_file_result_payload({transfer_id, true, ErrorCode::Ok, {}});
        ASSERT_TRUE(payload.ok());
        send(MessageType::FileResult, request_id, std::move(payload).value());
    }

    void send_file_reject(coredesk::RequestId request_id, const std::string& transfer_id)
    {
        auto payload = coredesk::protocol::encode_file_reject_payload(
            {transfer_id, ErrorCode::Busy, "test rejection"});
        ASSERT_TRUE(payload.ok());
        send(MessageType::FileReject, request_id, std::move(payload).value());
    }

    void repeat_last_file_result()
    {
        ASSERT_NE(last_finish_request_id_, 0U);
        send_unexpected_file_result(last_finish_request_id_, offer_.transfer_id);
    }

private:
    void send(MessageType type, coredesk::RequestId request_id, std::vector<std::byte> payload)
    {
        auto encoded = FrameEncoder::encode(Frame{type, 0, request_id, std::move(payload)});
        ASSERT_TRUE(encoded.ok());
        const auto bytes = qbytearray_from_bytes(encoded.value());
        ASSERT_EQ(socket_->write(bytes), bytes.size());
    }

    void handle_ready_read()
    {
        if (!socket_ || (paused_ && offer_accepted_)) {
            return;
        }
        const auto raw = socket_->readAll();
        std::vector<std::byte> bytes;
        bytes.reserve(static_cast<std::size_t>(raw.size()));
        for (const auto ch : raw) {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        auto decoded = decoder_.push(bytes);
        ASSERT_TRUE(decoded.ok());
        for (const auto& frame : decoded.value()) {
            handle_frame(frame);
        }
    }

    void handle_frame(const Frame& frame)
    {
        if (frame.type == MessageType::Hello) {
            auto payload = coredesk::protocol::encode_hello_ack_payload({1, "PausingReceiver"});
            ASSERT_TRUE(payload.ok());
            send(MessageType::HelloAck, frame.request_id, std::move(payload).value());
            return;
        }
        if (frame.type == MessageType::FileOffer) {
            auto offer = coredesk::protocol::decode_file_offer_payload(frame.payload);
            ASSERT_TRUE(offer.ok());
            offer_ = offer.value();
            offer_received_ = true;
            hash_.reset();
            received_bytes_ = 0;
            if (!auto_accept_offers_) {
                return;
            }
            auto payload = coredesk::protocol::encode_file_accept_payload({offer_.transfer_id, 0});
            ASSERT_TRUE(payload.ok());
            offer_accepted_ = true;
            send(MessageType::FileAccept, frame.request_id, std::move(payload).value());
            return;
        }
        if (frame.type == MessageType::FileChunk) {
            auto chunk = coredesk::protocol::decode_file_chunk_payload(frame.payload);
            ASSERT_TRUE(chunk.ok());
            ASSERT_EQ(chunk.value().offset, received_bytes_);
            QByteArray data;
            data.resize(static_cast<qsizetype>(chunk.value().data.size()));
            for (std::size_t i = 0; i < chunk.value().data.size(); ++i) {
                data[static_cast<qsizetype>(i)] =
                    static_cast<char>(std::to_integer<unsigned char>(chunk.value().data[i]));
            }
            hash_.addData(data);
            received_bytes_ += static_cast<std::uint64_t>(data.size());
            return;
        }
        if (frame.type == MessageType::FileFinish) {
            auto finish = coredesk::protocol::decode_file_finish_payload(frame.payload);
            ASSERT_TRUE(finish.ok());
            const auto hash = hash_.result().toHex();
            const std::string actual(hash.constData(), static_cast<std::size_t>(hash.size()));
            const auto ok = received_bytes_ == offer_.file_size && actual == offer_.sha256;
            last_finish_request_id_ = frame.request_id;
            auto payload = coredesk::protocol::encode_file_result_payload(
                {offer_.transfer_id, ok, ok ? ErrorCode::Ok : ErrorCode::HashMismatch, ok ? "" : "mismatch"});
            ASSERT_TRUE(payload.ok());
            send(MessageType::FileResult, frame.request_id, std::move(payload).value());
        }
    }

    QTcpServer server_;
    QTcpSocket* socket_{};
    FrameDecoder decoder_;
    FileOfferPayload offer_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    std::uint64_t received_bytes_{};
    coredesk::RequestId last_finish_request_id_{};
    bool offer_accepted_{false};
    bool offer_received_{false};
    bool auto_accept_offers_{true};
    bool paused_{true};
};

} // namespace

TEST(TcpTransferIntegrationTest, HelloHandshakeUsesFrameProtocolOverTcpLoopback)
{
    app();

    TcpTransferServer server(QStringLiteral("ServerNode"));
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());
    ASSERT_TRUE(server.is_listening());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    bool handshake_complete = false;
    std::vector<Error> errors;
    client.set_handshake_callback([&]() {
        handshake_complete = true;
    });
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });

    client.connect_to_host(QStringLiteral("127.0.0.1"), server.server_port());

    ASSERT_TRUE(wait_until([&]() {
        return handshake_complete;
    }));
    EXPECT_TRUE(client.is_connected());
    EXPECT_TRUE(client.handshake_complete());
    EXPECT_TRUE(errors.empty());

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, ReceiverActiveTransferCountReflectsRealState)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_EQ(server.active_transfer_count(), 0U);
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    FrameDecoder decoder;
    std::vector<Frame> frames;
    raw_handshake(socket, decoder, frames, server);

    const auto data = bytes_from_string("active transfer");
    const auto transfer_id = valid_transfer_id('c');
    raw_offer(socket, decoder, frames, make_offer_for_data(transfer_id, "active-count.bin", data), 150);
    EXPECT_EQ(server.active_transfer_count(), 1U);

    socket.abort();
    ASSERT_TRUE(wait_until([&]() {
        return server.active_transfer_count() == 0U;
    }));
    server.close();
}

TEST(TcpTransferIntegrationTest, FileOfferBeforeHelloIsRejected)
{
    app();
    TempDirectory receive;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), server.server_port());
    ASSERT_TRUE(socket.waitForConnected(3000));
    auto payload = coredesk::protocol::encode_file_offer_payload(make_offer(valid_transfer_id('1'), "before-hello.bin"));
    ASSERT_TRUE(payload.ok());
    write_payload_frame(socket, MessageType::FileOffer, 151, std::move(payload).value());
    ASSERT_TRUE(wait_until([&]() {
        return socket.state() == QAbstractSocket::UnconnectedState;
    }));
    EXPECT_EQ(server.active_transfer_count(), 0U);
}

TEST(TcpTransferIntegrationTest, SocketAActiveSocketBInvalidChunkDoesNotAbortA)
{
    app();
    TempDirectory receive;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket_a;
    FrameDecoder decoder_a;
    std::vector<Frame> frames_a;
    raw_handshake(socket_a, decoder_a, frames_a, server);
    const auto data = bytes_from_string("socket A remains active");
    const auto transfer_id = valid_transfer_id('2');
    raw_offer(socket_a, decoder_a, frames_a, make_offer_for_data(transfer_id, "socket-a.bin", data), 152);
    ASSERT_TRUE(std::filesystem::exists(receive.path() / "socket-a.bin.coredesk.part"));

    QTcpSocket socket_b;
    FrameDecoder decoder_b;
    std::vector<Frame> frames_b;
    raw_handshake(socket_b, decoder_b, frames_b, server);
    auto invalid = coredesk::protocol::encode_file_chunk_payload(
        {valid_transfer_id('3'), 0, bytes_from_string("foreign")});
    ASSERT_TRUE(invalid.ok());
    write_payload_frame(socket_b, MessageType::FileChunk, 153, std::move(invalid).value());
    ASSERT_TRUE(wait_until([&]() {
        return socket_b.state() == QAbstractSocket::UnconnectedState;
    }));

    EXPECT_EQ(server.active_transfer_count(), 1U);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "socket-a.bin.coredesk.part"));

    complete_raw_transfer(socket_a, decoder_a, frames_a, transfer_id, data, 154, 155);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "socket-a.bin"));
    EXPECT_EQ(read_file_bytes(receive.path() / "socket-a.bin"), data);
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "socket-a.bin.coredesk.part"));
}

TEST(TcpTransferIntegrationTest, SocketBDisconnectDoesNotAbortSocketA)
{
    app();
    TempDirectory receive;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket_a;
    FrameDecoder decoder_a;
    std::vector<Frame> frames_a;
    raw_handshake(socket_a, decoder_a, frames_a, server);
    const auto data = bytes_from_string("active after foreign disconnect");
    const auto transfer_id = valid_transfer_id('4');
    raw_offer(socket_a, decoder_a, frames_a, make_offer_for_data(transfer_id, "disconnect-b.bin", data), 156);

    QTcpSocket socket_b;
    FrameDecoder decoder_b;
    std::vector<Frame> frames_b;
    raw_handshake(socket_b, decoder_b, frames_b, server);
    socket_b.abort();
    ASSERT_TRUE(wait_until([&]() {
        return socket_b.state() == QAbstractSocket::UnconnectedState;
    }));
    EXPECT_EQ(server.active_transfer_count(), 1U);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "disconnect-b.bin.coredesk.part"));
    complete_raw_transfer(socket_a, decoder_a, frames_a, transfer_id, data, 161, 162);
    EXPECT_EQ(read_file_bytes(receive.path() / "disconnect-b.bin"), data);
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "disconnect-b.bin.coredesk.part"));
}

TEST(TcpTransferIntegrationTest, FileChunkBeforeAcceptedOfferDoesNotAffectOtherTransfer)
{
    app();
    TempDirectory receive;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket owner;
    FrameDecoder owner_decoder;
    std::vector<Frame> owner_frames;
    raw_handshake(owner, owner_decoder, owner_frames, server);
    const auto owner_id = valid_transfer_id('5');
    const auto owner_data = bytes_from_string("owner data");
    raw_offer(owner, owner_decoder, owner_frames, make_offer_for_data(owner_id, "owner.bin", owner_data), 157);

    QTcpSocket offender;
    FrameDecoder offender_decoder;
    std::vector<Frame> offender_frames;
    raw_handshake(offender, offender_decoder, offender_frames, server);
    auto chunk = coredesk::protocol::encode_file_chunk_payload(
        {valid_transfer_id('6'), 0, bytes_from_string("no offer")});
    ASSERT_TRUE(chunk.ok());
    write_payload_frame(offender, MessageType::FileChunk, 158, std::move(chunk).value());
    ASSERT_TRUE(wait_until([&]() {
        return offender.state() == QAbstractSocket::UnconnectedState;
    }));
    EXPECT_EQ(server.active_transfer_count(), 1U);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "owner.bin.coredesk.part"));
    complete_raw_transfer(owner, owner_decoder, owner_frames, owner_id, owner_data, 163, 164);
    EXPECT_EQ(read_file_bytes(receive.path() / "owner.bin"), owner_data);
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "owner.bin.coredesk.part"));
}

TEST(TcpTransferIntegrationTest, FileFinishWrongTransferIdDoesNotAbortOtherValidTransfer)
{
    app();
    TempDirectory receive;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket owner;
    FrameDecoder owner_decoder;
    std::vector<Frame> owner_frames;
    raw_handshake(owner, owner_decoder, owner_frames, server);
    const auto owner_id = valid_transfer_id('7');
    const auto owner_data = bytes_from_string("owner");
    raw_offer(owner,
              owner_decoder,
              owner_frames,
              make_offer_for_data(owner_id, "finish-owner.bin", owner_data),
              159);

    QTcpSocket offender;
    FrameDecoder offender_decoder;
    std::vector<Frame> offender_frames;
    raw_handshake(offender, offender_decoder, offender_frames, server);
    auto finish = coredesk::protocol::encode_file_finish_payload({valid_transfer_id('8')});
    ASSERT_TRUE(finish.ok());
    write_payload_frame(offender, MessageType::FileFinish, 160, std::move(finish).value());
    ASSERT_TRUE(wait_until([&]() {
        return offender.state() == QAbstractSocket::UnconnectedState;
    }));
    EXPECT_EQ(server.active_transfer_count(), 1U);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "finish-owner.bin.coredesk.part"));
    complete_raw_transfer(owner, owner_decoder, owner_frames, owner_id, owner_data, 165, 166);
    EXPECT_EQ(read_file_bytes(receive.path() / "finish-owner.bin"), owner_data);
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "finish-owner.bin.coredesk.part"));
}

TEST(TcpTransferIntegrationTest, TransferManagerStartStopAndStatusAreIdempotent)
{
    app();
    TempDirectory temp;
    TransferManager manager;
    ASSERT_FALSE(manager.enabled());

    auto directory = manager.set_receive_directory(temp.path());
    ASSERT_TRUE(directory.ok());
    EXPECT_EQ(manager.receive_directory(), temp.path());

    auto started = manager.start();
    ASSERT_TRUE(started.ok());
    EXPECT_TRUE(manager.enabled());
    EXPECT_NE(manager.listening_port(), 0U);
    EXPECT_EQ(manager.active_transfer_count(), 0U);

    auto repeated = manager.start();
    EXPECT_TRUE(repeated.ok());
    EXPECT_TRUE(manager.enabled());

    const auto status = manager.status();
    EXPECT_TRUE(status.enabled);
    EXPECT_EQ(status.port, manager.listening_port());
    EXPECT_EQ(status.receive_directory, temp.path());
    EXPECT_EQ(status.active_transfers, 0U);

    manager.stop();
    EXPECT_FALSE(manager.enabled());
    manager.stop();
    EXPECT_FALSE(manager.enabled());
}

TEST(TcpTransferIntegrationTest, TransferManagerReceiveDirectoryValidation)
{
    app();
    TempDirectory first;
    TempDirectory second;
    TransferManager manager;

    auto set_first = manager.set_receive_directory(first.path());
    ASSERT_TRUE(set_first.ok());
    EXPECT_EQ(manager.receive_directory(), first.path());

    auto started = manager.start();
    ASSERT_TRUE(started.ok());
    auto busy = manager.set_receive_directory(second.path());
    ASSERT_FALSE(busy.ok());
    EXPECT_EQ(busy.error().code, ErrorCode::Busy);
    EXPECT_EQ(manager.receive_directory(), first.path());

    manager.stop();
    const auto file_path = first.path() / "not-a-directory";
    first.write_file("not-a-directory", "x");
    auto invalid = manager.set_receive_directory(file_path);
    ASSERT_FALSE(invalid.ok());
    EXPECT_NE(invalid.error().code, ErrorCode::Ok);
    EXPECT_EQ(manager.receive_directory(), first.path());

    const auto nested = second.path() / "nested" / "receive";
    auto set_nested = manager.set_receive_directory(nested);
    ASSERT_TRUE(set_nested.ok());
    EXPECT_EQ(manager.receive_directory(), nested);
    EXPECT_TRUE(std::filesystem::is_directory(nested));
}

TEST(TcpTransferIntegrationTest, FileOfferAcceptedAfterHelloHandshake)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    std::vector<std::pair<coredesk::RequestId, FileAcceptPayload>> accepts;
    client.set_file_accept_callback([&](coredesk::RequestId request_id, const FileAcceptPayload& payload) {
        accepts.emplace_back(request_id, payload);
    });
    connect_client_and_wait(client, server);

    const auto offer = make_offer(valid_transfer_id('1'), "accepted.bin");
    const auto request_id = client.send_file_offer(offer);

    ASSERT_TRUE(wait_until([&]() {
        return !accepts.empty();
    }));
    EXPECT_EQ(client.offer_state(), TcpTransferClient::OfferState::Accepted);
    EXPECT_EQ(accepts[0].first, request_id);
    EXPECT_EQ(accepts[0].second.transfer_id, offer.transfer_id);
    EXPECT_EQ(accepts[0].second.start_offset, 0U);

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, FileOfferPathTraversalIsRejected)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), server.server_port());
    ASSERT_TRUE(socket.waitForConnected(3000));

    FrameDecoder decoder;
    std::vector<Frame> frames;
    auto hello = coredesk::protocol::encode_hello_payload(coredesk::protocol::HelloPayload{1, "RawClient"});
    ASSERT_TRUE(hello.ok());
    write_frame(socket, Frame{MessageType::Hello, 0, 10, std::move(hello).value()});
    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::HelloAck;
    }));

    const auto invalid_offer =
        "{\"transfer_id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2\","
        "\"file_name\":\"../evil.bin\","
        "\"file_size\":128,"
        "\"chunk_size\":65536,"
        "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}";
    write_frame(socket, Frame{MessageType::FileOffer, 0, 11, bytes_from_text(invalid_offer)});

    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::FileReject && frame.request_id == 11;
    }));
    const auto reject_frame = std::find_if(frames.begin(), frames.end(), [](const Frame& frame) {
        return frame.type == MessageType::FileReject && frame.request_id == 11;
    });
    ASSERT_NE(reject_frame, frames.end());
    auto reject = coredesk::protocol::decode_file_reject_payload(reject_frame->payload);
    ASSERT_TRUE(reject.ok());
    EXPECT_EQ(reject.value().code, ErrorCode::InvalidArgument);

    socket.disconnectFromHost();
    server.close();
}

TEST(TcpTransferIntegrationTest, FileOfferTargetExistsIsRejected)
{
    app();
    TempDirectory temp;
    temp.write_file("exists.bin", "already here");
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    std::vector<std::pair<coredesk::RequestId, FileRejectPayload>> rejects;
    client.set_file_reject_callback([&](coredesk::RequestId request_id, const FileRejectPayload& payload) {
        rejects.emplace_back(request_id, payload);
    });
    connect_client_and_wait(client, server);

    const auto request_id = client.send_file_offer(make_offer(valid_transfer_id('3'), "exists.bin"));
    ASSERT_TRUE(wait_until([&]() {
        return !rejects.empty();
    }));
    EXPECT_EQ(client.offer_state(), TcpTransferClient::OfferState::Rejected);
    EXPECT_EQ(rejects[0].first, request_id);
    EXPECT_EQ(rejects[0].second.code, ErrorCode::TargetExists);

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, FileOfferInvalidTransferIdIsRejected)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), server.server_port());
    ASSERT_TRUE(socket.waitForConnected(3000));

    FrameDecoder decoder;
    std::vector<Frame> frames;
    auto hello = coredesk::protocol::encode_hello_payload(coredesk::protocol::HelloPayload{1, "RawClient"});
    ASSERT_TRUE(hello.ok());
    write_frame(socket, Frame{MessageType::Hello, 0, 20, std::move(hello).value()});
    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::HelloAck;
    }));

    const auto invalid_offer =
        "{\"transfer_id\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
        "\"file_name\":\"bad-id.bin\","
        "\"file_size\":128,"
        "\"chunk_size\":65536,"
        "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}";
    write_frame(socket, Frame{MessageType::FileOffer, 0, 21, bytes_from_text(invalid_offer)});

    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::FileReject && frame.request_id == 21;
    }));
    const auto reject_frame = std::find_if(frames.begin(), frames.end(), [](const Frame& frame) {
        return frame.type == MessageType::FileReject && frame.request_id == 21;
    });
    ASSERT_NE(reject_frame, frames.end());
    auto reject = coredesk::protocol::decode_file_reject_payload(reject_frame->payload);
    ASSERT_TRUE(reject.ok());
    EXPECT_EQ(reject.value().code, ErrorCode::InvalidArgument);

    socket.disconnectFromHost();
    server.close();
}

TEST(TcpTransferIntegrationTest, FileOfferBusyReceiveIsRejected)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    std::vector<std::pair<coredesk::RequestId, FileAcceptPayload>> accepts;
    std::vector<std::pair<coredesk::RequestId, FileRejectPayload>> rejects;
    client.set_file_accept_callback([&](coredesk::RequestId request_id, const FileAcceptPayload& payload) {
        accepts.emplace_back(request_id, payload);
    });
    client.set_file_reject_callback([&](coredesk::RequestId request_id, const FileRejectPayload& payload) {
        rejects.emplace_back(request_id, payload);
    });
    connect_client_and_wait(client, server);

    client.send_file_offer(make_offer(valid_transfer_id('4'), "first.bin"));
    ASSERT_TRUE(wait_until([&]() {
        return !accepts.empty();
    }));

    const auto second_request_id = client.send_file_offer(make_offer(valid_transfer_id('5'), "second.bin"));
    ASSERT_TRUE(wait_until([&]() {
        return !rejects.empty();
    }));
    EXPECT_EQ(client.offer_state(), TcpTransferClient::OfferState::Rejected);
    EXPECT_EQ(rejects[0].first, second_request_id);
    EXPECT_EQ(rejects[0].second.code, ErrorCode::Busy);

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, ReceiverWritesTenKilobyteFileAndRemovesPart)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    FrameDecoder decoder;
    std::vector<Frame> frames;
    raw_handshake(socket, decoder, frames, server);

    std::string text;
    text.reserve(10 * 1024);
    for (int i = 0; i < 10 * 1024; ++i) {
        text.push_back(static_cast<char>('a' + (i % 26)));
    }
    const auto data = bytes_from_string(text);
    const auto transfer_id = valid_transfer_id('6');
    raw_offer(socket, decoder, frames, make_offer_for_data(transfer_id, "ten-k.bin", data), 200);

    const auto first = std::vector<std::byte>(data.begin(), data.begin() + 4096);
    const auto second = std::vector<std::byte>(data.begin() + 4096, data.end());
    auto first_chunk = coredesk::protocol::encode_file_chunk_payload(coredesk::protocol::FileChunkPayload{transfer_id, 0, first});
    ASSERT_TRUE(first_chunk.ok());
    write_payload_frame(socket, MessageType::FileChunk, 201, std::move(first_chunk).value());
    auto second_chunk = coredesk::protocol::encode_file_chunk_payload(coredesk::protocol::FileChunkPayload{transfer_id, 4096, second});
    ASSERT_TRUE(second_chunk.ok());
    write_payload_frame(socket, MessageType::FileChunk, 202, std::move(second_chunk).value());
    auto finish = coredesk::protocol::encode_file_finish_payload(coredesk::protocol::FileFinishPayload{transfer_id});
    ASSERT_TRUE(finish.ok());
    write_payload_frame(socket, MessageType::FileFinish, 203, std::move(finish).value());

    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::FileResult && frame.request_id == 203;
    }));
    const auto result = decode_result_from_frames(frames, 203);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.code, ErrorCode::Ok);
    EXPECT_TRUE(std::filesystem::exists(temp.path() / "ten-k.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "ten-k.bin.coredesk.part"));
    EXPECT_EQ(read_file_bytes(temp.path() / "ten-k.bin"), data);

    socket.disconnectFromHost();
    server.close();
}

TEST(TcpTransferIntegrationTest, ReceiverRejectsWrongOffsetAndDeletesPart)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    FrameDecoder decoder;
    std::vector<Frame> frames;
    raw_handshake(socket, decoder, frames, server);

    const auto data = bytes_from_string("abcdef");
    const auto transfer_id = valid_transfer_id('7');
    raw_offer(socket, decoder, frames, make_offer_for_data(transfer_id, "wrong-offset.bin", data), 300);

    auto chunk = coredesk::protocol::encode_file_chunk_payload(coredesk::protocol::FileChunkPayload{transfer_id, 1, data});
    ASSERT_TRUE(chunk.ok());
    write_payload_frame(socket, MessageType::FileChunk, 301, std::move(chunk).value());

    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::FileResult && frame.request_id == 301;
    }));
    const auto result = decode_result_from_frames(frames, 301);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "wrong-offset.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "wrong-offset.bin.coredesk.part"));

    socket.disconnectFromHost();
    server.close();
}

TEST(TcpTransferIntegrationTest, FileChunkOverflowBoundaryRejected)
{
    const auto bounds = coredesk::qt_network::detail::validate_chunk_bounds(
        std::numeric_limits<std::uint64_t>::max() - 1U,
        std::numeric_limits<std::uint64_t>::max(),
        3U);
    ASSERT_FALSE(bounds.ok());
    EXPECT_EQ(bounds.error().code, ErrorCode::InvalidArgument);

    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    FrameDecoder decoder;
    std::vector<Frame> frames;
    raw_handshake(socket, decoder, frames, server);

    const auto data = bytes_from_string("abc");
    const auto transfer_id = valid_transfer_id('0');
    auto offer = make_offer_for_data(transfer_id, "overflow-boundary.bin", data);
    offer.file_size = 2;
    raw_offer(socket, decoder, frames, offer, 350);

    auto chunk = coredesk::protocol::encode_file_chunk_payload(coredesk::protocol::FileChunkPayload{transfer_id, 0, data});
    ASSERT_TRUE(chunk.ok());
    write_payload_frame(socket, MessageType::FileChunk, 351, std::move(chunk).value());

    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::FileResult && frame.request_id == 351;
    }));
    const auto result = decode_result_from_frames(frames, 351);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "overflow-boundary.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "overflow-boundary.bin.coredesk.part"));

    socket.disconnectFromHost();
    server.close();
}

TEST(TcpTransferIntegrationTest, ReceiverCleansPartOnDisconnect)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    {
        QTcpSocket socket;
        FrameDecoder decoder;
        std::vector<Frame> frames;
        raw_handshake(socket, decoder, frames, server);

        const auto data = bytes_from_string("partial transfer data");
        const auto transfer_id = valid_transfer_id('8');
        raw_offer(socket, decoder, frames, make_offer_for_data(transfer_id, "disconnect.bin", data), 400);

        const auto partial = std::vector<std::byte>(data.begin(), data.begin() + 7);
        auto chunk = coredesk::protocol::encode_file_chunk_payload(coredesk::protocol::FileChunkPayload{transfer_id, 0, partial});
        ASSERT_TRUE(chunk.ok());
        write_payload_frame(socket, MessageType::FileChunk, 401, std::move(chunk).value());
        ASSERT_TRUE(wait_until([&]() {
            return std::filesystem::exists(temp.path() / "disconnect.bin.coredesk.part");
        }));
        socket.abort();
    }

    ASSERT_TRUE(wait_until([&]() {
        return !std::filesystem::exists(temp.path() / "disconnect.bin.coredesk.part");
    }));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "disconnect.bin"));

    server.close();
}

TEST(TcpTransferIntegrationTest, ReceiverRejectsHashMismatchAndDeletesPart)
{
    app();
    TempDirectory temp;
    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(temp.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    QTcpSocket socket;
    FrameDecoder decoder;
    std::vector<Frame> frames;
    raw_handshake(socket, decoder, frames, server);

    const auto data = bytes_from_string("hash mismatch data");
    auto offer = make_offer_for_data(valid_transfer_id('9'), "hash-mismatch.bin", data);
    offer.sha256 = std::string(64, '0');
    raw_offer(socket, decoder, frames, offer, 500);

    auto chunk = coredesk::protocol::encode_file_chunk_payload(coredesk::protocol::FileChunkPayload{offer.transfer_id, 0, data});
    ASSERT_TRUE(chunk.ok());
    write_payload_frame(socket, MessageType::FileChunk, 501, std::move(chunk).value());
    auto finish = coredesk::protocol::encode_file_finish_payload(coredesk::protocol::FileFinishPayload{offer.transfer_id});
    ASSERT_TRUE(finish.ok());
    write_payload_frame(socket, MessageType::FileFinish, 502, std::move(finish).value());

    ASSERT_TRUE(wait_for_frame(socket, decoder, frames, [](const Frame& frame) {
        return frame.type == MessageType::FileResult && frame.request_id == 502;
    }));
    const auto result = decode_result_from_frames(frames, 502);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.code, ErrorCode::HashMismatch);
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "hash-mismatch.bin"));
    EXPECT_FALSE(std::filesystem::exists(temp.path() / "hash-mismatch.bin.coredesk.part"));

    socket.disconnectFromHost();
    server.close();
}

TEST(TcpTransferIntegrationTest, SenderTransfersSmallFileToReceiver)
{
    app();
    TempDirectory source;
    TempDirectory receive;
    const auto data = bytes_from_string("small sender file");
    source.write_binary_file("small-send.bin", data);

    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    std::vector<std::pair<coredesk::RequestId, FileResultPayload>> results;
    client.set_file_result_callback([&](coredesk::RequestId request_id, const FileResultPayload& payload) {
        results.emplace_back(request_id, payload);
    });
    connect_client_and_wait(client, server);

    auto sent = client.send_file(QString::fromStdWString((source.path() / "small-send.bin").wstring()));
    ASSERT_TRUE(sent.ok());
    ASSERT_TRUE(wait_until([&]() {
        return !results.empty();
    }));

    EXPECT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Completed);
    EXPECT_TRUE(results[0].second.ok);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "small-send.bin"));
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "small-send.bin.coredesk.part"));
    const auto received = read_file_bytes(receive.path() / "small-send.bin");
    EXPECT_EQ(received, data);
    EXPECT_EQ(sha256_hex(received), sha256_hex(data));

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, SenderTransfersTenMiBFileToReceiver)
{
    app();
    TempDirectory source;
    TempDirectory receive;
    const auto data = patterned_bytes(10U * 1024U * 1024U);
    source.write_binary_file("ten-mib-send.bin", data);

    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    std::vector<std::pair<coredesk::RequestId, FileResultPayload>> results;
    client.set_file_result_callback([&](coredesk::RequestId request_id, const FileResultPayload& payload) {
        results.emplace_back(request_id, payload);
    });
    connect_client_and_wait(client, server);

    auto sent = client.send_file(QString::fromStdWString((source.path() / "ten-mib-send.bin").wstring()));
    ASSERT_TRUE(sent.ok());
    ASSERT_TRUE(wait_until([&]() {
        return !results.empty();
    }, 30000));

    EXPECT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Completed);
    ASSERT_TRUE(results[0].second.ok);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "ten-mib-send.bin"));
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "ten-mib-send.bin.coredesk.part"));
    const auto received = read_file_bytes(receive.path() / "ten-mib-send.bin");
    EXPECT_EQ(received.size(), data.size());
    EXPECT_EQ(sha256_hex(received), sha256_hex(data));

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, PausedReceiverBoundsQueueAndResumesTransfer)
{
    app();
    BuildTestDirectory source;
    constexpr std::uint64_t file_size = 32U * 1024U * 1024U;
    const auto source_path = source.path() / "backpressure-32-mib.bin";
    write_pattern_file(source_path, file_size);

    PausingReceiver receiver;
    ASSERT_TRUE(receiver.listen());

    TcpTransferClient client(QStringLiteral("BackpressureClient"));
    std::vector<FileResultPayload> results;
    std::vector<Error> errors;
    client.set_file_result_callback([&](coredesk::RequestId, const FileResultPayload& payload) {
        results.push_back(payload);
    });
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });
    client.connect_to_host(QStringLiteral("127.0.0.1"), receiver.port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));

#ifdef _WIN32
    auto sent = client.send_file(QString::fromStdWString(source_path.wstring()));
#else
    auto sent = client.send_file(QString::fromStdString(source_path.string()));
#endif
    ASSERT_TRUE(sent.ok());
    ASSERT_TRUE(wait_until([&]() {
        return receiver.offer_accepted();
    }, 60000));
    ASSERT_TRUE(wait_until([&]() {
        return TcpTransferClientTestPeer::pending_write_bytes(client) >=
            TcpTransferClientTestPeer::high_water_mark();
    }, 30000));

    const auto offset_while_paused = TcpTransferClientTestPeer::send_offset(client);
    const auto pending_while_paused = TcpTransferClientTestPeer::pending_write_bytes(client);
    EXPECT_LT(offset_while_paused, file_size);
    EXPECT_LE(pending_while_paused,
              TcpTransferClientTestPeer::high_water_mark() + TcpTransferClientTestPeer::chunk_size() + 1024);
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 250));
    EXPECT_LT(TcpTransferClientTestPeer::send_offset(client), file_size);
    EXPECT_LE(TcpTransferClientTestPeer::pending_write_bytes(client),
              TcpTransferClientTestPeer::high_water_mark() + TcpTransferClientTestPeer::chunk_size() + 1024);

    receiver.resume();
    ASSERT_TRUE(wait_until([&]() {
        return !results.empty() || !errors.empty();
    }, 60000));
    ASSERT_TRUE(errors.empty());
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results[0].ok);
    EXPECT_EQ(receiver.received_bytes(), file_size);
    EXPECT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Completed);
}

TEST(TcpTransferIntegrationTest, PartialWriteRemainderPreservesFrameOrderAndCompletes)
{
    app();
    BuildTestDirectory source;
    constexpr std::uint64_t file_size = 3U * 256U * 1024U;
    const auto source_path = source.path() / "partial-write.bin";
    write_pattern_file(source_path, file_size);

    PausingReceiver receiver;
    ASSERT_TRUE(receiver.listen());
    receiver.resume();

    TcpTransferClient client(QStringLiteral("PartialWriteClient"));
    TcpTransferClientTestPeer::set_write_acceptance_limit(client, 1024);
    std::vector<FileResultPayload> results;
    std::vector<Error> errors;
    client.set_file_result_callback([&](coredesk::RequestId, const FileResultPayload& payload) {
        results.push_back(payload);
    });
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });
    client.connect_to_host(QStringLiteral("127.0.0.1"), receiver.port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));

#ifdef _WIN32
    ASSERT_TRUE(client.send_file(QString::fromStdWString(source_path.wstring())).ok());
#else
    ASSERT_TRUE(client.send_file(QString::fromStdString(source_path.string())).ok());
#endif
    ASSERT_TRUE(wait_until([&]() {
        return TcpTransferClientTestPeer::remainder_bytes(client) > 128 * 1024;
    }, 10000));
    const auto offset_with_remainder = TcpTransferClientTestPeer::send_offset(client);
    EXPECT_EQ(offset_with_remainder, static_cast<std::uint64_t>(TcpTransferClientTestPeer::chunk_size()));
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 25));
    EXPECT_EQ(TcpTransferClientTestPeer::send_offset(client), offset_with_remainder);
    EXPECT_GT(TcpTransferClientTestPeer::remainder_bytes(client), 0);
    EXPECT_LE(TcpTransferClientTestPeer::pending_write_bytes(client),
              TcpTransferClientTestPeer::high_water_mark() + TcpTransferClientTestPeer::chunk_size() + 1024);

    ASSERT_TRUE(wait_until([&]() {
        return !results.empty() || !errors.empty();
    }, 30000));
    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results[0].ok);
    EXPECT_EQ(receiver.received_bytes(), file_size);
    EXPECT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Completed);
    EXPECT_EQ(TcpTransferClientTestPeer::remainder_bytes(client), 0);
    receiver.repeat_last_file_result();
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 100));
    EXPECT_EQ(results.size(), 1U);
}

TEST(TcpTransferIntegrationTest, UnexpectedHelloAckDoesNotResetActiveTransfer)
{
    app();
    BuildTestDirectory source;
    const auto source_path = source.path() / "unexpected-hello.bin";
    write_pattern_file(source_path, 512U * 1024U);
    PausingReceiver receiver;
    ASSERT_TRUE(receiver.listen());

    TcpTransferClient client(QStringLiteral("UnexpectedHelloClient"));
    TcpTransferClientTestPeer::set_write_acceptance_limit(client, 1024);
    std::vector<Error> errors;
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });
    client.connect_to_host(QStringLiteral("127.0.0.1"), receiver.port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));
#ifdef _WIN32
    ASSERT_TRUE(client.send_file(QString::fromStdWString(source_path.wstring())).ok());
#else
    ASSERT_TRUE(client.send_file(QString::fromStdString(source_path.string())).ok());
#endif
    ASSERT_TRUE(wait_until([&]() {
        return client.transfer_state() == TcpTransferClient::TransferState::Sending;
    }, 10000));
    receiver.send_unexpected_hello_ack(1);
    ASSERT_TRUE(wait_until([&]() {
        return client.transfer_state() == TcpTransferClient::TransferState::Failed;
    }));
    EXPECT_NE(client.transfer_state(), TcpTransferClient::TransferState::Idle);
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 100));
    EXPECT_EQ(errors.size(), 1U);
}

TEST(TcpTransferIntegrationTest, DuplicateHelloAckOutsideHelloSentFailsOnce)
{
    app();
    PausingReceiver receiver;
    ASSERT_TRUE(receiver.listen());
    TcpTransferClient client(QStringLiteral("DuplicateHelloClient"));
    std::vector<Error> errors;
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });
    client.connect_to_host(QStringLiteral("127.0.0.1"), receiver.port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));
    receiver.send_unexpected_hello_ack(1);
    ASSERT_TRUE(wait_until([&]() {
        return client.transfer_state() == TcpTransferClient::TransferState::Failed;
    }));
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 100));
    EXPECT_EQ(errors.size(), 1U);
}

TEST(TcpTransferIntegrationTest, FileAcceptOutsideOfferingFailsOnce)
{
    app();
    PausingReceiver receiver;
    ASSERT_TRUE(receiver.listen());
    TcpTransferClient client(QStringLiteral("UnexpectedAcceptClient"));
    std::vector<Error> errors;
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });
    client.connect_to_host(QStringLiteral("127.0.0.1"), receiver.port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));
    receiver.send_unexpected_file_accept(201, valid_transfer_id('9'));
    ASSERT_TRUE(wait_until([&]() {
        return client.transfer_state() == TcpTransferClient::TransferState::Failed;
    }));
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 100));
    EXPECT_EQ(errors.size(), 1U);
}

TEST(TcpTransferIntegrationTest, FileResultOutsideFinishingFailsOnce)
{
    app();
    PausingReceiver receiver;
    ASSERT_TRUE(receiver.listen());
    TcpTransferClient client(QStringLiteral("UnexpectedResultClient"));
    std::vector<Error> errors;
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });
    client.connect_to_host(QStringLiteral("127.0.0.1"), receiver.port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));
    receiver.send_unexpected_file_result(202, valid_transfer_id('0'));
    ASSERT_TRUE(wait_until([&]() {
        return client.transfer_state() == TcpTransferClient::TransferState::Failed;
    }));
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 100));
    EXPECT_EQ(errors.size(), 1U);
}

TEST(TcpTransferIntegrationTest, FileRejectWrongTransferIdFailsCorrelation)
{
    app();
    PausingReceiver receiver;
    receiver.set_auto_accept_offers(false);
    ASSERT_TRUE(receiver.listen());

    TcpTransferClient client(QStringLiteral("RejectCorrelationClient"));
    std::vector<Error> errors;
    std::vector<FileRejectPayload> rejects;
    client.set_error_callback([&](const Error& error) {
        errors.push_back(error);
    });
    client.set_file_reject_callback([&](coredesk::RequestId, const FileRejectPayload& payload) {
        rejects.push_back(payload);
    });
    client.connect_to_host(QStringLiteral("127.0.0.1"), receiver.port());
    ASSERT_TRUE(wait_until([&]() {
        return client.handshake_complete();
    }));

    const auto current_transfer_id = valid_transfer_id('b');
    const auto request_id = client.send_file_offer(make_offer(current_transfer_id, "correlation.bin"));
    ASSERT_NE(request_id, 0U);
    ASSERT_TRUE(wait_until([&]() {
        return receiver.offer_received();
    }));
    ASSERT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Offering);

    receiver.send_file_reject(request_id, valid_transfer_id('c'));
    ASSERT_TRUE(wait_until([&]() {
        return client.transfer_state() == TcpTransferClient::TransferState::Failed;
    }));
    EXPECT_NE(client.transfer_state(), TcpTransferClient::TransferState::Idle);
    EXPECT_TRUE(rejects.empty());
    EXPECT_FALSE(wait_until([]() {
        return false;
    }, 100));
    EXPECT_EQ(errors.size(), 1U);
    EXPECT_TRUE(rejects.empty());
}

TEST(TcpTransferIntegrationTest, SenderSha256PreparationDoesNotBlockEventLoop)
{
    app();
    TempDirectory source;
    TempDirectory receive;
    const auto data = patterned_bytes(32U * 1024U * 1024U);
    source.write_binary_file("event-loop-send.bin", data);

    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    std::vector<std::pair<coredesk::RequestId, FileResultPayload>> results;
    client.set_file_result_callback([&](coredesk::RequestId request_id, const FileResultPayload& payload) {
        results.emplace_back(request_id, payload);
    });
    connect_client_and_wait(client, server);

    int timer_ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        ++timer_ticks;
    });
    timer.start(1);

    auto sent = client.send_file(QString::fromStdWString((source.path() / "event-loop-send.bin").wstring()));
    ASSERT_TRUE(sent.ok());
    ASSERT_TRUE(wait_until([&]() {
        return timer_ticks > 0 && results.empty();
    }, 3000));
    timer.stop();

    ASSERT_TRUE(wait_until([&]() {
        return !results.empty();
    }, 30000));
    EXPECT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Completed);
    ASSERT_TRUE(results[0].second.ok);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "event-loop-send.bin"));
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "event-loop-send.bin.coredesk.part"));
    EXPECT_EQ(sha256_hex(read_file_bytes(receive.path() / "event-loop-send.bin")), sha256_hex(data));

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, SenderFileNotFoundReturnsError)
{
    app();

    TcpTransferClient client(QStringLiteral("ClientNode"));
    auto sent = client.send_file(QStringLiteral("Z:/definitely/not/found/coredesk-missing.bin"));
    ASSERT_FALSE(sent.ok());
    EXPECT_EQ(sent.error().code, ErrorCode::PathNotFound);
}

TEST(TcpTransferIntegrationTest, SenderHandlesReceiverReject)
{
    app();
    TempDirectory source;
    TempDirectory receive;
    const auto data = bytes_from_string("existing target");
    source.write_binary_file("reject-existing.bin", data);
    receive.write_binary_file("reject-existing.bin", bytes_from_string("already exists"));

    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    std::vector<std::pair<coredesk::RequestId, FileRejectPayload>> rejects;
    client.set_file_reject_callback([&](coredesk::RequestId request_id, const FileRejectPayload& payload) {
        rejects.emplace_back(request_id, payload);
    });
    connect_client_and_wait(client, server);

    auto sent = client.send_file(QString::fromStdWString((source.path() / "reject-existing.bin").wstring()));
    ASSERT_TRUE(sent.ok());
    ASSERT_TRUE(wait_until([&]() {
        return !rejects.empty();
    }));

    EXPECT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Failed);
    EXPECT_EQ(client.offer_state(), TcpTransferClient::OfferState::Rejected);
    EXPECT_EQ(rejects[0].second.code, ErrorCode::TargetExists);

    client.disconnect_from_host();
    server.close();
}

TEST(TcpTransferIntegrationTest, SenderFailsWhenConnectionDrops)
{
    app();
    TempDirectory source;
    TempDirectory receive;
    const auto data = patterned_bytes(1024U * 1024U);
    source.write_binary_file("drop-send.bin", data);

    TcpTransferServer server(QStringLiteral("ServerNode"));
    server.set_receive_directory(receive.path());
    ASSERT_TRUE(server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    connect_client_and_wait(client, server);

    auto sent = client.send_file(QString::fromStdWString((source.path() / "drop-send.bin").wstring()));
    ASSERT_TRUE(sent.ok());
    server.close();

    ASSERT_TRUE(wait_until([&]() {
        return client.transfer_state() == TcpTransferClient::TransferState::Failed || !client.is_connected();
    }));
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "drop-send.bin"));

    client.disconnect_from_host();
}
