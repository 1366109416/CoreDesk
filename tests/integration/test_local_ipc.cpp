#include "LocalIpcClient.h"
#include "LocalIpcServer.h"
#include "coredesk/protocol/FrameCodec.h"
#include "coredesk/protocol/JsonPayload.h"
#include "coredesk/service/ServiceController.h"

#ifdef COREDESK_BUILD_NETWORK
#include "TransferManager.h"
#endif

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using coredesk::ErrorCode;
using coredesk::protocol::Frame;
using coredesk::protocol::FrameDecoder;
using coredesk::protocol::FrameEncoder;
using coredesk::protocol::MessageType;
using coredesk::qt_ipc::LocalIpcClient;
using coredesk::qt_ipc::LocalIpcServer;
using coredesk::service::ServiceController;

#ifdef COREDESK_BUILD_NETWORK
using coredesk::service::TransferManager;
#endif

QCoreApplication& app()
{
    static int argc = 1;
    static char app_name[] = "coredesk_local_ipc_tests";
    static char* argv[] = {app_name, nullptr};
    static QCoreApplication application(argc, argv);
    return application;
}

std::string unique_server_name()
{
    return "CoreDesk.M4.Test." +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

class TempDirectory {
public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("coredesk_m4_ipc_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
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

private:
    std::filesystem::path path_;
};

std::vector<std::byte> text_bytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
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

Frame encode_then_decode_single(const std::vector<std::byte>& bytes)
{
    FrameDecoder decoder;
    auto decoded = decoder.push(bytes);
    EXPECT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().size(), 1U);
    return decoded.value()[0];
}

#ifdef COREDESK_BUILD_NETWORK
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

coredesk::qt_ipc::TransferManagementHandlers transfer_handlers_for(TransferManager& manager)
{
    return coredesk::qt_ipc::TransferManagementHandlers{
        [&manager]() -> coredesk::Result<coredesk::protocol::EnableLanTransferResponsePayload> {
            auto started = manager.start();
            if (!started.ok()) {
                return coredesk::Result<coredesk::protocol::EnableLanTransferResponsePayload>::failure(started.error());
            }
            return coredesk::Result<coredesk::protocol::EnableLanTransferResponsePayload>::success(
                {true, manager.listening_port()});
        },
        [&manager]() -> coredesk::Result<coredesk::protocol::DisableLanTransferResponsePayload> {
            manager.stop();
            return coredesk::Result<coredesk::protocol::DisableLanTransferResponsePayload>::success({true});
        },
        [&manager](
            const coredesk::protocol::SetReceiveDirectoryRequestPayload& payload)
            -> coredesk::Result<coredesk::protocol::SetReceiveDirectoryResponsePayload> {
            auto updated = manager.set_receive_directory(path_from_utf8_string(payload.path));
            if (!updated.ok()) {
                return coredesk::Result<coredesk::protocol::SetReceiveDirectoryResponsePayload>::failure(updated.error());
            }
            return coredesk::Result<coredesk::protocol::SetReceiveDirectoryResponsePayload>::success(
                {true, path_to_utf8_string(manager.receive_directory())});
        },
        [&manager]() -> coredesk::Result<coredesk::protocol::GetTransferStatusResponsePayload> {
            const auto status = manager.status();
            return coredesk::Result<coredesk::protocol::GetTransferStatusResponsePayload>::success(
                {status.enabled, status.port, path_to_utf8_string(status.receive_directory), status.active_transfers});
        }};
}

#endif

} // namespace

