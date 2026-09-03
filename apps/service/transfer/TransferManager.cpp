#include "TransferManager.h"

#include <iostream>
#include <fstream>
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

void TransferManager::set_logger(Logger* logger) noexcept
{
    logger_ = logger;
    tcp_server_.set_logger(logger);
    if (tcp_client_) {
        tcp_client_->set_logger(logger);
    }
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
    if (outgoing_active_) {
        finish_outgoing(Result<void>::failure({ErrorCode::Cancelled, "LAN transfer stopped"}));
    }
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

Result<void> TransferManager::send_file(std::filesystem::path file_path,
                                        std::string host,
                                        std::uint16_t port,
                                        OutgoingCompletion completion)
{
    if (outgoing_active_) {
        return Result<void>::failure({ErrorCode::Busy, "an outgoing transfer is already active"});
    }
    if (file_path.empty()) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "file path must not be empty"});
    }
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec)) {
        return Result<void>::failure(ec ? filesystem_error(ec) : Error{ErrorCode::PathNotFound, "file does not exist"});
    }
    if (!std::filesystem::is_regular_file(file_path, ec)) {
        return Result<void>::failure(ec ? filesystem_error(ec) : Error{ErrorCode::InvalidArgument, "path is not a regular file"});
    }
    std::ifstream readable(file_path, std::ios::binary);
    if (!readable.is_open()) {
        return Result<void>::failure({ErrorCode::PermissionDenied, "file is not readable"});
    }
    if (host.empty()) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "host must not be empty"});
    }
    if (port == 0) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "port must be in range 1..65535"});
    }

    tcp_client_ = std::make_unique<coredesk::qt_network::TcpTransferClient>(QStringLiteral("CoreDeskService"));
    tcp_client_->set_logger(logger_);
    outgoing_active_ = true;
    outgoing_file_path_ = std::move(file_path);
    outgoing_completion_ = std::move(completion);

    tcp_client_->set_handshake_callback([this]() {
#ifdef _WIN32
        const auto path = QString::fromStdWString(outgoing_file_path_.wstring());
#else
        const auto path = QString::fromStdString(outgoing_file_path_.string());
#endif
        auto started = tcp_client_->send_file(path);
        if (!started.ok()) {
            finish_outgoing(Result<void>::failure(started.error()));
        }
    });
    tcp_client_->set_file_reject_callback([this](RequestId, const protocol::FileRejectPayload& reject) {
        finish_outgoing(Result<void>::failure({reject.code, reject.message}));
    });
    tcp_client_->set_file_result_callback([this](RequestId, const protocol::FileResultPayload& result) {
        if (result.ok) {
            finish_outgoing(Result<void>::success());
        } else {
            finish_outgoing(Result<void>::failure({result.code, result.message}));
        }
    });
    tcp_client_->set_error_callback([this](const Error& error) {
        finish_outgoing(Result<void>::failure(error));
    });
    tcp_client_->set_disconnected_callback([this]() {
        if (outgoing_active_) {
            finish_outgoing(Result<void>::failure({ErrorCode::ConnectionFailed, "connection closed before transfer completed"}));
        }
    });

    if (logger_) {
        logger_->log(LogLevel::Info,
                     "network",
                     "outgoing request accepted target=" + host + ":" + std::to_string(port) +
                         " file=" + outgoing_file_path_.filename().string());
    }
    tcp_client_->connect_to_host(QString::fromUtf8(host.data(), static_cast<qsizetype>(host.size())), port);
    return Result<void>::success();
}

bool TransferManager::outgoing_active() const noexcept
{
    return outgoing_active_;
}

void TransferManager::finish_outgoing(Result<void> result)
{
    if (!outgoing_active_) {
        return;
    }
    outgoing_active_ = false;
    auto completion = std::move(outgoing_completion_);
    outgoing_completion_ = {};
    outgoing_file_path_.clear();
    if (tcp_client_) {
        tcp_client_->disconnect_from_host();
    }
    if (logger_) {
        if (result.ok()) {
            logger_->log(LogLevel::Info, "network", "outgoing transfer success error_code=Ok");
        } else {
            logger_->log(LogLevel::Error,
                         "network",
                         "outgoing transfer fail error_code=" + std::string(to_string(result.error().code)) +
                             " message=" + result.error().message);
        }
    }
    if (completion) {
        completion(std::move(result));
    }
}


}
