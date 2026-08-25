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

struct HelloPayload {
    std::uint32_t protocol_version{1};
    std::string node_name;
};

struct HelloAckPayload {
    std::uint32_t protocol_version{1};
    std::string node_name;
};

struct FileOfferPayload {
    std::string transfer_id;
    std::string file_name;
    std::uint64_t file_size{};
    std::uint64_t chunk_size{};
    std::string sha256;
};

struct FileAcceptPayload {
    std::string transfer_id;
    std::uint64_t start_offset{};
};

struct FileRejectPayload {
    std::string transfer_id;
    ErrorCode code{ErrorCode::Ok};
    std::string message;
};

struct FileFinishPayload {
    std::string transfer_id;
};

struct FileResultPayload {
    std::string transfer_id;
    bool ok{false};
    ErrorCode code{ErrorCode::Ok};
    std::string message;
};

struct FileChunkPayload {
    std::string transfer_id;
    std::uint64_t offset{};
    std::vector<std::byte> data;
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

Result<std::vector<std::byte>> encode_hello_payload(const HelloPayload& payload);
Result<HelloPayload> decode_hello_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_hello_ack_payload(const HelloAckPayload& payload);
Result<HelloAckPayload> decode_hello_ack_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_file_offer_payload(const FileOfferPayload& payload);
Result<FileOfferPayload> decode_file_offer_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_file_accept_payload(const FileAcceptPayload& payload);
Result<FileAcceptPayload> decode_file_accept_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_file_reject_payload(const FileRejectPayload& payload);
Result<FileRejectPayload> decode_file_reject_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_file_finish_payload(const FileFinishPayload& payload);
Result<FileFinishPayload> decode_file_finish_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_file_result_payload(const FileResultPayload& payload);
Result<FileResultPayload> decode_file_result_payload(std::span<const std::byte> bytes);

Result<std::vector<std::byte>> encode_file_chunk_payload(const FileChunkPayload& payload);
Result<FileChunkPayload> decode_file_chunk_payload(std::span<const std::byte> bytes);

} // namespace coredesk::protocol
