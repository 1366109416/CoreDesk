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

TEST(JsonPayloadTest, HelloAndHelloAckRoundtrip)
{
    auto hello = encode_hello_payload(HelloPayload{1, "Sender"});
    ASSERT_TRUE(hello.ok());
    auto decoded_hello = decode_hello_payload(hello.value());
    ASSERT_TRUE(decoded_hello.ok());
    EXPECT_EQ(decoded_hello.value().protocol_version, 1U);
    EXPECT_EQ(decoded_hello.value().node_name, "Sender");

    auto ack = encode_hello_ack_payload(HelloAckPayload{1, "Receiver"});
    ASSERT_TRUE(ack.ok());
    auto decoded_ack = decode_hello_ack_payload(ack.value());
    ASSERT_TRUE(decoded_ack.ok());
    EXPECT_EQ(decoded_ack.value().protocol_version, 1U);
    EXPECT_EQ(decoded_ack.value().node_name, "Receiver");
}

TEST(JsonPayloadTest, HelloRejectsWrongProtocolVersion)
{
    auto decoded = decode_hello_payload(bytes_from_text("{\"protocol_version\":2,\"node_name\":\"Peer\"}"));
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::InvalidArgument);
}

TEST(JsonPayloadTest, FileOfferRoundtrip)
{
    FileOfferPayload payload{"0123456789abcdef0123456789abcdef",
                             "report.pdf",
                             123456789,
                             262144,
                             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    auto encoded = encode_file_offer_payload(payload);
    ASSERT_TRUE(encoded.ok());

    auto decoded = decode_file_offer_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().transfer_id, payload.transfer_id);
    EXPECT_EQ(decoded.value().file_name, payload.file_name);
    EXPECT_EQ(decoded.value().file_size, payload.file_size);
    EXPECT_EQ(decoded.value().chunk_size, payload.chunk_size);
    EXPECT_EQ(decoded.value().sha256, payload.sha256);
}

TEST(JsonPayloadTest, FileOfferRejectsInvalidTransferIdShaAndFileName)
{
    const auto good_sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    auto bad_id = decode_file_offer_payload(bytes_from_text(
        "{\"transfer_id\":\"0123456789ABCDEF0123456789abcdef\",\"file_name\":\"report.pdf\","
        "\"file_size\":1,\"chunk_size\":262144,\"sha256\":\"" + std::string(good_sha) + "\"}"));
    ASSERT_FALSE(bad_id.ok());
    EXPECT_EQ(bad_id.error().code, ErrorCode::InvalidArgument);

    auto traversal = decode_file_offer_payload(bytes_from_text(
        "{\"transfer_id\":\"0123456789abcdef0123456789abcdef\",\"file_name\":\"../report.pdf\","
        "\"file_size\":1,\"chunk_size\":262144,\"sha256\":\"" + std::string(good_sha) + "\"}"));
    ASSERT_FALSE(traversal.ok());
    EXPECT_EQ(traversal.error().code, ErrorCode::InvalidArgument);

    auto bad_sha = decode_file_offer_payload(bytes_from_text(
        "{\"transfer_id\":\"0123456789abcdef0123456789abcdef\",\"file_name\":\"report.pdf\","
        "\"file_size\":1,\"chunk_size\":262144,\"sha256\":\"not-a-sha\"}"));
    ASSERT_FALSE(bad_sha.ok());
    EXPECT_EQ(bad_sha.error().code, ErrorCode::InvalidArgument);
}

TEST(JsonPayloadTest, FileAcceptRequiresZeroStartOffset)
{
    FileAcceptPayload payload{"0123456789abcdef0123456789abcdef", 0};
    auto encoded = encode_file_accept_payload(payload);
    ASSERT_TRUE(encoded.ok());
    auto decoded = decode_file_accept_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().transfer_id, payload.transfer_id);
    EXPECT_EQ(decoded.value().start_offset, 0U);

    auto non_zero = decode_file_accept_payload(
        bytes_from_text("{\"transfer_id\":\"0123456789abcdef0123456789abcdef\",\"start_offset\":1}"));
    ASSERT_FALSE(non_zero.ok());
    EXPECT_EQ(non_zero.error().code, ErrorCode::InvalidArgument);
}

