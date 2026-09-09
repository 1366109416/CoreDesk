#include "LocalIpcServer.h"
#include "LocalIpcClient.h"
#include "TcpTransferClient.h"
#include "TcpTransferServer.h"
#include "TransferManager.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/protocol/JsonPayload.h"
#include "coredesk/service/ServiceController.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QHostAddress>
#include <QLocalServer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using coredesk::protocol::FileAcceptPayload;
using coredesk::protocol::FileResultPayload;
using coredesk::qt_ipc::LocalIpcServer;
using coredesk::qt_ipc::LocalIpcClient;
using coredesk::qt_network::TcpTransferClient;
using coredesk::qt_network::TcpTransferServer;
using coredesk::service::ServiceController;
using coredesk::service::TransferManager;

QCoreApplication& app()
{
    if (auto* existing = QCoreApplication::instance()) {
        return *existing;
    }

    static int argc = 1;
    static char app_name[] = "coredesk_service_tcp_integration_tests";
    static char* argv[] = {app_name, nullptr};
    static QCoreApplication application(argc, argv);
    return application;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 10000)
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
    explicit TempDirectory(std::string_view prefix)
        : path_(std::filesystem::temp_directory_path() /
                (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
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

std::vector<std::byte> bytes_from_string(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
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

std::string path_to_utf8_string(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path path_from_utf8_string(const std::string& text)
{
    std::u8string utf8;
    utf8.reserve(text.size());
    for (const unsigned char ch : text) {
        utf8.push_back(static_cast<char8_t>(ch));
    }
    return std::filesystem::path(utf8);
}

coredesk::qt_ipc::TransferManagementHandlers outgoing_handlers_for(TransferManager& manager)
{
    coredesk::qt_ipc::TransferManagementHandlers handlers;
    handlers.send_file = [&manager](const coredesk::protocol::SendFileRequestPayload& payload,
                                    coredesk::qt_ipc::TransferManagementHandlers::SendFileCompletion completion) {
        return manager.send_file(path_from_utf8_string(payload.file_path),
                                 payload.host,
                                 payload.port,
                                 std::move(completion));
    };
    return handlers;
}

QByteArray byte_array_from_bytes(std::span<const std::byte> bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

std::vector<std::byte> bytes_from_byte_array(const QByteArray& bytes)
{
    std::vector<std::byte> result;
    result.reserve(static_cast<std::size_t>(bytes.size()));
    for (const char value : bytes) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

class DropAfterFirstChunkPeer {
public:
    DropAfterFirstChunkPeer()
    {
        QObject::connect(&server_, &QTcpServer::newConnection, [&]() {
            socket_ = server_.nextPendingConnection();
            QObject::connect(socket_, &QTcpSocket::readyRead, [this]() { handle_ready_read(); });
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

    bool offer_seen() const noexcept
    {
        return offer_seen_;
    }

    bool chunk_seen() const noexcept
    {
        return chunk_seen_;
    }

    bool protocol_error() const noexcept
    {
        return protocol_error_;
    }

private:
    void send_frame(coredesk::protocol::Frame frame)
    {
        auto encoded = coredesk::protocol::FrameEncoder::encode(frame);
        if (!encoded.ok() || !socket_) {
            protocol_error_ = true;
            return;
        }
        socket_->write(byte_array_from_bytes(encoded.value()));
    }

    void handle_ready_read()
    {
        auto decoded = decoder_.push(bytes_from_byte_array(socket_->readAll()));
        if (!decoded.ok()) {
            protocol_error_ = true;
            socket_->abort();
            return;
        }
        for (const auto& frame : decoded.value()) {
            if (frame.type == coredesk::protocol::MessageType::Hello) {
                auto payload = coredesk::protocol::encode_hello_ack_payload({1, "DropPeer"});
                if (!payload.ok()) {
                    protocol_error_ = true;
                    return;
                }
                send_frame({coredesk::protocol::MessageType::HelloAck,
                            0,
                            frame.request_id,
                            std::move(payload).value()});
                continue;
            }
            if (frame.type == coredesk::protocol::MessageType::FileOffer) {
                auto offer = coredesk::protocol::decode_file_offer_payload(frame.payload);
                if (!offer.ok()) {
                    protocol_error_ = true;
                    return;
                }
                offer_seen_ = true;
                auto payload = coredesk::protocol::encode_file_accept_payload({offer.value().transfer_id, 0});
                if (!payload.ok()) {
                    protocol_error_ = true;
                    return;
                }
                send_frame({coredesk::protocol::MessageType::FileAccept,
                            0,
                            frame.request_id,
                            std::move(payload).value()});
                continue;
            }
            if (frame.type == coredesk::protocol::MessageType::FileChunk) {
                chunk_seen_ = true;
                socket_->abort();
                return;
            }
        }
    }

    QTcpServer server_;
    QTcpSocket* socket_{};
    coredesk::protocol::FrameDecoder decoder_;
    bool offer_seen_{false};
    bool chunk_seen_{false};
    bool protocol_error_{false};
};

} // namespace

TEST(ServiceTcpIntegrationTest, ServiceCompositionReceivesLoopbackFile)
{
    app();
    TempDirectory source("coredesk_service_tcp_source_");
    TempDirectory receive("coredesk_service_tcp_receive_");
    const auto data = bytes_from_string("service composition tcp transfer");
    source.write_binary_file("service-transfer.bin", data);

    ServiceController controller;
    LocalIpcServer ipc_server(controller);
    TcpTransferServer tcp_server(QStringLiteral("ServiceNode"));
    tcp_server.set_receive_directory(receive.path());
    ASSERT_TRUE(tcp_server.listen(0, QHostAddress::LocalHost).ok());

    TcpTransferClient client(QStringLiteral("ClientNode"));
    bool handshake_complete = false;
    std::vector<std::pair<coredesk::RequestId, FileAcceptPayload>> accepts;
    std::vector<std::pair<coredesk::RequestId, FileResultPayload>> results;
    client.set_handshake_callback([&]() {
        handshake_complete = true;
    });
    client.set_file_accept_callback([&](coredesk::RequestId request_id, const FileAcceptPayload& payload) {
        accepts.emplace_back(request_id, payload);
    });
    client.set_file_result_callback([&](coredesk::RequestId request_id, const FileResultPayload& payload) {
        results.emplace_back(request_id, payload);
    });

    client.connect_to_host(QStringLiteral("127.0.0.1"), tcp_server.server_port());
    ASSERT_TRUE(wait_until([&]() {
        return handshake_complete;
    }));
    EXPECT_TRUE(client.handshake_complete());

    auto sent = client.send_file(QString::fromStdWString((source.path() / "service-transfer.bin").wstring()));
    ASSERT_TRUE(sent.ok());
    ASSERT_TRUE(wait_until([&]() {
        return !accepts.empty();
    }));
    ASSERT_TRUE(wait_until([&]() {
        return !results.empty();
    }));

    EXPECT_EQ(client.transfer_state(), TcpTransferClient::TransferState::Completed);
    EXPECT_TRUE(results[0].second.ok);
    EXPECT_TRUE(std::filesystem::exists(receive.path() / "service-transfer.bin"));
    EXPECT_FALSE(std::filesystem::exists(receive.path() / "service-transfer.bin.coredesk.part"));
    const auto received = read_file_bytes(receive.path() / "service-transfer.bin");
    EXPECT_EQ(received, data);
    EXPECT_EQ(sha256_hex(received), sha256_hex(data));

    client.disconnect_from_host();
    tcp_server.close();
    ipc_server.close();
}

TEST(ServiceTcpIntegrationTest, LocalIpcOutgoingTransferCompletesAndCanSendAgain)
{
    app();
    TempDirectory source("coredesk_outgoing_source_");
    TempDirectory receive("coredesk_outgoing_receive_");
    const auto small_data = bytes_from_string("outgoing through real local IPC");
    std::vector<std::byte> multi_chunk_data(300 * 1024);
    for (std::size_t i = 0; i < multi_chunk_data.size(); ++i) {
        multi_chunk_data[i] = static_cast<std::byte>(i % 251);
    }
    source.write_binary_file("small-outgoing.bin", small_data);
    source.write_binary_file("multi-outgoing.bin", multi_chunk_data);

    TcpTransferServer receiver(QStringLiteral("RemoteReceiver"));
    receiver.set_receive_directory(receive.path());
    ASSERT_TRUE(receiver.listen(0, QHostAddress::LocalHost).ok());

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer ipc_server(controller);
    ipc_server.set_transfer_management_handlers(outgoing_handlers_for(manager));
    const auto ipc_name = QStringLiteral("CoreDesk.M8A.E2E.%1")
                              .arg(std::chrono::steady_clock::now().time_since_epoch().count());
    QLocalServer::removeServer(ipc_name);
    ASSERT_TRUE(ipc_server.listen(ipc_name).ok());

    LocalIpcClient desktop_client;
    std::vector<coredesk::protocol::Frame> frames;
    desktop_client.set_frame_callback([&](const coredesk::protocol::Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(desktop_client.connect_to_server(ipc_name).ok());

    const auto send_and_verify = [&](const std::filesystem::path& source_path,
                                     const std::vector<std::byte>& expected) {
        frames.clear();
        const auto request_id = desktop_client.send_file_request(
            {path_to_utf8_string(source_path), "127.0.0.1", receiver.server_port()});
        ASSERT_TRUE(wait_until([&] {
            return std::any_of(frames.begin(), frames.end(), [request_id](const auto& frame) {
                return frame.request_id == request_id && frame.type == coredesk::protocol::MessageType::SendFileAccepted;
            });
        }));
        ASSERT_TRUE(wait_until([&] {
            return std::any_of(frames.begin(), frames.end(), [request_id](const auto& frame) {
                return frame.request_id == request_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
            });
        }));
        const auto result_it = std::find_if(frames.begin(), frames.end(), [request_id](const auto& frame) {
            return frame.request_id == request_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
        });
        ASSERT_NE(result_it, frames.end());
        auto result = coredesk::protocol::decode_send_file_result_payload(result_it->payload);
        ASSERT_TRUE(result.ok());
        EXPECT_TRUE(result.value().success);
        EXPECT_EQ(result.value().code, coredesk::ErrorCode::Ok);
        EXPECT_FALSE(manager.outgoing_active());
        const auto received_path = receive.path() / source_path.filename();
        ASSERT_TRUE(std::filesystem::exists(received_path));
        const auto received = read_file_bytes(received_path);
        EXPECT_EQ(received, expected);
        EXPECT_EQ(sha256_hex(received), sha256_hex(expected));
        EXPECT_FALSE(std::filesystem::exists(received_path.string() + ".coredesk.part"));
    };

    send_and_verify(source.path() / "small-outgoing.bin", small_data);
    send_and_verify(source.path() / "multi-outgoing.bin", multi_chunk_data);

    desktop_client.disconnect_from_server();
    ipc_server.close();
    receiver.close();
    QLocalServer::removeServer(ipc_name);
}

TEST(ServiceTcpIntegrationTest, LocalIpcTargetExistsReturnsTerminalErrorAndAllowsNextSend)
{
    app();
    TempDirectory source("coredesk_ipc_target_exists_source_");
    TempDirectory receive("coredesk_ipc_target_exists_receive_");
    source.write_binary_file("duplicate.bin", bytes_from_string("new contents"));
    source.write_binary_file("next.bin", bytes_from_string("next contents"));
    receive.write_binary_file("duplicate.bin", bytes_from_string("existing contents"));

    TcpTransferServer receiver(QStringLiteral("TargetExistsReceiver"));
    receiver.set_receive_directory(receive.path());
    ASSERT_TRUE(receiver.listen(0, QHostAddress::LocalHost).ok());

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer ipc_server(controller);
    ipc_server.set_transfer_management_handlers(outgoing_handlers_for(manager));
    const auto ipc_name = QStringLiteral("CoreDesk.M8A.TargetExists.%1")
                              .arg(std::chrono::steady_clock::now().time_since_epoch().count());
    QLocalServer::removeServer(ipc_name);
    ASSERT_TRUE(ipc_server.listen(ipc_name).ok());

    LocalIpcClient client;
    std::vector<coredesk::protocol::Frame> frames;
    client.set_frame_callback([&](const auto& frame) { frames.push_back(frame); });
    ASSERT_TRUE(client.connect_to_server(ipc_name).ok());

    const auto rejected_id = client.send_file_request(
        {path_to_utf8_string(source.path() / "duplicate.bin"), "127.0.0.1", receiver.server_port()});
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [rejected_id](const auto& frame) {
            return frame.request_id == rejected_id &&
                frame.type == coredesk::protocol::MessageType::SendFileAccepted;
        });
    }));
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [rejected_id](const auto& frame) {
            return frame.request_id == rejected_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
        });
    }));
    const auto rejected = std::find_if(frames.begin(), frames.end(), [rejected_id](const auto& frame) {
        return frame.request_id == rejected_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
    });
    ASSERT_NE(rejected, frames.end());
    auto rejected_result = coredesk::protocol::decode_send_file_result_payload(rejected->payload);
    ASSERT_TRUE(rejected_result.ok());
    EXPECT_FALSE(rejected_result.value().success);
    EXPECT_EQ(rejected_result.value().code, coredesk::ErrorCode::TargetExists);
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_EQ(read_file_bytes(receive.path() / "duplicate.bin"), bytes_from_string("existing contents"));

    const auto retry_id = client.send_file_request(
        {path_to_utf8_string(source.path() / "next.bin"), "127.0.0.1", receiver.server_port()});
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [retry_id](const auto& frame) {
            return frame.request_id == retry_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
        });
    }));
    const auto retry = std::find_if(frames.begin(), frames.end(), [retry_id](const auto& frame) {
        return frame.request_id == retry_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
    });
    ASSERT_NE(retry, frames.end());
    auto retry_result = coredesk::protocol::decode_send_file_result_payload(retry->payload);
    ASSERT_TRUE(retry_result.ok());
    EXPECT_TRUE(retry_result.value().success);
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_EQ(read_file_bytes(receive.path() / "next.bin"), bytes_from_string("next contents"));

    client.disconnect_from_server();
    ipc_server.close();
    receiver.close();
    QLocalServer::removeServer(ipc_name);
}

