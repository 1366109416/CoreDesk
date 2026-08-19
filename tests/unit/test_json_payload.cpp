#include "coredesk/protocol/JsonPayload.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

using coredesk::ErrorCode;
using namespace coredesk::protocol;

std::vector<std::byte> bytes_from_text(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
}

std::vector<std::byte> search_response_json(std::string_view modified_ms, std::string_view score)
{
    std::string json =
        "{\"generation\":3,\"stale\":false,\"elapsed_us\":1,\"from_cache\":false,"
        "\"results\":[{\"name\":\"x\",\"path\":\"x\",\"relative_path\":\"x\",\"size\":0,"
        "\"modified_ms\":";
    json += modified_ms;
    json += ",\"type\":\"file\",\"score\":";
    json += score;
    json += "}]}";
    return bytes_from_text(json);
}

} // namespace

TEST(JsonPayloadTest, ScanRequestRoundtripUsesFixedFieldNames)
{
    ScanRequestPayload payload{"D:/Documents", true, false, 4};
    auto encoded = encode_scan_request_payload(payload);
    ASSERT_TRUE(encoded.ok());

    auto decoded = decode_scan_request_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().root, "D:/Documents");
    EXPECT_TRUE(decoded.value().include_dot_hidden);
    EXPECT_FALSE(decoded.value().follow_directory_symlinks);
    EXPECT_EQ(decoded.value().worker_count, 4U);
}

TEST(JsonPayloadTest, ScanProgressRoundtrip)
{
    ScanProgressPayload payload{"1", 12000, 11820, 123, 57, 950};
    auto encoded = encode_scan_progress_payload(payload);
    ASSERT_TRUE(encoded.ok());

    auto decoded = decode_scan_progress_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().scan_id, "1");
    EXPECT_EQ(decoded.value().discovered, 12000U);
    EXPECT_EQ(decoded.value().processed, 11820U);
    EXPECT_EQ(decoded.value().skipped, 123U);
    EXPECT_EQ(decoded.value().failed, 57U);
    EXPECT_EQ(decoded.value().elapsed_ms, 950U);
}

TEST(JsonPayloadTest, ScanCompletedRoundtrip)
{
    ScanCompletedPayload payload{"1", 3, 11820, 1420};
    auto encoded = encode_scan_completed_payload(payload);
    ASSERT_TRUE(encoded.ok());

    auto decoded = decode_scan_completed_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().scan_id, "1");
    EXPECT_EQ(decoded.value().generation, 3U);
    EXPECT_EQ(decoded.value().file_count, 11820U);
    EXPECT_EQ(decoded.value().elapsed_ms, 1420U);
}

TEST(JsonPayloadTest, SearchRequestRoundtrip)
{
    SearchRequestPayload payload{"project report", 25};
    auto encoded = encode_search_request_payload(payload);
    ASSERT_TRUE(encoded.ok());

    auto decoded = decode_search_request_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().query, "project report");
    EXPECT_EQ(decoded.value().limit, 25U);
}

TEST(JsonPayloadTest, SearchResponseRoundtrip)
{
    SearchResponsePayload payload;
    payload.generation = 3;
    payload.stale = false;
    payload.elapsed_us = 730;
    payload.from_cache = true;
    payload.results.push_back({"project_report.docx",
                               "D:/Documents/work/project_report.docx",
                               "work/project_report.docx",
                               124334,
                               1760000000000LL,
                               "file",
                               100});

    auto encoded = encode_search_response_payload(payload);
    ASSERT_TRUE(encoded.ok());

    auto decoded = decode_search_response_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.value().results.size(), 1U);
    EXPECT_EQ(decoded.value().generation, 3U);
    EXPECT_FALSE(decoded.value().stale);
    EXPECT_EQ(decoded.value().elapsed_us, 730U);
    EXPECT_TRUE(decoded.value().from_cache);
    EXPECT_EQ(decoded.value().results[0].name, "project_report.docx");
    EXPECT_EQ(decoded.value().results[0].relative_path, "work/project_report.docx");
    EXPECT_EQ(decoded.value().results[0].type, "file");
    EXPECT_EQ(decoded.value().results[0].score, 100);
}