TEST(LocalIpcIntegrationTest, PingPongUsesFrameProtocolOverQLocalSocket)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    LocalIpcServer server(controller);
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto request_id = client.send_ping();
    ASSERT_TRUE(wait_until([&] {
        return !frames.empty();
    }));

    EXPECT_EQ(frames[0].type, MessageType::Pong);
    EXPECT_EQ(frames[0].request_id, request_id);

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, PingScanSearchFlowPassesEndToEnd)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);
    TempDirectory temp;
    temp.write_file("ipc_project_report.txt", "x");

    ServiceController controller;
    LocalIpcServer server(controller);
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto ping_id = client.send_ping();
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [&](const Frame& frame) {
            return frame.type == MessageType::Pong && frame.request_id == ping_id;
        });
    }));

    coredesk::protocol::ScanRequestPayload scan_request;
    const auto root_u8 = temp.path().u8string();
    scan_request.root.assign(reinterpret_cast<const char*>(root_u8.data()), root_u8.size());
    scan_request.include_dot_hidden = false;
    scan_request.follow_directory_symlinks = false;
    scan_request.worker_count = 1;
    const auto scan_id = client.send_scan_request(scan_request);
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [&](const Frame& frame) {
            return frame.type == MessageType::ScanAccepted && frame.request_id == scan_id;
        });
    }));
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [&](const Frame& frame) {
            return frame.type == MessageType::ScanCompleted && frame.request_id == scan_id;
        });
    }));

    const auto search_id = client.send_search_request(coredesk::protocol::SearchRequestPayload{"project", 100});
    ASSERT_TRUE(wait_until([&] {
        return std::any_of(frames.begin(), frames.end(), [&](const Frame& frame) {
            return frame.type == MessageType::SearchResponse && frame.request_id == search_id;
        });
    }));

    const auto search_frame = std::find_if(frames.begin(), frames.end(), [&](const Frame& frame) {
        return frame.type == MessageType::SearchResponse && frame.request_id == search_id;
    });
    ASSERT_NE(search_frame, frames.end());
    auto response = coredesk::protocol::decode_search_response_payload(search_frame->payload);
    ASSERT_TRUE(response.ok());
    ASSERT_EQ(response.value().results.size(), 1U);
    EXPECT_EQ(response.value().results[0].name, "ipc_project_report.txt");

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, SearchBeforeScanReturnsIndexNotReadyError)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    LocalIpcServer server(controller);
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto request_id = client.send_search_request(coredesk::protocol::SearchRequestPayload{"missing", 100});
    ASSERT_TRUE(wait_until([&] {
        return !frames.empty();
    }));

    EXPECT_EQ(frames[0].type, MessageType::SearchResponse);
    EXPECT_EQ(frames[0].request_id, request_id);
    auto error = coredesk::protocol::decode_error_response_payload(frames[0].payload);
    ASSERT_TRUE(error.ok());
    EXPECT_EQ(error.value().code, ErrorCode::IndexNotReady);

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, MultipleFramesInOneSocketWriteAreHandledInOrder)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    LocalIpcServer server(controller);
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto first = client.send_ping();
    const auto second = client.send_ping();
    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 2;
    }));

    EXPECT_EQ(frames[0].request_id, first);
    EXPECT_EQ(frames[1].request_id, second);
    EXPECT_EQ(frames[0].type, MessageType::Pong);
    EXPECT_EQ(frames[1].type, MessageType::Pong);

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, PartialRawFrameIsDecodedByServer)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    LocalIpcServer server(controller);
    ASSERT_TRUE(server.listen(name).ok());

    QLocalSocket socket;
    socket.connectToServer(name);
    ASSERT_TRUE(socket.waitForConnected(3000));

    auto encoded = FrameEncoder::encode(Frame{MessageType::Ping, 0, 42, {}});
    ASSERT_TRUE(encoded.ok());
    const auto first = QByteArray(reinterpret_cast<const char*>(encoded.value().data()), 7);
    const auto second = QByteArray(reinterpret_cast<const char*>(encoded.value().data() + 7),
                                   static_cast<qsizetype>(encoded.value().size() - 7));
    socket.write(first);
    socket.flush();
    QCoreApplication::processEvents();
    socket.write(second);
    socket.flush();

    ASSERT_TRUE(wait_until([&] {
        return socket.bytesAvailable() >= static_cast<qint64>(coredesk::protocol::kFrameHeaderSize);
    }));
    const auto raw_response = socket.readAll();
    const auto response_bytes = text_bytes(std::string_view(raw_response.constData(), static_cast<std::size_t>(raw_response.size())));
    const auto response = encode_then_decode_single(response_bytes);
    EXPECT_EQ(response.type, MessageType::Pong);
    EXPECT_EQ(response.request_id, 42U);

    socket.disconnectFromServer();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, MalformedProtocolClosesConnection)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    LocalIpcServer server(controller);
    ASSERT_TRUE(server.listen(name).ok());

    QLocalSocket socket;
    socket.connectToServer(name);
    ASSERT_TRUE(socket.waitForConnected(3000));

    auto encoded = FrameEncoder::encode(Frame{MessageType::Ping, 0, 42, {}});
    ASSERT_TRUE(encoded.ok());
    encoded.value()[0] = std::byte{'X'};
    socket.write(QByteArray(reinterpret_cast<const char*>(encoded.value().data()),
                            static_cast<qsizetype>(encoded.value().size())));
    socket.flush();

    ASSERT_TRUE(wait_until([&] {
        return socket.state() == QLocalSocket::UnconnectedState;
    }));

    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, ServerDestructionShutsDownActiveScanWithoutDanglingCallback)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);
    TempDirectory temp;
    temp.write_file("shutdown_scan.txt", "x");

    ServiceController controller;
    auto server = std::make_unique<LocalIpcServer>(controller);
    ASSERT_TRUE(server->listen(name).ok());

    std::promise<void> progress_started;
    std::promise<void> release_promise;
    auto release_future = release_promise.get_future().share();
    std::atomic_bool progress_reported{false};
    auto scan_payload = coredesk::protocol::ScanRequestPayload{};
    const auto root_u8 = temp.path().u8string();
    scan_payload.root.assign(reinterpret_cast<const char*>(root_u8.data()), root_u8.size());
    scan_payload.worker_count = 1;

    auto started = controller.start_scan(
        99,
        scan_payload,
        [&](auto, const auto&) {
            if (!progress_reported.exchange(true)) {
                progress_started.set_value();
            }
            release_future.wait();
        },
        {});
    ASSERT_TRUE(started.ok());
    ASSERT_EQ(progress_started.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);

    auto release_future_task = std::async(std::launch::async, [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        release_promise.set_value();
    });

    server.reset();
    ASSERT_EQ(release_future_task.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_NE(controller.status().state, coredesk::service::ServiceState::Scanning);

    QLocalServer::removeServer(name);
}