TEST(JsonPayloadTest, FileRejectFinishAndResultRoundtrip)
{
    const std::string transfer_id = "0123456789abcdef0123456789abcdef";

    auto reject = encode_file_reject_payload(FileRejectPayload{transfer_id, ErrorCode::TargetExists, "exists"});
    ASSERT_TRUE(reject.ok());
    auto decoded_reject = decode_file_reject_payload(reject.value());
    ASSERT_TRUE(decoded_reject.ok());
    EXPECT_EQ(decoded_reject.value().transfer_id, transfer_id);
    EXPECT_EQ(decoded_reject.value().code, ErrorCode::TargetExists);
    EXPECT_EQ(decoded_reject.value().message, "exists");

    auto finish = encode_file_finish_payload(FileFinishPayload{transfer_id});
    ASSERT_TRUE(finish.ok());
    auto decoded_finish = decode_file_finish_payload(finish.value());
    ASSERT_TRUE(decoded_finish.ok());
    EXPECT_EQ(decoded_finish.value().transfer_id, transfer_id);

    auto result = encode_file_result_payload(FileResultPayload{transfer_id, false, ErrorCode::HashMismatch, "bad hash"});
    ASSERT_TRUE(result.ok());
    auto decoded_result = decode_file_result_payload(result.value());
    ASSERT_TRUE(decoded_result.ok());
    EXPECT_FALSE(decoded_result.value().ok);
    EXPECT_EQ(decoded_result.value().code, ErrorCode::HashMismatch);
    EXPECT_EQ(decoded_result.value().message, "bad hash");
}

TEST(JsonPayloadTest, FileChunkBinaryRoundtripAndBigEndianLayout)
{
    FileChunkPayload payload{"0123456789abcdef0123456789abcdef",
                             0x0102030405060708ULL,
                             {std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}}};
    auto encoded = encode_file_chunk_payload(payload);
    ASSERT_TRUE(encoded.ok());
    ASSERT_EQ(encoded.value().size(), 32U + 8U + 4U + 3U);

    EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(encoded.value()[0])), '0');
    EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(encoded.value()[31])), 'f');
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[32]), 0x01);
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[33]), 0x02);
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[39]), 0x08);
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[40]), 0x00);
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[41]), 0x00);
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[42]), 0x00);
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[43]), 0x03);
    EXPECT_EQ(std::to_integer<unsigned char>(encoded.value()[44]), 0xaa);

    auto decoded = decode_file_chunk_payload(encoded.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().transfer_id, payload.transfer_id);
    EXPECT_EQ(decoded.value().offset, payload.offset);
    EXPECT_EQ(decoded.value().data, payload.data);
}

TEST(JsonPayloadTest, FileChunkRejectsLengthMismatchAndOversizedPayload)
{
    auto encoded = encode_file_chunk_payload(
        FileChunkPayload{"0123456789abcdef0123456789abcdef", 0, {std::byte{0x01}, std::byte{0x02}}});
    ASSERT_TRUE(encoded.ok());
    encoded.value()[43] = std::byte{0x03};
    auto mismatch = decode_file_chunk_payload(encoded.value());
    ASSERT_FALSE(mismatch.ok());
    EXPECT_EQ(mismatch.error().code, ErrorCode::InvalidArgument);

    std::vector<std::byte> oversized(1024 * 1024 + 1, std::byte{0});
    auto too_large = decode_file_chunk_payload(oversized);
    ASSERT_FALSE(too_large.ok());
    EXPECT_EQ(too_large.error().code, ErrorCode::PayloadTooLarge);
}
