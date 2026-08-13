#include "coredesk/common/Cancellation.h"
#include "coredesk/filesystem/FileScanner.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace coredesk::filesystem {
namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        std::error_code ec;
        path_ = std::filesystem::temp_directory_path(ec) /
                std::filesystem::path("coredesk_m1_scanner_test_" + std::to_string(counter_.fetch_add(1)));
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
    }

    ~TemporaryDirectory()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
    inline static std::atomic<int> counter_{0};
};

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream file(path, std::ios::binary);
    file << content;
}

bool has_relative_path(const ScanOutput& output, const std::filesystem::path& relative)
{
    return std::any_of(output.records.begin(), output.records.end(), [&](const FileRecord& record) {
        return record.relative_path == relative;
    });
}

TEST(FileScannerTest, ScansFilesAndDirectories)
{
    TemporaryDirectory temp;
    std::filesystem::create_directories(temp.path() / "subdir");
    write_file(temp.path() / "alpha.txt", "abc");
    write_file(temp.path() / "subdir" / "beta.bin", "12345");

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), {});

    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& output = result.value();
    EXPECT_EQ(output.stats.failed, 0);
    EXPECT_TRUE(has_relative_path(output, "alpha.txt"));
    EXPECT_TRUE(has_relative_path(output, std::filesystem::path("subdir") / "beta.bin"));
    EXPECT_TRUE(has_relative_path(output, "subdir"));
}

TEST(FileScannerTest, SkipsDotHiddenByDefault)
{
    TemporaryDirectory temp;
    write_file(temp.path() / ".hidden", "hidden");
    write_file(temp.path() / "visible", "visible");

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), {});

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(has_relative_path(result.value(), ".hidden"));
    EXPECT_TRUE(has_relative_path(result.value(), "visible"));
    EXPECT_EQ(result.value().stats.skipped, 1);
}

TEST(FileScannerTest, SkipsDotHiddenDirectoriesByDefault)
{
    TemporaryDirectory temp;
    std::filesystem::create_directories(temp.path() / ".hidden_dir");
    write_file(temp.path() / ".hidden_dir" / "inside.txt", "hidden");
    write_file(temp.path() / "visible.txt", "visible");

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), {});

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(has_relative_path(result.value(), ".hidden_dir"));
    EXPECT_FALSE(has_relative_path(result.value(), std::filesystem::path(".hidden_dir") / "inside.txt"));
    EXPECT_TRUE(has_relative_path(result.value(), "visible.txt"));
    EXPECT_EQ(result.value().stats.skipped, 1);
}

TEST(FileScannerTest, CanIncludeDotHidden)
{
    TemporaryDirectory temp;
    write_file(temp.path() / ".hidden", "hidden");

    ScanOptions options;
    options.include_dot_hidden = true;

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(temp.path(), options, cancellation.token(), {});

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(has_relative_path(result.value(), ".hidden"));
}

TEST(FileScannerTest, MissingRootReturnsPathNotFound)
{
    TemporaryDirectory temp;
    const auto missing = temp.path() / "missing";

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(missing, {}, cancellation.token(), {});

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::PathNotFound);
}

TEST(FileScannerTest, FileRootReturnsInvalidArgument)
{
    TemporaryDirectory temp;
    const auto file = temp.path() / "file.txt";
    write_file(file, "abc");

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(file, {}, cancellation.token(), {});

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(FileScannerTest, CancelledBeforeStartReturnsCancelled)
{
    TemporaryDirectory temp;
    write_file(temp.path() / "file.txt", "abc");
    CancellationSource cancellation;
    cancellation.cancel();

    FileScanner scanner;
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), {});

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST(FileScannerTest, CancellationDuringScanReturnsCancelled)
{
    TemporaryDirectory temp;
    for (int i = 0; i < 200; ++i) {
        write_file(temp.path() / ("file_" + std::to_string(i) + ".txt"), "abc");
    }

    FileScanner scanner;
    CancellationSource cancellation;
    std::atomic<bool> cancellation_requested{false};
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), [&](const ScanProgress& progress) {
        if (progress.discovered > 0 && !cancellation_requested.exchange(true)) {
            cancellation.cancel();
        }
    });

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
    EXPECT_TRUE(cancellation_requested.load());
}

TEST(FileScannerTest, ReportsFileSizeAndType)
{
    TemporaryDirectory temp;
    const auto file = temp.path() / "sized.txt";
    write_file(file, "1234567");

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), {});

    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& records = result.value().records;
    const auto it = std::find_if(records.begin(), records.end(), [](const FileRecord& record) {
        return record.relative_path == "sized.txt";
    });
    ASSERT_NE(it, records.end());
    EXPECT_EQ(it->type, EntryType::RegularFile);
    EXPECT_EQ(it->size_bytes, 7U);
}