TEST(ServiceTcpIntegrationTest, OutgoingTransferSurvivesIpcDisconnectAndReconnectCanSendAgain)
{
    app();
    TempDirectory source("coredesk_ipc_disconnect_source_");
    TempDirectory receive("coredesk_ipc_disconnect_receive_");
    std::vector<std::byte> first_data(2 * 1024 * 1024);
    for (std::size_t i = 0; i < first_data.size(); ++i) {
        first_data[i] = static_cast<std::byte>(i % 251);
    }
    const auto second_data = bytes_from_string("send after reconnect");
    source.write_binary_file("survives-disconnect.bin", first_data);
    source.write_binary_file("after-reconnect.bin", second_data);

    TcpTransferServer receiver(QStringLiteral("DisconnectReceiver"));
    receiver.set_receive_directory(receive.path());
    ASSERT_TRUE(receiver.listen(0, QHostAddress::LocalHost).ok());

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer ipc_server(controller);
    ipc_server.set_transfer_management_handlers(outgoing_handlers_for(manager));
    const auto ipc_name = QStringLiteral("CoreDesk.M8A.IpcDisconnect.%1")
                              .arg(std::chrono::steady_clock::now().time_since_epoch().count());
    QLocalServer::removeServer(ipc_name);
    ASSERT_TRUE(ipc_server.listen(ipc_name).ok());

    LocalIpcClient first_client;
    bool accepted_seen = false;
    coredesk::RequestId first_id = 0;
    first_client.set_frame_callback([&](const auto& frame) {
        if (frame.request_id == first_id && frame.type == coredesk::protocol::MessageType::SendFileAccepted) {
            accepted_seen = true;
            first_client.disconnect_from_server();
        }
    });
    ASSERT_TRUE(first_client.connect_to_server(ipc_name).ok());
    first_id = first_client.send_file_request(
        {path_to_utf8_string(source.path() / "survives-disconnect.bin"), "127.0.0.1", receiver.server_port()});
    ASSERT_TRUE(wait_until([&] { return accepted_seen && !first_client.is_connected(); }));
    ASSERT_TRUE(wait_until([&] {
        return !manager.outgoing_active() && std::filesystem::exists(receive.path() / "survives-disconnect.bin");
    }, 20000));
    EXPECT_EQ(read_file_bytes(receive.path() / "survives-disconnect.bin"), first_data);

    LocalIpcClient second_client;
    std::vector<coredesk::protocol::Frame> frames;
    second_client.set_frame_callback([&](const auto& frame) { frames.push_back(frame); });
    ASSERT_TRUE(second_client.connect_to_server(ipc_name).ok());
    const auto second_id = second_client.send_file_request(
        {path_to_utf8_string(source.path() / "after-reconnect.bin"), "127.0.0.1", receiver.server_port()});
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [second_id](const auto& frame) {
            return frame.request_id == second_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
        });
    }));
    const auto result = std::find_if(frames.begin(), frames.end(), [second_id](const auto& frame) {
        return frame.request_id == second_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
    });
    ASSERT_NE(result, frames.end());
    auto decoded = coredesk::protocol::decode_send_file_result_payload(result->payload);
    ASSERT_TRUE(decoded.ok());
    EXPECT_TRUE(decoded.value().success);
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_EQ(read_file_bytes(receive.path() / "after-reconnect.bin"), second_data);

    second_client.disconnect_from_server();
    ipc_server.close();
    receiver.close();
    QLocalServer::removeServer(ipc_name);
}