TEST(JsonPayloadTest, ErrorResponseRoundtrip)
{
    ErrorResponsePayload payload{false, ErrorCode::PermissionDenied, "Cannot access root directory"};
    auto encoded = encode_error_response_payload(payload);
    ASSERT_TRUE(encoded.ok());

    auto decoded = decode_error_response_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_FALSE(decoded.value().ok);
    EXPECT_EQ(decoded.value().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(decoded.value().message, "Cannot access root directory");
}

TEST(JsonPayloadTest, MalformedJsonMapsToProtocolError)
{
    auto decoded = decode_search_request_payload(bytes_from_text("{\"query\":"));
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::ProtocolError);
}

TEST(JsonPayloadTest, MissingRequiredFieldMapsToInvalidArgument)
{
    auto decoded = decode_search_request_payload(bytes_from_text("{\"query\":\"report\"}"));
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::InvalidArgument);
}

TEST(JsonPayloadTest, WrongFieldTypeMapsToInvalidArgument)
{
    auto decoded = decode_search_request_payload(bytes_from_text("{\"query\":42,\"limit\":100}"));
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::InvalidArgument);
}

TEST(JsonPayloadTest, InvalidSchemaValueMapsToInvalidArgument)
{
    const auto json = bytes_from_text(
        "{\"generation\":3,\"stale\":false,\"elapsed_us\":1,\"from_cache\":false,"
        "\"results\":[{\"name\":\"x\",\"path\":\"x\",\"relative_path\":\"x\",\"size\":0,"
        "\"modified_ms\":0,\"type\":\"socket\",\"score\":1}]}");
    auto decoded = decode_search_response_payload(json);
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::InvalidArgument);
}

TEST(JsonPayloadTest, ScoreAcceptsIntBoundaries)
{
    auto max_score = decode_search_response_payload(
        search_response_json("0", std::to_string(std::numeric_limits<int>::max())));
    ASSERT_TRUE(max_score.ok());
    ASSERT_EQ(max_score.value().results.size(), 1U);
    EXPECT_EQ(max_score.value().results[0].score, std::numeric_limits<int>::max());

    auto min_score = decode_search_response_payload(
        search_response_json("0", std::to_string(std::numeric_limits<int>::min())));
    ASSERT_TRUE(min_score.ok());
    ASSERT_EQ(min_score.value().results.size(), 1U);
    EXPECT_EQ(min_score.value().results[0].score, std::numeric_limits<int>::min());
}

TEST(JsonPayloadTest, ScoreRejectsOutOfRangeIntegers)
{
    const auto too_large = static_cast<long long>(std::numeric_limits<int>::max()) + 1LL;
    auto above_max = decode_search_response_payload(search_response_json("0", std::to_string(too_large)));
    ASSERT_FALSE(above_max.ok());
    EXPECT_EQ(above_max.error().code, ErrorCode::InvalidArgument);

    const auto too_small = static_cast<long long>(std::numeric_limits<int>::min()) - 1LL;
    auto below_min = decode_search_response_payload(search_response_json("0", std::to_string(too_small)));
    ASSERT_FALSE(below_min.ok());
    EXPECT_EQ(below_min.error().code, ErrorCode::InvalidArgument);

    auto huge_unsigned = decode_search_response_payload(search_response_json("0", "18446744073709551615"));
    ASSERT_FALSE(huge_unsigned.ok());
    EXPECT_EQ(huge_unsigned.error().code, ErrorCode::InvalidArgument);
}

TEST(JsonPayloadTest, ModifiedMsAcceptsInt64Boundaries)
{
    auto max_modified = decode_search_response_payload(
        search_response_json(std::to_string(std::numeric_limits<std::int64_t>::max()), "1"));
    ASSERT_TRUE(max_modified.ok());
    ASSERT_EQ(max_modified.value().results.size(), 1U);
    EXPECT_EQ(max_modified.value().results[0].modified_ms, std::numeric_limits<std::int64_t>::max());

    auto min_modified = decode_search_response_payload(
        search_response_json(std::to_string(std::numeric_limits<std::int64_t>::min()), "1"));
    ASSERT_TRUE(min_modified.ok());
    ASSERT_EQ(min_modified.value().results.size(), 1U);
    EXPECT_EQ(min_modified.value().results[0].modified_ms, std::numeric_limits<std::int64_t>::min());
}

TEST(JsonPayloadTest, ModifiedMsRejectsUnsignedAboveInt64Max)
{
    const auto above_int64 = std::to_string(static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U);
    auto decoded = decode_search_response_payload(search_response_json(above_int64, "1"));
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::InvalidArgument);
}
