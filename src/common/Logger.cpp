#include "coredesk/common/Logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace coredesk {
namespace {

std::uint64_t process_id() noexcept
{
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
           << milliseconds.count() << 'Z';
    return output.str();
}

Error filesystem_error(const std::error_code& error, std::string_view context)
{
    const auto code = error == std::errc::permission_denied ? ErrorCode::PermissionDenied : ErrorCode::IoError;
    return {code, std::string(context) + ": " + error.message()};
}

std::string single_line(std::string_view text)
{
    std::string output(text);
    for (auto& ch : output) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return output;
}

} // namespace

std::string_view to_string(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

Logger::~Logger()
{
    close();
}

Result<void> Logger::open(const std::filesystem::path& path)
{
    if (path.empty() || path.filename().empty()) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "log file path must include a file name"});
    }

    std::lock_guard lock(mutex_);
    stream_.close();
    stream_.clear();

    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            return Result<void>::failure(filesystem_error(error, "failed to create log directory"));
        }
    }

    stream_.open(path, std::ios::out | std::ios::app);
    if (!stream_.is_open()) {
        stream_.clear();
        return Result<void>::failure({ErrorCode::IoError, "failed to open log file"});
    }
    return Result<void>::success();
}

void Logger::close() noexcept
{
    try {
        std::lock_guard lock(mutex_);
        if (stream_.is_open()) {
            stream_.flush();
            stream_.close();
        }
    } catch (...) {
    }
}

void Logger::log(LogLevel level, std::string_view component, std::string_view message) noexcept
{
    try {
        std::lock_guard lock(mutex_);
        if (!stream_.is_open()) {
            return;
        }
        stream_ << timestamp() << " | " << to_string(level) << " | pid=" << process_id() << " | tid="
                << std::this_thread::get_id() << " | " << single_line(component) << " | " << single_line(message)
                << '\n';
        stream_.flush();
    } catch (...) {
    }
}

bool Logger::is_open() const noexcept
{
    try {
        std::lock_guard lock(mutex_);
        return stream_.is_open();
    } catch (...) {
        return false;
    }
}

} // namespace coredesk