TEST(ServiceTcpIntegrationTest, EstablishedOutgoingConnectionDropFailsOnceAndAllowsRetry)
{
    app();
    TempDirectory source("coredesk_tcp_drop_source_");
    TempDirectory receive("coredesk_tcp_drop_receive_");
    std::vector<std::byte> dropped_data(512 * 1024, std::byte{0x5a});
    const auto retry_data = bytes_from_string("retry after established drop");
    source.write_binary_file("drop.bin", dropped_data);
    source.write_binary_file("retry.bin", retry_data);

    DropAfterFirstChunkPeer dropping_peer;
    ASSERT_TRUE(dropping_peer.listen());
    TcpTransferServer retry_receiver(QStringLiteral("DropRecoveryReceiver"));
    retry_receiver.set_receive_directory(receive.path());
    ASSERT_TRUE(retry_receiver.listen(0, QHostAddress::LocalHost).ok());

    ServiceController controller;
    TransferManager manager;
    ASSERT_TRUE(manager.set_receive_directory(receive.path() / "manager-receiver").ok());
    ASSERT_TRUE(manager.start().ok());
    LocalIpcServer ipc_server(controller);
    ipc_server.set_transfer_management_handlers(outgoing_handlers_for(manager));
    const auto ipc_name = QStringLiteral("CoreDesk.M8A.TcpDrop.%1")
                              .arg(std::chrono::steady_clock::now().time_since_epoch().count());
    QLocalServer::removeServer(ipc_name);
    ASSERT_TRUE(ipc_server.listen(ipc_name).ok());

    LocalIpcClient client;
    std::vector<coredesk::protocol::Frame> frames;
    client.set_frame_callback([&](const auto& frame) { frames.push_back(frame); });
    ASSERT_TRUE(client.connect_to_server(ipc_name).ok());
    const auto dropped_id = client.send_file_request(
        {path_to_utf8_string(source.path() / "drop.bin"), "127.0.0.1", dropping_peer.port()});
    ASSERT_TRUE(wait_until([&] { return dropping_peer.offer_seen(); }));
    ASSERT_TRUE(wait_until([&] { return dropping_peer.chunk_seen(); }));
    ASSERT_FALSE(dropping_peer.protocol_error());
    ASSERT_TRUE(wait_until([&] {
        return std::count_if(frames.begin(), frames.end(), [dropped_id](const auto& frame) {
            return frame.request_id == dropped_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
        }) == 1;
    }));
    const auto dropped_result = std::find_if(frames.begin(), frames.end(), [dropped_id](const auto& frame) {
        return frame.request_id == dropped_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
    });
    ASSERT_NE(dropped_result, frames.end());
    auto decoded_failure = coredesk::protocol::decode_send_file_result_payload(dropped_result->payload);
    ASSERT_TRUE(decoded_failure.ok());
    EXPECT_FALSE(decoded_failure.value().success);
    EXPECT_NE(decoded_failure.value().code, coredesk::ErrorCode::Ok);
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_TRUE(manager.enabled());
    EXPECT_FALSE(wait_until([&] {
        return std::count_if(frames.begin(), frames.end(), [dropped_id](const auto& frame) {
            return frame.request_id == dropped_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
        }) > 1;
    }, 250));

    const auto retry_id = client.send_file_request(
        {path_to_utf8_string(source.path() / "retry.bin"), "127.0.0.1", retry_receiver.server_port()});
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [retry_id](const auto& frame) {
            return frame.request_id == retry_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
        });
    }));
    const auto retry_result = std::find_if(frames.begin(), frames.end(), [retry_id](const auto& frame) {
        return frame.request_id == retry_id && frame.type == coredesk::protocol::MessageType::SendFileResult;
    });
    ASSERT_NE(retry_result, frames.end());
    auto decoded_retry = coredesk::protocol::decode_send_file_result_payload(retry_result->payload);
    ASSERT_TRUE(decoded_retry.ok());
    EXPECT_TRUE(decoded_retry.value().success);
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_TRUE(manager.enabled());
    EXPECT_EQ(read_file_bytes(receive.path() / "retry.bin"), retry_data);

    client.disconnect_from_server();
    ipc_server.close();
    manager.stop();
    retry_receiver.close();
    QLocalServer::removeServer(ipc_name);
}

