#pragma once

#include "coredesk/common/Error.h"
#include "coredesk/common/Result.h"
#include "coredesk/common/Types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace coredesk::protocol {

struct ScanRequestPayload {
    std::string root;
    bool include_dot_hidden{false};
    bool follow_directory_symlinks{false};
    std::size_t worker_count{};
};

struct ScanProgressPayload {
    std::string scan_id;
    std::uint64_t discovered{};
    std::uint64_t processed{};
    std::uint64_t skipped{};
    std::uint64_t failed{};
    std::uint64_t elapsed_ms{};
};

struct ScanCompletedPayload {
    std::string scan_id;
    IndexGeneration generation{};
    std::uint64_t file_count{};
    std::uint64_t elapsed_ms{};
};

struct SearchRequestPayload {
    std::string query;
    std::size_t limit{100};
};

struct SearchResultPayload {
    std::string name;
    std::string path;
    std::string relative_path;
    std::uint64_t size{};
    std::int64_t modified_ms{};
    std::string type;
    int score{};
};

struct SearchResponsePayload {
    IndexGeneration generation{};
    bool stale{false};
    std::uint64_t elapsed_us{};
    bool from_cache{false};
    std::vector<SearchResultPayload> results;
};

struct ErrorResponsePayload {
    bool ok{false};
    ErrorCode code{ErrorCode::Ok};
    std::string message;
};

Result<std::vector<std::byte>> encode_scan_request_payload(const ScanRequestPayload& payload);
Result<ScanRequestPayload> decode_scan_request_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_scan_progress_payload(const ScanProgressPayload& payload);
Result<ScanProgressPayload> decode_scan_progress_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_scan_completed_payload(const ScanCompletedPayload& payload);
Result<ScanCompletedPayload> decode_scan_completed_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_search_request_payload(const SearchRequestPayload& payload);
Result<SearchRequestPayload> decode_search_request_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_search_response_payload(const SearchResponsePayload& payload);
Result<SearchResponsePayload> decode_search_response_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_error_response_payload(const ErrorResponsePayload& payload);
Result<ErrorResponsePayload> decode_error_response_payload(std::span<const std::byte> bytes);

} // namespace coredesk::protocol
