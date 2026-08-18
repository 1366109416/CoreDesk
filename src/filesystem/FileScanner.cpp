#include "coredesk/filesystem/FileScanner.h"

#include "coredesk/concurrency/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>

namespace coredesk::filesystem {
namespace {

std::size_t default_worker_count()
{
    const auto hardware = std::thread::hardware_concurrency();
    return std::max<std::size_t>(2, hardware == 0 ? 2 : hardware);
}

bool is_dot_hidden(const std::filesystem::path& path)
{
    const auto name = path.filename().native();
    return !name.empty() && name[0] == '.';
}

EntryType entry_type_from_status(const std::filesystem::file_status& status)
{
    if (std::filesystem::is_symlink(status)) {
        return EntryType::Symlink;
    }
    if (std::filesystem::is_regular_file(status)) {
        return EntryType::RegularFile;
    }
    if (std::filesystem::is_directory(status)) {
        return EntryType::Directory;
    }
    return EntryType::Other;
}

Error error_from_code(ErrorCode code, const std::filesystem::path& path, const std::error_code& ec)
{
    std::string message = "filesystem error";
    if (ec) {
        message += ": ";
        message += ec.message();
    }
    if (!path.empty()) {
        message += " at path";
    }
    return {code, message};
}

std::filesystem::path directory_identity(const std::filesystem::path& path, std::error_code& ec)
{
    auto identity = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return identity;
    }

    ec.clear();
    identity = std::filesystem::absolute(path, ec);
    if (ec) {
        return {};
    }
    return identity.lexically_normal();
}

} // namespace

