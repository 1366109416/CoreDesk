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
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
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