#ifdef COREDESK_BUILD_NETWORK

TEST(LocalIpcIntegrationTest, EnableLanTransferRequestEnablesManager)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto request_id = client.send_enable_lan_transfer_request();
    ASSERT_TRUE(wait_until([&] {
        return !frames.empty();
    }));

    ASSERT_EQ(frames[0].type, MessageType::EnableLanTransferResponse);
    EXPECT_EQ(frames[0].request_id, request_id);
    auto response = coredesk::protocol::decode_enable_lan_transfer_response_payload(frames[0].payload);
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response.value().success);
    EXPECT_EQ(response.value().port, manager.listening_port());
    EXPECT_GT(response.value().port, 0U);
    EXPECT_TRUE(manager.enabled());

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, RepeatedEnableIsIdempotent)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto first_id = client.send_enable_lan_transfer_request();
    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 1;
    }));
    const auto first_port = manager.listening_port();

    const auto second_id = client.send_enable_lan_transfer_request();
    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 2;
    }));

    EXPECT_EQ(frames[0].request_id, first_id);
    EXPECT_EQ(frames[1].request_id, second_id);
    EXPECT_EQ(frames[0].type, MessageType::EnableLanTransferResponse);
    EXPECT_EQ(frames[1].type, MessageType::EnableLanTransferResponse);
    auto first = coredesk::protocol::decode_enable_lan_transfer_response_payload(frames[0].payload);
    auto second = coredesk::protocol::decode_enable_lan_transfer_response_payload(frames[1].payload);
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.value().port, first_port);
    EXPECT_EQ(second.value().port, first_port);
    EXPECT_TRUE(manager.enabled());

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, DisableRepeatedDisableAndReenableAreIdempotent)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto enable_id = client.send_enable_lan_transfer_request();
    const auto disable_id = client.send_disable_lan_transfer_request();
    const auto second_disable_id = client.send_disable_lan_transfer_request();
    const auto reenable_id = client.send_enable_lan_transfer_request();

    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 4;
    }));

    EXPECT_EQ(frames[0].type, MessageType::EnableLanTransferResponse);
    EXPECT_EQ(frames[0].request_id, enable_id);
    EXPECT_EQ(frames[1].type, MessageType::DisableLanTransferResponse);
    EXPECT_EQ(frames[1].request_id, disable_id);
    EXPECT_EQ(frames[2].type, MessageType::DisableLanTransferResponse);
    EXPECT_EQ(frames[2].request_id, second_disable_id);
    EXPECT_EQ(frames[3].type, MessageType::EnableLanTransferResponse);
    EXPECT_EQ(frames[3].request_id, reenable_id);
    EXPECT_TRUE(manager.enabled());
    EXPECT_GT(manager.listening_port(), 0U);

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, GetTransferStatusReflectsManagerState)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto before_id = client.send_get_transfer_status_request();
    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 1;
    }));
    ASSERT_EQ(frames[0].type, MessageType::GetTransferStatusResponse);
    EXPECT_EQ(frames[0].request_id, before_id);
    auto before = coredesk::protocol::decode_get_transfer_status_response_payload(frames[0].payload);
    ASSERT_TRUE(before.ok());
    EXPECT_FALSE(before.value().enabled);
    EXPECT_EQ(before.value().port, 0U);
    EXPECT_EQ(before.value().receive_directory, path_to_utf8_string(manager.receive_directory()));
    EXPECT_EQ(before.value().active_transfers, 0U);

    const auto enable_id = client.send_enable_lan_transfer_request();
    const auto after_id = client.send_get_transfer_status_request();
    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 3;
    }));

    EXPECT_EQ(frames[1].request_id, enable_id);
    ASSERT_EQ(frames[2].type, MessageType::GetTransferStatusResponse);
    EXPECT_EQ(frames[2].request_id, after_id);
    auto after = coredesk::protocol::decode_get_transfer_status_response_payload(frames[2].payload);
    ASSERT_TRUE(after.ok());
    EXPECT_TRUE(after.value().enabled);
    EXPECT_EQ(after.value().port, manager.listening_port());
    EXPECT_EQ(after.value().receive_directory, path_to_utf8_string(manager.receive_directory()));
    EXPECT_EQ(after.value().active_transfers, manager.active_transfer_count());

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, SetReceiveDirectoryWhileDisabledUpdatesManager)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);
    TempDirectory temp;
    const auto receive_dir = temp.path() / "receiver";

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto request_id =
        client.send_set_receive_directory_request({path_to_utf8_string(receive_dir)});
    ASSERT_TRUE(wait_until([&] {
        return !frames.empty();
    }));

    ASSERT_EQ(frames[0].type, MessageType::SetReceiveDirectoryResponse);
    EXPECT_EQ(frames[0].request_id, request_id);
    auto response = coredesk::protocol::decode_set_receive_directory_response_payload(frames[0].payload);
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response.value().success);
    EXPECT_EQ(response.value().path, path_to_utf8_string(receive_dir));
    EXPECT_EQ(manager.receive_directory(), receive_dir);

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, SetReceiveDirectoryWhileEnabledReturnsBusy)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);
    TempDirectory temp;

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    client.send_enable_lan_transfer_request();
    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 1;
    }));

    const auto old_dir = manager.receive_directory();
    const auto request_id =
        client.send_set_receive_directory_request({path_to_utf8_string(temp.path() / "new_receiver")});
    ASSERT_TRUE(wait_until([&] {
        return frames.size() >= 2;
    }));

    ASSERT_EQ(frames[1].type, MessageType::SetReceiveDirectoryResponse);
    EXPECT_EQ(frames[1].request_id, request_id);
    auto error = coredesk::protocol::decode_error_response_payload(frames[1].payload);
    ASSERT_TRUE(error.ok());
    EXPECT_EQ(error.value().code, ErrorCode::Busy);
    EXPECT_EQ(manager.receive_directory(), old_dir);

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, MalformedSetReceiveDirectoryPayloadReturnsTypedErrorResponse)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const auto encoded = coredesk::protocol::encode_disable_lan_transfer_request_payload({});
    ASSERT_TRUE(encoded.ok());
    const coredesk::RequestId request_id = 4242;
    ASSERT_TRUE(client.send_frame(Frame{MessageType::SetReceiveDirectoryRequest, 0, request_id, encoded.value()}).ok());
    ASSERT_TRUE(wait_until([&] {
        return !frames.empty();
    }));

    ASSERT_EQ(frames[0].type, MessageType::SetReceiveDirectoryResponse);
    EXPECT_EQ(frames[0].request_id, request_id);
    auto error = coredesk::protocol::decode_error_response_payload(frames[0].payload);
    ASSERT_TRUE(error.ok());
    EXPECT_EQ(error.value().code, ErrorCode::InvalidArgument);

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, EmptyManagementPayloadWithWrongShapeReturnsErrorResponse)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    const coredesk::RequestId request_id = 5151;
    ASSERT_TRUE(client.send_frame(Frame{MessageType::EnableLanTransferRequest, 0, request_id, text_bytes("[]")}).ok());
    ASSERT_TRUE(wait_until([&] {
        return !frames.empty();
    }));

    ASSERT_EQ(frames[0].type, MessageType::EnableLanTransferResponse);
    EXPECT_EQ(frames[0].request_id, request_id);
    auto error = coredesk::protocol::decode_error_response_payload(frames[0].payload);
    ASSERT_TRUE(error.ok());
    EXPECT_EQ(error.value().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(manager.enabled());

    client.disconnect_from_server();
    server.close();
    QLocalServer::removeServer(name);
}

TEST(LocalIpcIntegrationTest, LocalIpcDisconnectDoesNotStopLanReceiver)
{
    app();
    const auto name = QString::fromStdString(unique_server_name());
    QLocalServer::removeServer(name);

    ServiceController controller;
    TransferManager manager;
    LocalIpcServer server(controller);
    server.set_transfer_management_handlers(transfer_handlers_for(manager));
    ASSERT_TRUE(server.listen(name).ok());

    LocalIpcClient client;
    std::vector<Frame> frames;
    client.set_frame_callback([&](const Frame& frame) {
        frames.push_back(frame);
    });
    ASSERT_TRUE(client.connect_to_server(name).ok());

    client.send_enable_lan_transfer_request();
    ASSERT_TRUE(wait_until([&] {
        return !frames.empty();
    }));
    ASSERT_TRUE(manager.enabled());

    client.disconnect_from_server();
    ASSERT_TRUE(wait_until([&] {
        return !client.is_connected();
    }));
    EXPECT_TRUE(manager.enabled());

    server.close();
    QLocalServer::removeServer(name);
}

#endif