TEST(FileScannerTest, ProgressCallbackIsThrottledAndReportsFinalState)
{
    TemporaryDirectory temp;
    for (int i = 0; i < 64; ++i) {
        write_file(temp.path() / ("file_" + std::to_string(i) + ".txt"), "abc");
    }

    FileScanner scanner;
    CancellationSource cancellation;
    std::mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> callback_times;
    std::vector<ScanProgress> snapshots;

    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), [&](const ScanProgress& progress) {
        std::lock_guard<std::mutex> lock(mutex);
        callback_times.push_back(std::chrono::steady_clock::now());
        snapshots.push_back(progress);
        if (callback_times.size() == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(275));
        }
    });

    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_FALSE(snapshots.empty());
    const auto final_progress = snapshots.back();
    EXPECT_EQ(final_progress.discovered, result.value().stats.discovered);
    EXPECT_EQ(final_progress.processed, result.value().stats.processed);
    EXPECT_EQ(final_progress.skipped, result.value().stats.skipped);
    EXPECT_EQ(final_progress.failed, result.value().stats.failed);

    for (std::size_t i = 1; i + 1 < callback_times.size(); ++i) {
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
            callback_times[i] - callback_times[i - 1]);
        EXPECT_GE(delta.count(), 240);
    }
}

TEST(FileScannerTest, ContinuesWhenDiscoveredEntryIsDeletedBeforeMetadata)
{
    TemporaryDirectory temp;
    write_file(temp.path() / "stable.txt", "stable");
    for (int i = 0; i < 128; ++i) {
        write_file(temp.path() / ("volatile_" + std::to_string(i) + ".txt"), "volatile");
    }

    FileScanner scanner;
    CancellationSource cancellation;
    std::atomic<bool> deleted{false};
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), [&](const ScanProgress& progress) {
        if (progress.discovered > 0 && !deleted.exchange(true)) {
            std::error_code ec;
            for (int i = 0; i < 128; ++i) {
                std::filesystem::remove(temp.path() / ("volatile_" + std::to_string(i) + ".txt"), ec);
                ec.clear();
            }
        }
    });

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(has_relative_path(result.value(), "stable.txt"));
    if (result.value().stats.failed == 0) {
        GTEST_SKIP() << "metadata deletion race was not observed on this run";
    }
    EXPECT_GT(result.value().stats.failed, 0);
}

TEST(FileScannerTest, SymlinkDoesNotForceRecursiveFollow)
{
    TemporaryDirectory temp;
    std::filesystem::create_directories(temp.path() / "target");
    write_file(temp.path() / "target" / "inside.txt", "abc");

    std::error_code ec;
    std::filesystem::create_directory_symlink(temp.path() / "target", temp.path() / "link_to_target", ec);
    if (ec) {
        GTEST_SKIP() << "directory symlink is not supported in this environment";
    }

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(temp.path(), {}, cancellation.token(), {});

    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto link_count = std::count_if(result.value().records.begin(), result.value().records.end(),
                                          [](const FileRecord& record) {
                                              return record.relative_path == "link_to_target";
                                          });
    EXPECT_EQ(link_count, 1);
    EXPECT_TRUE(has_relative_path(result.value(), std::filesystem::path("target") / "inside.txt"));
    EXPECT_FALSE(has_relative_path(result.value(), std::filesystem::path("link_to_target") / "inside.txt"));
}

TEST(FileScannerTest, FollowDirectorySymlinksAvoidsCycles)
{
    TemporaryDirectory temp;
    std::filesystem::create_directories(temp.path() / "target");
    write_file(temp.path() / "target" / "inside.txt", "abc");

    std::error_code ec;
    std::filesystem::create_directory_symlink(temp.path(), temp.path() / "target" / "loop_to_root", ec);
    if (ec) {
        GTEST_SKIP() << "directory symlink is not supported in this environment";
    }

    ScanOptions options;
    options.follow_directory_symlinks = true;

    FileScanner scanner;
    CancellationSource cancellation;
    const auto result = scanner.scan(temp.path(), options, cancellation.token(), {});

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(has_relative_path(result.value(), std::filesystem::path("target") / "loop_to_root"));
    EXPECT_LT(result.value().records.size(), 16U);
    EXPECT_FALSE(has_relative_path(result.value(),
                                   std::filesystem::path("target") / "loop_to_root" / "target" / "inside.txt"));
}

} // namespace
} // namespace coredesk::filesystem
