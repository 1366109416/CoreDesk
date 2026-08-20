#include "coredesk/service/ServiceController.h"

#include <gtest/gtest.h>

#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>

namespace {

using coredesk::ErrorCode;
using coredesk::Result;
using coredesk::protocol::ScanCompletedPayload;
using coredesk::protocol::ScanRequestPayload;
using coredesk::protocol::SearchRequestPayload;
using coredesk::service::ServiceController;
using coredesk::service::ServiceState;

class TempDirectory {
public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("coredesk_m4_service_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
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

    void write_file(std::string_view name, std::string_view contents) const
    {
        std::ofstream file(path_ / std::filesystem::path(name));
        file << contents;
    }

private:
    std::filesystem::path path_;
};

ScanRequestPayload scan_request_for(const std::filesystem::path& root)
{
    const auto root_u8 = root.u8string();
    ScanRequestPayload payload;
    payload.root.assign(reinterpret_cast<const char*>(root_u8.data()), root_u8.size());
    payload.include_dot_hidden = false;
    payload.follow_directory_symlinks = false;
    payload.worker_count = 1;
    return payload;
}

Result<ScanCompletedPayload> wait_for_completion(std::future<Result<ScanCompletedPayload>>& future)
{
    EXPECT_EQ(future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    return future.get();
}

} // namespace

TEST(ServiceControllerTest, SearchBeforeIndexReadyReturnsIndexNotReady)
{
    ServiceController controller;
    auto result = controller.search(SearchRequestPayload{"report", 100});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::IndexNotReady);
}

TEST(ServiceControllerTest, ScanAcceptedAndSearchAfterSuccessfulScan)
{
    TempDirectory temp;
    temp.write_file("project_report_2026.txt", "x");

    ServiceController controller;
    std::promise<Result<ScanCompletedPayload>> completed_promise;
    auto completed_future = completed_promise.get_future();

    auto started = controller.start_scan(1, scan_request_for(temp.path()), {}, [&](auto, auto result) {
        completed_promise.set_value(std::move(result));
    });
    ASSERT_TRUE(started.ok());
    EXPECT_EQ(started.value().scan_id, "1");

    auto completed = wait_for_completion(completed_future);
    ASSERT_TRUE(completed.ok());
    EXPECT_EQ(completed.value().generation, 1U);
    EXPECT_EQ(controller.status().state, ServiceState::Ready);

    auto search = controller.search(SearchRequestPayload{"report", 100});
    ASSERT_TRUE(search.ok());
    EXPECT_EQ(search.value().generation, 1U);
    EXPECT_FALSE(search.value().stale);
    ASSERT_EQ(search.value().results.size(), 1U);
    EXPECT_EQ(search.value().results[0].name, "project_report_2026.txt");
}

TEST(ServiceControllerTest, SearchResultModifiedMsUsesSystemClockEpoch)
{
    TempDirectory temp;
    temp.write_file("dated_report.txt", "x");
    const auto file_path = temp.path() / "dated_report.txt";
    const auto before = std::chrono::system_clock::now() - std::chrono::minutes(1);
    std::filesystem::last_write_time(file_path, std::filesystem::file_time_type::clock::now());
    const auto after = std::chrono::system_clock::now() + std::chrono::minutes(1);

    ServiceController controller;
    std::promise<Result<ScanCompletedPayload>> completed_promise;
    auto completed_future = completed_promise.get_future();
    ASSERT_TRUE(controller.start_scan(1, scan_request_for(temp.path()), {}, [&](auto, auto result) {
        completed_promise.set_value(std::move(result));
    }).ok());
    ASSERT_TRUE(wait_for_completion(completed_future).ok());

    auto search = controller.search(SearchRequestPayload{"dated", 100});
    ASSERT_TRUE(search.ok());
    ASSERT_EQ(search.value().results.size(), 1U);

    const auto modified = std::chrono::system_clock::time_point{
        std::chrono::milliseconds(search.value().results[0].modified_ms)};
    EXPECT_GE(modified, before);
    EXPECT_LE(modified, after);
}

TEST(ServiceControllerTest, SecondScanWhileBusyReturnsBusy)
{
    TempDirectory temp;
    temp.write_file("busy.txt", "x");

    ServiceController controller;
    std::promise<void> progress_started;
    std::promise<void> release_promise;
    auto release_progress = release_promise.get_future().share();
    std::atomic_bool progress_reported{false};
    std::promise<Result<ScanCompletedPayload>> completed_promise;
    auto completed_future = completed_promise.get_future();

    auto started = controller.start_scan(
        1,
        scan_request_for(temp.path()),
        [&](auto, const auto&) {
            if (!progress_reported.exchange(true)) {
                progress_started.set_value();
            }
            release_progress.wait();
        },
        [&](auto, auto result) {
            completed_promise.set_value(std::move(result));
        });
    ASSERT_TRUE(started.ok());
    ASSERT_EQ(progress_started.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);

    auto second = controller.start_scan(2, scan_request_for(temp.path()), {}, {});
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(second.error().code, ErrorCode::Busy);

    release_promise.set_value();
    auto completed = wait_for_completion(completed_future);
    EXPECT_TRUE(completed.ok());
}

TEST(ServiceControllerTest, CancelActiveScanReturnsCancelledAndNoSnapshot)
{
    TempDirectory temp;
    temp.write_file("cancel.txt", "x");

    ServiceController controller;
    std::promise<void> progress_started;
    std::promise<void> release_promise;
    auto release_future = release_promise.get_future().share();
    std::atomic_bool progress_reported{false};
    std::promise<Result<ScanCompletedPayload>> completed_promise;
    auto completed_future = completed_promise.get_future();

    auto started = controller.start_scan(
        1,
        scan_request_for(temp.path()),
        [&](auto, const auto&) {
            if (!progress_reported.exchange(true)) {
                progress_started.set_value();
            }
            release_future.wait();
        },
        [&](auto, auto result) {
            completed_promise.set_value(std::move(result));
        });
    ASSERT_TRUE(started.ok());
    ASSERT_EQ(progress_started.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);

    auto cancelled = controller.cancel_scan();
    ASSERT_TRUE(cancelled.ok());
    release_promise.set_value();

    auto completed = wait_for_completion(completed_future);
    ASSERT_FALSE(completed.ok());
    EXPECT_EQ(completed.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(controller.status().state, ServiceState::NoIndex);
}

TEST(ServiceControllerTest, ProgressCallbackExceptionReturnsInternalErrorAndStateRecovers)
{
    TempDirectory temp;
    temp.write_file("throws.txt", "x");

    ServiceController controller;
    std::promise<Result<ScanCompletedPayload>> failed_promise;
    auto failed_future = failed_promise.get_future();

    auto started = controller.start_scan(
        1,
        scan_request_for(temp.path()),
        [](auto, const auto&) {
            throw std::runtime_error("progress callback failed");
        },
        [&](auto, auto result) {
            failed_promise.set_value(std::move(result));
        });
    ASSERT_TRUE(started.ok());

    auto failed = wait_for_completion(failed_future);
    ASSERT_FALSE(failed.ok());
    EXPECT_EQ(failed.error().code, ErrorCode::InternalError);
    EXPECT_EQ(controller.status().state, ServiceState::NoIndex);

    std::promise<Result<ScanCompletedPayload>> recovered_promise;
    auto recovered_future = recovered_promise.get_future();
    auto recovered = controller.start_scan(2, scan_request_for(temp.path()), {}, [&](auto, auto result) {
        recovered_promise.set_value(std::move(result));
    });
    ASSERT_TRUE(recovered.ok());
    EXPECT_TRUE(wait_for_completion(recovered_future).ok());
    EXPECT_EQ(controller.status().state, ServiceState::Ready);
}

TEST(ServiceControllerTest, FailedScanDoesNotReplaceCurrentSnapshot)
{
    TempDirectory temp;
    temp.write_file("stable_report.txt", "x");

    ServiceController controller;
    std::promise<Result<ScanCompletedPayload>> first_promise;
    auto first_future = first_promise.get_future();
    ASSERT_TRUE(controller.start_scan(1, scan_request_for(temp.path()), {}, [&](auto, auto result) {
        first_promise.set_value(std::move(result));
    }).ok());
    ASSERT_TRUE(wait_for_completion(first_future).ok());

    std::promise<Result<ScanCompletedPayload>> second_promise;
    auto second_future = second_promise.get_future();
    const auto missing = temp.path() / "missing";
    auto started = controller.start_scan(2, scan_request_for(missing), {}, [&](auto, auto result) {
        second_promise.set_value(std::move(result));
    });
    ASSERT_TRUE(started.ok());

    auto failed = wait_for_completion(second_future);
    ASSERT_FALSE(failed.ok());
    EXPECT_EQ(controller.status().state, ServiceState::Ready);

    auto search = controller.search(SearchRequestPayload{"stable", 100});
    ASSERT_TRUE(search.ok());
    EXPECT_EQ(search.value().generation, 1U);
    ASSERT_EQ(search.value().results.size(), 1U);
}

TEST(ServiceControllerTest, CancelledNewScanDoesNotReplaceCurrentSnapshot)
{
    TempDirectory temp;
    temp.write_file("stable_report.txt", "x");

    ServiceController controller;
    std::promise<Result<ScanCompletedPayload>> first_promise;
    auto first_future = first_promise.get_future();
    ASSERT_TRUE(controller.start_scan(1, scan_request_for(temp.path()), {}, [&](auto, auto result) {
        first_promise.set_value(std::move(result));
    }).ok());
    ASSERT_TRUE(wait_for_completion(first_future).ok());

    temp.write_file("new_report.txt", "x");
    std::promise<void> progress_started;
    std::promise<void> release_promise;
    auto release_future = release_promise.get_future().share();
    std::atomic_bool progress_reported{false};
    std::promise<Result<ScanCompletedPayload>> second_promise;
    auto second_future = second_promise.get_future();
    ASSERT_TRUE(controller.start_scan(
        2,
        scan_request_for(temp.path()),
        [&](auto, const auto&) {
            if (!progress_reported.exchange(true)) {
                progress_started.set_value();
            }
            release_future.wait();
        },
        [&](auto, auto result) {
            second_promise.set_value(std::move(result));
        }).ok());
    ASSERT_EQ(progress_started.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);

    ASSERT_TRUE(controller.cancel_scan().ok());
    release_promise.set_value();

    auto cancelled = wait_for_completion(second_future);
    ASSERT_FALSE(cancelled.ok());
    EXPECT_EQ(cancelled.error().code, ErrorCode::Cancelled);

    auto search = controller.search(SearchRequestPayload{"stable", 100});
    ASSERT_TRUE(search.ok());
    EXPECT_EQ(search.value().generation, 1U);
    ASSERT_EQ(search.value().results.size(), 1U);
    EXPECT_EQ(search.value().results[0].name, "stable_report.txt");

    auto new_search = controller.search(SearchRequestPayload{"new", 100});
    ASSERT_TRUE(new_search.ok());
    EXPECT_TRUE(new_search.value().results.empty());
}

TEST(ServiceControllerTest, SearchDuringScanUsesOldSnapshotAsStale)
{
    TempDirectory temp;
    temp.write_file("old_report.txt", "x");

    ServiceController controller;
    std::promise<Result<ScanCompletedPayload>> first_promise;
    auto first_future = first_promise.get_future();
    ASSERT_TRUE(controller.start_scan(1, scan_request_for(temp.path()), {}, [&](auto, auto result) {
        first_promise.set_value(std::move(result));
    }).ok());
    ASSERT_TRUE(wait_for_completion(first_future).ok());

    temp.write_file("new_report.txt", "x");
    std::promise<void> progress_started;
    std::promise<void> release_promise;
    auto release_future = release_promise.get_future().share();
    std::atomic_bool progress_reported{false};
    std::promise<Result<ScanCompletedPayload>> second_promise;
    auto second_future = second_promise.get_future();
    ASSERT_TRUE(controller.start_scan(
        2,
        scan_request_for(temp.path()),
        [&](auto, const auto&) {
            if (!progress_reported.exchange(true)) {
                progress_started.set_value();
            }
            release_future.wait();
        },
        [&](auto, auto result) {
            second_promise.set_value(std::move(result));
        }).ok());
    ASSERT_EQ(progress_started.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);

    auto search = controller.search(SearchRequestPayload{"old", 100});
    ASSERT_TRUE(search.ok());
    EXPECT_TRUE(search.value().stale);
    EXPECT_EQ(search.value().generation, 1U);
    ASSERT_EQ(search.value().results.size(), 1U);

    release_promise.set_value();
    ASSERT_TRUE(wait_for_completion(second_future).ok());
}
