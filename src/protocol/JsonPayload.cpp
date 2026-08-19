#include "coredesk/protocol/JsonPayload.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace coredesk::protocol {
namespace {

using Json = nlohmann::json;

std::vector<std::byte> to_bytes(const std::string& text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
}

std::string to_string(std::span<const std::byte> bytes)
{
    std::string text;
    text.reserve(bytes.size());
    for (const auto byte : bytes) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

Result<Json> parse_json(std::span<const std::byte> bytes)
{
    try {
        return Result<Json>::success(Json::parse(to_string(bytes)));
    } catch (const Json::parse_error& error) {
        return Result<Json>::failure({ErrorCode::ProtocolError, error.what()});
    }
}

Result<void> require_object(const Json& json)
{
    if (!json.is_object()) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "JSON payload must be an object"});
    }
    return Result<void>::success();
}

template <class T>
Result<T> missing_or_wrong_type(std::string_view field)
{
    return Result<T>::failure({ErrorCode::InvalidArgument, "missing or invalid field: " + std::string(field)});
}

Result<std::string> required_string(const Json& json, std::string_view field)
{
    const auto it = json.find(field);
    if (it == json.end() || !it->is_string()) {
        return missing_or_wrong_type<std::string>(field);
    }
    return Result<std::string>::success(it->get<std::string>());
}

Result<bool> required_bool(const Json& json, std::string_view field)
{
    const auto it = json.find(field);
    if (it == json.end() || !it->is_boolean()) {
        return missing_or_wrong_type<bool>(field);
    }
    return Result<bool>::success(it->get<bool>());
}

Result<std::uint64_t> required_u64(const Json& json, std::string_view field)
{
    const auto it = json.find(field);
    if (it == json.end() || !it->is_number_unsigned()) {
        return missing_or_wrong_type<std::uint64_t>(field);
    }
    return Result<std::uint64_t>::success(it->get<std::uint64_t>());
}

Result<std::size_t> required_size(const Json& json, std::string_view field)
{
    auto value = required_u64(json, field);
    if (!value.ok()) {
        return Result<std::size_t>::failure(value.error());
    }
    if (value.value() > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Result<std::size_t>::failure({ErrorCode::InvalidArgument, "field is too large: " + std::string(field)});
    }
    return Result<std::size_t>::success(static_cast<std::size_t>(value.value()));
}

Result<std::int64_t> required_i64(const Json& json, std::string_view field)
{
    const auto it = json.find(field);
    if (it == json.end() || !it->is_number_integer()) {
        return missing_or_wrong_type<std::int64_t>(field);
    }
    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return Result<std::int64_t>::failure(
                {ErrorCode::InvalidArgument, "field is out of range: " + std::string(field)});
        }
        return Result<std::int64_t>::success(static_cast<std::int64_t>(value));
    }
    return Result<std::int64_t>::success(it->get<std::int64_t>());
}

Result<int> required_int(const Json& json, std::string_view field)
{
    const auto it = json.find(field);
    if (it == json.end() || !it->is_number_integer()) {
        return missing_or_wrong_type<int>(field);
    }
    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return Result<int>::failure(
                {ErrorCode::InvalidArgument, "field is out of range: " + std::string(field)});
        }
        return Result<int>::success(static_cast<int>(value));
    }

    const auto value = it->get<std::int64_t>();
    if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        return Result<int>::failure({ErrorCode::InvalidArgument, "field is out of range: " + std::string(field)});
    }
    return Result<int>::success(static_cast<int>(value));
}

bool is_valid_result_type(std::string_view type)
{
    return type == "file" || type == "directory" || type == "symlink" || type == "other";
}

