#pragma once

#include "coredesk/common/Result.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace coredesk {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

std::string_view to_string(LogLevel level) noexcept;

class Logger {
public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    Result<void> open(const std::filesystem::path& path);
    void close() noexcept;
    void log(LogLevel level, std::string_view component, std::string_view message) noexcept;
    bool is_open() const noexcept;

private:
    mutable std::mutex mutex_;
    std::ofstream stream_;
};

} // namespace coredesk
