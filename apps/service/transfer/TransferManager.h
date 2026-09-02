#pragma once

#ifdef COREDESK_BUILD_NETWORK

#include "TcpTransferServer.h"
#include "coredesk/common/Result.h"

#include <cstdint>
#include <filesystem>


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


private:

    std::filesystem::path receive_directory_;

    coredesk::qt_network::TcpTransferServer tcp_server_;
};


}

#endif