std::string error_code_to_protocol(ErrorCode code)
{
    switch (code) {
    case ErrorCode::Ok:
        return "OK";
    case ErrorCode::InvalidArgument:
        return "INVALID_ARGUMENT";
    case ErrorCode::PathNotFound:
        return "PATH_NOT_FOUND";
    case ErrorCode::PermissionDenied:
        return "PERMISSION_DENIED";
    case ErrorCode::Busy:
        return "BUSY";
    case ErrorCode::Cancelled:
        return "CANCELLED";
    case ErrorCode::IndexNotReady:
        return "INDEX_NOT_READY";
    case ErrorCode::ProtocolError:
        return "PROTOCOL_ERROR";
    case ErrorCode::PayloadTooLarge:
        return "PAYLOAD_TOO_LARGE";
    case ErrorCode::ConnectionFailed:
        return "CONNECTION_FAILED";
    case ErrorCode::Timeout:
        return "TIMEOUT";
    case ErrorCode::TargetExists:
        return "TARGET_EXISTS";
    case ErrorCode::IoError:
        return "IO_ERROR";
    case ErrorCode::HashMismatch:
        return "HASH_MISMATCH";
    case ErrorCode::InternalError:
        return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

Result<ErrorCode> error_code_from_protocol(std::string_view code)
{
    if (code == "OK") {
        return Result<ErrorCode>::success(ErrorCode::Ok);
    }
    if (code == "INVALID_ARGUMENT") {
        return Result<ErrorCode>::success(ErrorCode::InvalidArgument);
    }
    if (code == "PATH_NOT_FOUND") {
        return Result<ErrorCode>::success(ErrorCode::PathNotFound);
    }
    if (code == "PERMISSION_DENIED") {
        return Result<ErrorCode>::success(ErrorCode::PermissionDenied);
    }
    if (code == "BUSY") {
        return Result<ErrorCode>::success(ErrorCode::Busy);
    }
    if (code == "CANCELLED") {
        return Result<ErrorCode>::success(ErrorCode::Cancelled);
    }
    if (code == "INDEX_NOT_READY") {
        return Result<ErrorCode>::success(ErrorCode::IndexNotReady);
    }
    if (code == "PROTOCOL_ERROR") {
        return Result<ErrorCode>::success(ErrorCode::ProtocolError);
    }
    if (code == "PAYLOAD_TOO_LARGE") {
        return Result<ErrorCode>::success(ErrorCode::PayloadTooLarge);
    }
    if (code == "CONNECTION_FAILED") {
        return Result<ErrorCode>::success(ErrorCode::ConnectionFailed);
    }
    if (code == "TIMEOUT") {
        return Result<ErrorCode>::success(ErrorCode::Timeout);
    }
    if (code == "TARGET_EXISTS") {
        return Result<ErrorCode>::success(ErrorCode::TargetExists);
    }
    if (code == "IO_ERROR") {
        return Result<ErrorCode>::success(ErrorCode::IoError);
    }
    if (code == "HASH_MISMATCH") {
        return Result<ErrorCode>::success(ErrorCode::HashMismatch);
    }
    if (code == "INTERNAL_ERROR") {
        return Result<ErrorCode>::success(ErrorCode::InternalError);
    }
    return Result<ErrorCode>::failure({ErrorCode::InvalidArgument, "unknown error code"});
}

Result<std::vector<std::byte>> dump_json(const Json& json)
{
    try {
        return Result<std::vector<std::byte>>::success(to_bytes(json.dump()));
    } catch (const Json::exception& error) {
        return Result<std::vector<std::byte>>::failure({ErrorCode::InternalError, error.what()});
    }
}

template <class Payload, class Fn>
Result<Payload> decode_payload(std::span<const std::byte> bytes, Fn&& fn)
{
    auto parsed = parse_json(bytes);
    if (!parsed.ok()) {
        return Result<Payload>::failure(parsed.error());
    }
    auto object_result = require_object(parsed.value());
    if (!object_result.ok()) {
        return Result<Payload>::failure(object_result.error());
    }
    return fn(parsed.value());
}

} // namespace

Result<std::vector<std::byte>> encode_scan_request_payload(const ScanRequestPayload& payload)
{
    return dump_json(Json{{"root", payload.root},
                          {"include_dot_hidden", payload.include_dot_hidden},
                          {"follow_directory_symlinks", payload.follow_directory_symlinks},
                          {"worker_count", payload.worker_count}});
}

Result<ScanRequestPayload> decode_scan_request_payload(std::span<const std::byte> bytes)
{
    return decode_payload<ScanRequestPayload>(bytes, [](const Json& json) -> Result<ScanRequestPayload> {
        ScanRequestPayload payload;
        auto root = required_string(json, "root");
        auto include = required_bool(json, "include_dot_hidden");
        auto follow = required_bool(json, "follow_directory_symlinks");
        auto worker_count = required_size(json, "worker_count");
        if (!root.ok()) {
            return Result<ScanRequestPayload>::failure(root.error());
        }
        if (!include.ok()) {
            return Result<ScanRequestPayload>::failure(include.error());
        }
        if (!follow.ok()) {
            return Result<ScanRequestPayload>::failure(follow.error());
        }
        if (!worker_count.ok()) {
            return Result<ScanRequestPayload>::failure(worker_count.error());
        }
        payload.root = std::move(root).value();
        payload.include_dot_hidden = include.value();
        payload.follow_directory_symlinks = follow.value();
        payload.worker_count = worker_count.value();
        return Result<ScanRequestPayload>::success(std::move(payload));
    });
}

Result<std::vector<std::byte>> encode_scan_progress_payload(const ScanProgressPayload& payload)
{
    return dump_json(Json{{"scan_id", payload.scan_id},
                          {"discovered", payload.discovered},
                          {"processed", payload.processed},
                          {"skipped", payload.skipped},
                          {"failed", payload.failed},
                          {"elapsed_ms", payload.elapsed_ms}});
}

Result<ScanProgressPayload> decode_scan_progress_payload(std::span<const std::byte> bytes)
{
    return decode_payload<ScanProgressPayload>(bytes, [](const Json& json) -> Result<ScanProgressPayload> {
        ScanProgressPayload payload;
        auto scan_id = required_string(json, "scan_id");
        auto discovered = required_u64(json, "discovered");
        auto processed = required_u64(json, "processed");
        auto skipped = required_u64(json, "skipped");
        auto failed = required_u64(json, "failed");
        auto elapsed_ms = required_u64(json, "elapsed_ms");
        if (!scan_id.ok()) {
            return Result<ScanProgressPayload>::failure(scan_id.error());
        }
        if (!discovered.ok()) {
            return Result<ScanProgressPayload>::failure(discovered.error());
        }
        if (!processed.ok()) {
            return Result<ScanProgressPayload>::failure(processed.error());
        }
        if (!skipped.ok()) {
            return Result<ScanProgressPayload>::failure(skipped.error());
        }
        if (!failed.ok()) {
            return Result<ScanProgressPayload>::failure(failed.error());
        }
        if (!elapsed_ms.ok()) {
            return Result<ScanProgressPayload>::failure(elapsed_ms.error());
        }
        payload.scan_id = std::move(scan_id).value();
        payload.discovered = discovered.value();
        payload.processed = processed.value();
        payload.skipped = skipped.value();
        payload.failed = failed.value();
        payload.elapsed_ms = elapsed_ms.value();
        return Result<ScanProgressPayload>::success(std::move(payload));
    });
}

Result<std::vector<std::byte>> encode_scan_completed_payload(const ScanCompletedPayload& payload)
{
    return dump_json(Json{{"scan_id", payload.scan_id},
                          {"generation", payload.generation},
                          {"file_count", payload.file_count},
                          {"elapsed_ms", payload.elapsed_ms}});
}

Result<ScanCompletedPayload> decode_scan_completed_payload(std::span<const std::byte> bytes)
{
    return decode_payload<ScanCompletedPayload>(bytes, [](const Json& json) -> Result<ScanCompletedPayload> {
        ScanCompletedPayload payload;
        auto scan_id = required_string(json, "scan_id");
        auto generation = required_u64(json, "generation");
        auto file_count = required_u64(json, "file_count");
        auto elapsed_ms = required_u64(json, "elapsed_ms");
        if (!scan_id.ok()) {
            return Result<ScanCompletedPayload>::failure(scan_id.error());
        }
        if (!generation.ok()) {
            return Result<ScanCompletedPayload>::failure(generation.error());
        }
        if (!file_count.ok()) {
            return Result<ScanCompletedPayload>::failure(file_count.error());
        }
        if (!elapsed_ms.ok()) {
            return Result<ScanCompletedPayload>::failure(elapsed_ms.error());
        }
        payload.scan_id = std::move(scan_id).value();
        payload.generation = generation.value();
        payload.file_count = file_count.value();
        payload.elapsed_ms = elapsed_ms.value();
        return Result<ScanCompletedPayload>::success(std::move(payload));
    });
}

Result<std::vector<std::byte>> encode_search_request_payload(const SearchRequestPayload& payload)
{
    return dump_json(Json{{"query", payload.query}, {"limit", payload.limit}});
}

Result<SearchRequestPayload> decode_search_request_payload(std::span<const std::byte> bytes)
{
    return decode_payload<SearchRequestPayload>(bytes, [](const Json& json) -> Result<SearchRequestPayload> {
        SearchRequestPayload payload;
        auto query = required_string(json, "query");
        auto limit = required_size(json, "limit");
        if (!query.ok()) {
            return Result<SearchRequestPayload>::failure(query.error());
        }
        if (!limit.ok()) {
            return Result<SearchRequestPayload>::failure(limit.error());
        }
        payload.query = std::move(query).value();
        payload.limit = limit.value();
        return Result<SearchRequestPayload>::success(std::move(payload));
    });
}

Result<std::vector<std::byte>> encode_search_response_payload(const SearchResponsePayload& payload)
{
    Json results = Json::array();
    for (const auto& result : payload.results) {
        if (!is_valid_result_type(result.type)) {
            return Result<std::vector<std::byte>>::failure({ErrorCode::InvalidArgument, "invalid result type"});
        }
        results.push_back(Json{{"name", result.name},
                               {"path", result.path},
                               {"relative_path", result.relative_path},
                               {"size", result.size},
                               {"modified_ms", result.modified_ms},
                               {"type", result.type},
                               {"score", result.score}});
    }

    return dump_json(Json{{"generation", payload.generation},
                          {"stale", payload.stale},
                          {"elapsed_us", payload.elapsed_us},
                          {"from_cache", payload.from_cache},
                          {"results", std::move(results)}});
}

Result<SearchResponsePayload> decode_search_response_payload(std::span<const std::byte> bytes)
{
    return decode_payload<SearchResponsePayload>(bytes, [](const Json& json) -> Result<SearchResponsePayload> {
        SearchResponsePayload payload;
        auto generation = required_u64(json, "generation");
        auto stale = required_bool(json, "stale");
        auto elapsed_us = required_u64(json, "elapsed_us");
        auto from_cache = required_bool(json, "from_cache");
        const auto results_it = json.find("results");
        if (!generation.ok()) {
            return Result<SearchResponsePayload>::failure(generation.error());
        }
        if (!stale.ok()) {
            return Result<SearchResponsePayload>::failure(stale.error());
        }
        if (!elapsed_us.ok()) {
            return Result<SearchResponsePayload>::failure(elapsed_us.error());
        }
        if (!from_cache.ok()) {
            return Result<SearchResponsePayload>::failure(from_cache.error());
        }
        if (results_it == json.end() || !results_it->is_array()) {
            return Result<SearchResponsePayload>::failure({ErrorCode::InvalidArgument, "missing or invalid field: results"});
        }

        payload.generation = generation.value();
        payload.stale = stale.value();
        payload.elapsed_us = elapsed_us.value();
        payload.from_cache = from_cache.value();
        for (const auto& item : *results_it) {
            auto object_result = require_object(item);
            if (!object_result.ok()) {
                return Result<SearchResponsePayload>::failure(object_result.error());
            }
            SearchResultPayload result;
            auto name = required_string(item, "name");
            auto path = required_string(item, "path");
            auto relative_path = required_string(item, "relative_path");
            auto size = required_u64(item, "size");
            auto modified_ms = required_i64(item, "modified_ms");
            auto type = required_string(item, "type");
            auto score = required_int(item, "score");
            if (!name.ok()) {
                return Result<SearchResponsePayload>::failure(name.error());
            }
            if (!path.ok()) {
                return Result<SearchResponsePayload>::failure(path.error());
            }
            if (!relative_path.ok()) {
                return Result<SearchResponsePayload>::failure(relative_path.error());
            }
            if (!size.ok()) {
                return Result<SearchResponsePayload>::failure(size.error());
            }
            if (!modified_ms.ok()) {
                return Result<SearchResponsePayload>::failure(modified_ms.error());
            }
            if (!type.ok()) {
                return Result<SearchResponsePayload>::failure(type.error());
            }
            if (!score.ok()) {
                return Result<SearchResponsePayload>::failure(score.error());
            }
            if (!is_valid_result_type(type.value())) {
                return Result<SearchResponsePayload>::failure({ErrorCode::InvalidArgument, "invalid result type"});
            }
            result.name = std::move(name).value();
            result.path = std::move(path).value();
            result.relative_path = std::move(relative_path).value();
            result.size = size.value();
            result.modified_ms = modified_ms.value();
            result.type = std::move(type).value();
            result.score = score.value();
            payload.results.push_back(std::move(result));
        }
        return Result<SearchResponsePayload>::success(std::move(payload));
    });
}

Result<std::vector<std::byte>> encode_error_response_payload(const ErrorResponsePayload& payload)
{
    return dump_json(Json{{"ok", payload.ok},
                          {"code", error_code_to_protocol(payload.code)},
                          {"message", payload.message}});
}

Result<ErrorResponsePayload> decode_error_response_payload(std::span<const std::byte> bytes)
{
    return decode_payload<ErrorResponsePayload>(bytes, [](const Json& json) -> Result<ErrorResponsePayload> {
        ErrorResponsePayload payload;
        auto ok = required_bool(json, "ok");
        auto code_text = required_string(json, "code");
        auto message = required_string(json, "message");
        if (!ok.ok()) {
            return Result<ErrorResponsePayload>::failure(ok.error());
        }
        if (!code_text.ok()) {
            return Result<ErrorResponsePayload>::failure(code_text.error());
        }
        if (!message.ok()) {
            return Result<ErrorResponsePayload>::failure(message.error());
        }
        auto code = error_code_from_protocol(code_text.value());
        if (!code.ok()) {
            return Result<ErrorResponsePayload>::failure(code.error());
        }
        payload.ok = ok.value();
        payload.code = code.value();
        payload.message = std::move(message).value();
        return Result<ErrorResponsePayload>::success(std::move(payload));
    });
}

} // namespace coredesk::protocol
