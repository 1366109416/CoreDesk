#include "coredesk/service/ServiceController.h"

#include "coredesk/common/Error.h"
#include "coredesk/index/IndexBuilder.h"
#include "coredesk/index/Tokenizer.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace coredesk::service {
namespace {

Error make_error(ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

std::int64_t file_time_to_ms(const std::filesystem::file_time_type& time)
{
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch()).count();
}

std::string entry_type_to_protocol(EntryType type)
{
    switch (type) {
    case EntryType::RegularFile:
        return "file";
    case EntryType::Directory:
        return "directory";
    case EntryType::Symlink:
        return "symlink";
    case EntryType::Other:
        return "other";
    }
    return "other";
}

} // namespace

ServiceController::ServiceController() = default;

ServiceController::~ServiceController()
{
    shutdown();
}

Result<ScanStarted> ServiceController::start_scan(RequestId request_id,
                                                  const protocol::ScanRequestPayload& payload,
                                                  ProgressCallback progress,
                                                  CompletionCallback completed)
{
    join_finished_scan();

    CancellationSource cancellation;
    {
        std::lock_guard lock(state_mutex_);
        if (shutdown_requested_) {
            return Result<ScanStarted>::failure(make_error(ErrorCode::Cancelled, "service is shutting down"));
        }
        if (state_ == ServiceState::Scanning) {
            return Result<ScanStarted>::failure(make_error(ErrorCode::Busy, "scan already in progress"));
        }
        state_ = ServiceState::Scanning;
        active_scan_cancel_ = cancellation;
        active_scan_request_id_ = request_id;
    }

    const auto scan_id = std::to_string(request_id);
    auto options = filesystem::ScanOptions{payload.include_dot_hidden,
                                           payload.follow_directory_symlinks,
                                           payload.worker_count};
    auto root = std::filesystem::path(payload.root);
    auto token = cancellation.token();

    scan_thread_ = std::thread([this,
                                request_id,
                                scan_id,
                                root = std::move(root),
                                options,
                                token,
                                progress = std::move(progress),
                                completed = std::move(completed)]() mutable {
        try {
            const auto scan_started = std::chrono::steady_clock::now();
            auto scan_result = scanner_.scan(root, options, token, [&](const filesystem::ScanProgress& stats) {
                if (!progress) {
                    return;
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - scan_started);
                progress(request_id,
                         protocol::ScanProgressPayload{scan_id,
                                                       stats.discovered,
                                                       stats.processed,
                                                       stats.skipped,
                                                       stats.failed,
                                                       static_cast<std::uint64_t>(elapsed.count())});
            });

            if (!scan_result.ok()) {
                finish_scan_state(false);
                complete_scan(request_id,
                              completed,
                              Result<protocol::ScanCompletedPayload>::failure(scan_result.error()));
                return;
            }

            index::IndexBuilder builder;
            auto build_result = builder.build(next_generation_, std::move(scan_result).value(), token);
            if (!build_result.ok()) {
                finish_scan_state(false);
                complete_scan(request_id,
                              completed,
                              Result<protocol::ScanCompletedPayload>::failure(build_result.error()));
                return;
            }

            const auto file_count = static_cast<std::uint64_t>(build_result.value()->records.size());
            const auto generation = build_result.value()->generation;
            {
                std::unique_lock lock(snapshot_mutex_);
                current_snapshot_ = build_result.value();
                next_generation_ = generation + 1;
            }
            search_engine_.clear_cache();
            finish_scan_state(true);

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - scan_started);
            complete_scan(request_id,
                          completed,
                          Result<protocol::ScanCompletedPayload>::success(
                              protocol::ScanCompletedPayload{scan_id,
                                                             generation,
                                                             file_count,
                                                             static_cast<std::uint64_t>(elapsed.count())}));
        } catch (const std::exception& ex) {
            finish_scan_state(false);
            complete_scan(request_id,
                          completed,
                          Result<protocol::ScanCompletedPayload>::failure(
                              make_error(ErrorCode::InternalError, ex.what())));
        } catch (...) {
            finish_scan_state(false);
            complete_scan(request_id,
                          completed,
                          Result<protocol::ScanCompletedPayload>::failure(
                              make_error(ErrorCode::InternalError, "scan worker failed with an unknown exception")));
        }
    });

    return Result<ScanStarted>::success(ScanStarted{scan_id});
}

Result<void> ServiceController::cancel_scan()
{
    std::lock_guard lock(state_mutex_);
    if (state_ != ServiceState::Scanning) {
        return Result<void>::failure(make_error(ErrorCode::InvalidArgument, "no active scan"));
    }
    active_scan_cancel_.cancel();
    return Result<void>::success();
}