TEST(TransferManagerTest, BusyAndConnectionFailureReturnToIdleWithoutStoppingReceiver)
{
    app();
    TempDirectory source("coredesk_manager_source_");
    TempDirectory receive("coredesk_manager_receive_");
    source.write_binary_file("failure.bin", bytes_from_string("failure path"));

    TransferManager manager;
    ASSERT_TRUE(manager.set_receive_directory(receive.path()).ok());
    ASSERT_TRUE(manager.start().ok());
    ASSERT_TRUE(manager.enabled());

    QTcpServer port_probe;
    ASSERT_TRUE(port_probe.listen(QHostAddress::LocalHost, 0));
    const auto refused_port = port_probe.serverPort();
    port_probe.close();

    std::vector<coredesk::Result<void>> completions;
    auto first = manager.send_file(source.path() / "failure.bin", "127.0.0.1", refused_port, [&](auto result) {
        completions.push_back(std::move(result));
    });
    ASSERT_TRUE(first.ok());
    EXPECT_TRUE(manager.outgoing_active());
    auto second = manager.send_file(source.path() / "failure.bin", "127.0.0.1", refused_port, {});
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(second.error().code, coredesk::ErrorCode::Busy);

    ASSERT_TRUE(wait_until([&] {
        return !completions.empty();
    }));
    EXPECT_FALSE(completions[0].ok());
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_TRUE(manager.enabled());

    TcpTransferServer remote_receiver(QStringLiteral("RecoveryReceiver"));
    remote_receiver.set_receive_directory(receive.path());
    ASSERT_TRUE(remote_receiver.listen(0, QHostAddress::LocalHost).ok());
    auto retry = manager.send_file(source.path() / "failure.bin",
                                   "127.0.0.1",
                                   remote_receiver.server_port(),
                                   [&](auto result) { completions.push_back(std::move(result)); });
    ASSERT_TRUE(retry.ok());
    ASSERT_TRUE(wait_until([&] { return completions.size() == 2; }));
    EXPECT_TRUE(completions[1].ok());
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_TRUE(manager.enabled());
    EXPECT_EQ(read_file_bytes(receive.path() / "failure.bin"), bytes_from_string("failure path"));
    remote_receiver.close();
    manager.stop();
}

