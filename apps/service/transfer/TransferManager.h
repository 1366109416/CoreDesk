#pragma once

#ifdef COREDESK_BUILD_NETWORK

#include "TcpTransferServer.h"
#include "TcpTransferClient.h"
#include "coredesk/common/Result.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>


namespace coredesk::service
{

struct TransferStatus
{
    bool enabled{false};
    std::uint16_t port{};
    std::filesystem::path receive_directory;
    std::uint64_t active_transfers{};
};

class TransferManager
{
public:
    using OutgoingCompletion = std::function<void(Result<void>)>;

    TransferManager();

    ~TransferManager();

    void set_logger(Logger* logger) noexcept;

    Result<void> start();

    void stop();

    Result<void> set_receive_directory(std::filesystem::path path);

    bool enabled() const;

    std::uint16_t listening_port() const;

    const std::filesystem::path& receive_directory() const;

    std::uint64_t active_transfer_count() const;

    TransferStatus status() const;

    Result<void> send_file(std::filesystem::path file_path,
                           std::string host,
                           std::uint16_t port,
                           OutgoingCompletion completion);

    bool outgoing_active() const noexcept;


private:
    void finish_outgoing(Result<void> result);

    std::filesystem::path receive_directory_;

    coredesk::qt_network::TcpTransferServer tcp_server_;
    std::unique_ptr<coredesk::qt_network::TcpTransferClient> tcp_client_;
    std::filesystem::path outgoing_file_path_;
    OutgoingCompletion outgoing_completion_;
    Logger* logger_{};
    bool outgoing_active_{false};
};


}

#endif