Result<ScanOutput> FileScanner::scan(const std::filesystem::path& root,
                                     const ScanOptions& options,
                                     CancellationToken token,
                                     ProgressCallback progress)
{
    const auto started = std::chrono::steady_clock::now();

    if (token.is_cancelled()) {
        return Result<ScanOutput>::failure({ErrorCode::Cancelled, "scan cancelled"});
    }

    std::error_code ec;
    const bool exists = std::filesystem::exists(root, ec);
    if (ec) {
        return Result<ScanOutput>::failure(error_from_code(ErrorCode::IoError, root, ec));
    }
    if (!exists) {
        return Result<ScanOutput>::failure({ErrorCode::PathNotFound, "scan root does not exist"});
    }

    const bool is_directory = std::filesystem::is_directory(root, ec);
    if (ec) {
        return Result<ScanOutput>::failure(error_from_code(ErrorCode::IoError, root, ec));
    }
    if (!is_directory) {
        return Result<ScanOutput>::failure({ErrorCode::InvalidArgument, "scan root is not a directory"});
    }

    std::filesystem::path normalized_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        ec.clear();
        normalized_root = std::filesystem::absolute(root, ec);
        if (ec) {
            return Result<ScanOutput>::failure(error_from_code(ErrorCode::IoError, root, ec));
        }
        normalized_root = normalized_root.lexically_normal();
    }

    ScanOutput output;
    output.root = normalized_root;

    std::mutex output_mutex;
    std::mutex progress_mutex;
    constexpr auto progress_interval = std::chrono::milliseconds(250);
    auto last_progress_time = started - progress_interval;
    FileId next_id = 1;
    auto report_progress = [&](bool force) {
        if (!progress) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(progress_mutex);
            if (!force && now - last_progress_time < progress_interval) {
                return;
            }
            last_progress_time = now;
        }

        ScanProgress snapshot;
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            snapshot = output.stats;
        }
        progress(snapshot);
    };

    const auto worker_count = options.worker_count == 0 ? default_worker_count() : options.worker_count;
    concurrency::ThreadPool pool(worker_count);

    std::filesystem::directory_options directory_options = std::filesystem::directory_options::skip_permission_denied;
    if (options.follow_directory_symlinks) {
        directory_options |= std::filesystem::directory_options::follow_directory_symlink;
    }

    std::set<std::filesystem::path> visited_directories;
    if (options.follow_directory_symlinks) {
        std::error_code identity_ec;
        auto root_identity = directory_identity(normalized_root, identity_ec);
        if (!identity_ec && !root_identity.empty()) {
            visited_directories.insert(std::move(root_identity));
        }
    }

    std::filesystem::recursive_directory_iterator it(normalized_root, directory_options, ec);
    if (ec) {
        return Result<ScanOutput>::failure(error_from_code(ErrorCode::PermissionDenied, normalized_root, ec));
    }

    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        if (token.is_cancelled()) {
            pool.shutdown();
            return Result<ScanOutput>::failure({ErrorCode::Cancelled, "scan cancelled"});
        }

        const auto entry_path = it->path();
        if (options.follow_directory_symlinks) {
            std::error_code directory_ec;
            if (it->is_directory(directory_ec)) {
                std::error_code identity_ec;
                auto identity = directory_identity(entry_path, identity_ec);
                if (identity_ec || identity.empty()) {
                    it.disable_recursion_pending();
                    {
                        std::lock_guard<std::mutex> lock(output_mutex);
                        ++output.stats.failed;
                    }
                    report_progress(false);
                } else if (!visited_directories.insert(std::move(identity)).second) {
                    it.disable_recursion_pending();
                }
            } else if (directory_ec) {
                it.disable_recursion_pending();
                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    ++output.stats.failed;
                }
                report_progress(false);
            }
        }

        if (!options.include_dot_hidden && is_dot_hidden(entry_path)) {
            std::error_code hidden_ec;
            if (it->is_directory(hidden_ec)) {
                it.disable_recursion_pending();
            }
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                ++output.stats.skipped;
            }
            report_progress(false);
        } else {
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                ++output.stats.discovered;
            }

            const bool submitted = pool.submit([&, entry_path] {
                if (token.is_cancelled()) {
                    return;
                }

                std::error_code metadata_ec;
                const auto status = std::filesystem::symlink_status(entry_path, metadata_ec);
                if (metadata_ec) {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    ++output.stats.failed;
                    return;
                }

                FileRecord record;
                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    record.id = next_id++;
                }

                record.absolute_path = std::filesystem::absolute(entry_path, metadata_ec);
                if (metadata_ec) {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    ++output.stats.failed;
                    return;
                }
                record.absolute_path = record.absolute_path.lexically_normal();

                record.relative_path = record.absolute_path.lexically_relative(normalized_root);
                if (record.relative_path.empty()) {
                    record.relative_path = entry_path.filename();
                }
                record.file_name = entry_path.filename();
                record.extension = entry_path.extension();
                record.type = entry_type_from_status(status);

                if (record.type == EntryType::RegularFile) {
                    record.size_bytes = std::filesystem::file_size(entry_path, metadata_ec);
                    if (metadata_ec) {
                        std::lock_guard<std::mutex> lock(output_mutex);
                        ++output.stats.failed;
                        return;
                    }
                }

                record.modified_time = std::filesystem::last_write_time(entry_path, metadata_ec);
                if (metadata_ec) {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    ++output.stats.failed;
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    output.records.push_back(std::move(record));
                    ++output.stats.processed;
                }
            });

            if (!submitted) {
                pool.shutdown();
                return Result<ScanOutput>::failure({ErrorCode::InternalError, "scanner task submission failed"});
            }

            report_progress(false);
        }

        it.increment(ec);
        if (ec) {
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                ++output.stats.failed;
            }
            report_progress(false);
            ec.clear();
        }
    }

    pool.wait_idle();

    if (token.is_cancelled()) {
        return Result<ScanOutput>::failure({ErrorCode::Cancelled, "scan cancelled"});
    }

    output.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    report_progress(true);
    return Result<ScanOutput>::success(std::move(output));
}

} // namespace coredesk::filesystem
