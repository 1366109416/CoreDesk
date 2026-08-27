#include "TransferManager.h"

#include <iostream>
#include <system_error>


namespace coredesk::service
{
namespace
{

std::filesystem::path default_receive_directory()
{
    return std::filesystem::temp_directory_path() / "CoreDeskReceived";
}

Error filesystem_error(std::error_code ec)
{
    if (ec == std::errc::permission_denied) {
        return {ErrorCode::PermissionDenied, ec.message()};
    }
    return {ErrorCode::IoError, ec.message()};
}

Result<void> ensure_receive_directory(const std::filesystem::path& path)
{
    if (path.empty()) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "receive directory must not be empty"});
    }

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return Result<void>::failure(filesystem_error(ec));
    }

    if (!std::filesystem::is_directory(path, ec)) {
        if (ec) {
            return Result<void>::failure(filesystem_error(ec));
        }
        return Result<void>::failure({ErrorCode::InvalidArgument, "receive path is not a directory"});
    }

    return Result<void>::success();
}

} // namespace


TransferManager::TransferManager()
    : receive_directory_(default_receive_directory())
{
    tcp_server_.set_receive_directory(receive_directory_);
}

TransferManager::~TransferManager()
{
    stop();
}


Result<void> TransferManager::start()
{

#ifdef COREDESK_BUILD_NETWORK

    if (enabled()) {
        return Result<void>::success();
    }

    auto ready = ensure_receive_directory(receive_directory_);
    if (!ready.ok()) {
        return ready;
    }

    tcp_server_.set_receive_directory(receive_directory_);
    auto result = tcp_server_.listen();
    if (!result.ok()) {
        return result;
    }

#endif


    return Result<void>::success();
}



void TransferManager::stop()
{
    tcp_server_.close();
}

Result<void> TransferManager::set_receive_directory(std::filesystem::path path)
{
    if (enabled()) {
        return Result<void>::failure({ErrorCode::Busy, "cannot change receive directory while LAN transfer is enabled"});
    }

    auto ready = ensure_receive_directory(path);
    if (!ready.ok()) {
        return ready;
    }

    receive_directory_ = std::move(path);
    tcp_server_.set_receive_directory(receive_directory_);
    return Result<void>::success();
}

bool TransferManager::enabled() const
{
    return tcp_server_.is_listening();
}

std::uint16_t TransferManager::listening_port() const
{
    return tcp_server_.server_port();
}

const std::filesystem::path& TransferManager::receive_directory() const
{
    return receive_directory_;
}

std::uint64_t TransferManager::active_transfer_count() const
{
    return tcp_server_.active_transfer_count();
}

TransferStatus TransferManager::status() const
{
    return TransferStatus{enabled(), listening_port(), receive_directory_, active_transfer_count()};
}


}