TEST(TransferManagerTest, TargetExistsReturnsToIdleAndAllowsAnotherFile)
{
    app();
    TempDirectory source("coredesk_manager_reject_source_");
    TempDirectory receive("coredesk_manager_reject_receive_");
    source.write_binary_file("duplicate.bin", bytes_from_string("new contents"));
    source.write_binary_file("next.bin", bytes_from_string("next transfer"));
    receive.write_binary_file("duplicate.bin", bytes_from_string("existing contents"));

    TcpTransferServer remote_receiver(QStringLiteral("RejectReceiver"));
    remote_receiver.set_receive_directory(receive.path());
    ASSERT_TRUE(remote_receiver.listen(0, QHostAddress::LocalHost).ok());

    TransferManager manager;
    std::vector<coredesk::Result<void>> completions;
    ASSERT_TRUE(manager.send_file(source.path() / "duplicate.bin",
                                  "127.0.0.1",
                                  remote_receiver.server_port(),
                                  [&](auto result) { completions.push_back(std::move(result)); })
                    .ok());
    ASSERT_TRUE(wait_until([&] { return !completions.empty(); }));
    ASSERT_FALSE(completions[0].ok());
    EXPECT_EQ(completions[0].error().code, coredesk::ErrorCode::TargetExists);
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_EQ(read_file_bytes(receive.path() / "duplicate.bin"), bytes_from_string("existing contents"));

    ASSERT_TRUE(manager.send_file(source.path() / "next.bin",
                                  "127.0.0.1",
                                  remote_receiver.server_port(),
                                  [&](auto result) { completions.push_back(std::move(result)); })
                    .ok());
    ASSERT_TRUE(wait_until([&] { return completions.size() == 2; }));
    EXPECT_TRUE(completions[1].ok());
    EXPECT_FALSE(manager.outgoing_active());
    EXPECT_EQ(read_file_bytes(receive.path() / "next.bin"), bytes_from_string("next transfer"));
    remote_receiver.close();
}

TEST(TransferManagerTest, RejectsInvalidOutgoingInputs)
{
    app();
    TempDirectory temp("coredesk_manager_invalid_");
    temp.write_binary_file("valid.bin", bytes_from_string("valid"));
    TransferManager manager;
    auto missing = manager.send_file(temp.path() / "missing.bin", "127.0.0.1", 45827, {});
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, coredesk::ErrorCode::PathNotFound);
    auto directory = manager.send_file(temp.path(), "127.0.0.1", 45827, {});
    ASSERT_FALSE(directory.ok());
    EXPECT_EQ(directory.error().code, coredesk::ErrorCode::InvalidArgument);
    auto empty_host = manager.send_file(temp.path() / "valid.bin", "", 45827, {});
    ASSERT_FALSE(empty_host.ok());
    EXPECT_EQ(empty_host.error().code, coredesk::ErrorCode::InvalidArgument);
    auto zero_port = manager.send_file(temp.path() / "valid.bin", "127.0.0.1", 0, {});
    ASSERT_FALSE(zero_port.ok());
    EXPECT_EQ(zero_port.error().code, coredesk::ErrorCode::InvalidArgument);
    EXPECT_FALSE(manager.outgoing_active());
}
