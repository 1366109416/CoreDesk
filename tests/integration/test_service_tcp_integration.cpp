#include "LocalIpcServer.h"
#include "TcpTransferClient.h"
#include "TcpTransferServer.h"
#include "coredesk/protocol/JsonPayload.h"
#include "coredesk/service/ServiceController.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QHostAddress>
#include <QTimer>

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
using coredesk::qt_network::TcpTransferClient;
using coredesk::qt_network::TcpTransferServer;
using coredesk::service::ServiceController;

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
