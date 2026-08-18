#pragma once

#include "coredesk/common/Cancellation.h"
#include "coredesk/common/Result.h"
#include "coredesk/filesystem/FileRecord.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace coredesk::filesystem {

struct ScanOptions {
    bool include_dot_hidden{false};
    bool follow_directory_symlinks{false};
    std::size_t worker_count{0};
};

struct ScanProgress {
    std::uint64_t discovered{};
    std::uint64_t processed{};
    std::uint64_t skipped{};
    std::uint64_t failed{};
};

struct ScanOutput {
    std::filesystem::path root;
    std::vector<FileRecord> records;
    ScanProgress stats;
    std::chrono::milliseconds elapsed{};
};

using ProgressCallback = std::function<void(const ScanProgress&)>;

class FileScanner {
public:
    Result<ScanOutput> scan(const std::filesystem::path& root,
                            const ScanOptions& options,
                            CancellationToken token,
                            ProgressCallback progress);
};

} // namespace coredesk::filesystem