Result<protocol::SearchResponsePayload> ServiceController::search(const protocol::SearchRequestPayload& payload)
{
    std::shared_ptr<const index::IndexSnapshot> snapshot;
    {
        std::shared_lock lock(snapshot_mutex_);
        snapshot = current_snapshot_;
    }
    if (!snapshot) {
        return Result<protocol::SearchResponsePayload>::failure(
            make_error(ErrorCode::IndexNotReady, "index is not ready"));
    }

    bool stale = false;
    {
        std::lock_guard lock(state_mutex_);
        stale = state_ == ServiceState::Scanning;
    }

    index::SearchRequest request;
    request.query_utf8 = payload.query;
    request.limit = payload.limit;
    auto response = search_engine_.search(*snapshot, request);
    if (!response.ok()) {
        return Result<protocol::SearchResponsePayload>::failure(response.error());
    }

    return Result<protocol::SearchResponsePayload>::success(
        make_search_payload(*snapshot, response.value(), stale));
}

ServiceStatus ServiceController::status() const
{
    ServiceStatus status;
    {
        std::lock_guard lock(state_mutex_);
        status.state = state_;
        status.scan_active = state_ == ServiceState::Scanning;
    }
    {
        std::shared_lock lock(snapshot_mutex_);
        if (current_snapshot_) {
            status.generation = current_snapshot_->generation;
            status.file_count = static_cast<std::uint64_t>(current_snapshot_->records.size());
        }
    }
    return status;
}

void ServiceController::shutdown()
{
    bool should_cancel = false;
    {
        std::lock_guard lock(state_mutex_);
        if (shutdown_requested_) {
            return;
        }
        shutdown_requested_ = true;
        should_cancel = state_ == ServiceState::Scanning;
    }
    if (should_cancel) {
        active_scan_cancel_.cancel();
    }
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
    const auto has_snapshot = [&] {
        std::shared_lock lock(snapshot_mutex_);
        return current_snapshot_ != nullptr;
    }();
    {
        std::lock_guard lock(state_mutex_);
        state_ = has_snapshot ? ServiceState::Ready : ServiceState::NoIndex;
    }
}

void ServiceController::join_finished_scan()
{
    if (scan_thread_.joinable()) {
        bool should_join = false;
        {
            std::lock_guard lock(state_mutex_);
            should_join = state_ != ServiceState::Scanning;
        }
        if (should_join) {
            scan_thread_.join();
        }
    }
}

void ServiceController::finish_scan_state(bool installed_snapshot)
{
    const auto has_snapshot = [&] {
        if (installed_snapshot) {
            return true;
        }
        std::shared_lock lock(snapshot_mutex_);
        return current_snapshot_ != nullptr;
    }();

    std::lock_guard lock(state_mutex_);
    if (shutdown_requested_) {
        return;
    }
    state_ = has_snapshot ? ServiceState::Ready : ServiceState::NoIndex;
}

void ServiceController::complete_scan(RequestId request_id,
                                      CompletionCallback& completed,
                                      Result<protocol::ScanCompletedPayload> result) noexcept
{
    if (!completed) {
        return;
    }
    try {
        completed(request_id, std::move(result));
    } catch (...) {
    }
}

protocol::SearchResponsePayload ServiceController::make_search_payload(const index::IndexSnapshot& snapshot,
                                                                       const index::SearchResponse& response,
                                                                       bool stale) const
{
    protocol::SearchResponsePayload payload;
    payload.generation = response.generation;
    payload.stale = stale;
    payload.elapsed_us = static_cast<std::uint64_t>(response.elapsed.count());
    payload.from_cache = response.from_cache;
    payload.results.reserve(response.hits.size());

    for (const auto& hit : response.hits) {
        const auto pos = snapshot.id_to_pos.find(hit.id);
        if (pos == snapshot.id_to_pos.end()) {
            continue;
        }
        const auto& record = snapshot.records[pos->second];
        payload.results.push_back(protocol::SearchResultPayload{
            index::path_to_index_text(record.file_name),
            index::path_to_index_text(record.absolute_path),
            index::path_to_index_text(record.relative_path),
            static_cast<std::uint64_t>(record.size_bytes),
            file_time_to_ms(record.modified_time),
            entry_type_to_protocol(record.type),
            hit.score});
    }

    return payload;
}

std::string_view to_string(ServiceState state) noexcept
{
    switch (state) {
    case ServiceState::NoIndex:
        return "NoIndex";
    case ServiceState::Scanning:
        return "Scanning";
    case ServiceState::Ready:
        return "Ready";
    }
    return "NoIndex";
}

} // namespace coredesk::service
