#pragma once

#include "coredesk/common/Cancellation.h"
#include "coredesk/common/Logger.h"
#include "coredesk/common/Result.h"
#include "coredesk/common/Types.h"
#include "coredesk/filesystem/FileScanner.h"
#include "coredesk/index/IndexSnapshot.h"
#include "coredesk/index/SearchEngine.h"
#include "coredesk/protocol/JsonPayload.h"

#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>

namespace coredesk::service {

enum class ServiceState {
    NoIndex,
    Scanning,
    Ready
};

struct ScanStarted {
    std::string scan_id;
};

struct ServiceStatus {
    ServiceState state{ServiceState::NoIndex};
    IndexGeneration generation{};
    std::uint64_t file_count{};
    bool scan_active{false};
};

class ServiceController {
public:
    using ProgressCallback = std::function<void(RequestId, const protocol::ScanProgressPayload&)>;
    using CompletionCallback = std::function<void(RequestId, Result<protocol::ScanCompletedPayload>)>;

    ServiceController();
    ~ServiceController();

    ServiceController(const ServiceController&) = delete;
    ServiceController& operator=(const ServiceController&) = delete;

    void set_logger(Logger* logger) noexcept;

    Result<ScanStarted> start_scan(RequestId request_id,
                                   const protocol::ScanRequestPayload& payload,
                                   ProgressCallback progress,
                                   CompletionCallback completed);
    Result<void> cancel_scan();
    Result<protocol::SearchResponsePayload> search(const protocol::SearchRequestPayload& payload);
    ServiceStatus status() const;
    void shutdown();

private:
    void join_finished_scan();
    void finish_scan_state(bool installed_snapshot);
    void complete_scan(RequestId request_id,
                       CompletionCallback& completed,
                       Result<protocol::ScanCompletedPayload> result) noexcept;
    protocol::SearchResponsePayload make_search_payload(const index::IndexSnapshot& snapshot,
                                                        const index::SearchResponse& response,
                                                        bool stale) const;

    mutable std::mutex state_mutex_;
    ServiceState state_{ServiceState::NoIndex};
    bool shutdown_requested_{false};
    CancellationSource active_scan_cancel_;
    RequestId active_scan_request_id_{};
    std::thread scan_thread_;

    mutable std::shared_mutex snapshot_mutex_;
    std::shared_ptr<const index::IndexSnapshot> current_snapshot_;
    IndexGeneration next_generation_{1};

    filesystem::FileScanner scanner_;
    index::SearchEngine search_engine_;
    Logger* logger_{};
};

std::string_view to_string(ServiceState state) noexcept;

} // namespace coredesk::service
