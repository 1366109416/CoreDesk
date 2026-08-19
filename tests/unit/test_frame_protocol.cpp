#include "coredesk/protocol/FrameCodec.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using coredesk::ErrorCode;
using coredesk::protocol::Frame;
using coredesk::protocol::FrameDecoder;
using coredesk::protocol::FrameEncoder;
using coredesk::protocol::MessageType;

std::vector<std::byte> bytes_from_text(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    return bytes;
}

std::vector<std::byte> encode_or_fail(const Frame& frame)
{
    auto encoded = FrameEncoder::encode(frame);
    EXPECT_TRUE(encoded.ok());
    return std::move(encoded).value();
}

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>((value >> 8) & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>(value & 0xFFU);
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::byte>((value >> 24) & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 16) & 0xFFU);
    bytes[offset + 2] = static_cast<std::byte>((value >> 8) & 0xFFU);
    bytes[offset + 3] = static_cast<std::byte>(value & 0xFFU);
}

} // namespace

TEST(FrameProtocolTest, EncodeDecodeNormalRoundtrip)
{
    Frame frame{MessageType::SearchRequest, 0x01020304U, 42, bytes_from_text("hello")};
    const auto encoded = encode_or_fail(frame);

    FrameDecoder decoder;
    auto decoded = decoder.push(encoded);
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.value().size(), 1U);
    EXPECT_EQ(decoded.value()[0].type, MessageType::SearchRequest);
    EXPECT_EQ(decoded.value()[0].flags, 0x01020304U);
    EXPECT_EQ(decoded.value()[0].request_id, 42U);
    EXPECT_EQ(decoded.value()[0].payload, frame.payload);
}

TEST(FrameProtocolTest, EncodesExactBigEndianHeaderBytes)
{
    Frame frame{MessageType::FileChunk, 0x01020304U, 0x0102030405060708ULL, bytes_from_text("abc")};
    const auto encoded = encode_or_fail(frame);

    ASSERT_GE(encoded.size(), coredesk::protocol::kFrameHeaderSize);
    EXPECT_EQ(encoded[0], std::byte{'C'});
    EXPECT_EQ(encoded[1], std::byte{'D'});
    EXPECT_EQ(encoded[2], std::byte{'S'});
    EXPECT_EQ(encoded[3], std::byte{'K'});
    EXPECT_EQ(encoded[4], std::byte{0x00});
    EXPECT_EQ(encoded[5], std::byte{0x01});
    EXPECT_EQ(encoded[6], std::byte{0x00});
    EXPECT_EQ(encoded[7], std::byte{0x71});
    EXPECT_EQ(encoded[8], std::byte{0x01});
    EXPECT_EQ(encoded[9], std::byte{0x02});
    EXPECT_EQ(encoded[10], std::byte{0x03});
    EXPECT_EQ(encoded[11], std::byte{0x04});
    EXPECT_EQ(encoded[12], std::byte{0x01});
    EXPECT_EQ(encoded[13], std::byte{0x02});
    EXPECT_EQ(encoded[14], std::byte{0x03});
    EXPECT_EQ(encoded[15], std::byte{0x04});
    EXPECT_EQ(encoded[16], std::byte{0x05});
    EXPECT_EQ(encoded[17], std::byte{0x06});
    EXPECT_EQ(encoded[18], std::byte{0x07});
    EXPECT_EQ(encoded[19], std::byte{0x08});
    EXPECT_EQ(encoded[20], std::byte{0x00});
    EXPECT_EQ(encoded[21], std::byte{0x00});
    EXPECT_EQ(encoded[22], std::byte{0x00});
    EXPECT_EQ(encoded[23], std::byte{0x03});
}

TEST(FrameProtocolTest, PartialHeaderReturnsNoFrameUntilComplete)
{
    const auto encoded = encode_or_fail(Frame{MessageType::Ping, 0, 1, bytes_from_text("x")});
    FrameDecoder decoder;

    auto first = decoder.push(std::span<const std::byte>(encoded.data(), 7));
    ASSERT_TRUE(first.ok());
    EXPECT_TRUE(first.value().empty());

    auto second = decoder.push(std::span<const std::byte>(encoded.data() + 7, 10));
    ASSERT_TRUE(second.ok());
    EXPECT_TRUE(second.value().empty());

    auto third = decoder.push(std::span<const std::byte>(encoded.data() + 17, encoded.size() - 17));
    ASSERT_TRUE(third.ok());
    ASSERT_EQ(third.value().size(), 1U);
    EXPECT_EQ(third.value()[0].type, MessageType::Ping);
}

TEST(FrameProtocolTest, PartialPayloadReturnsNoFrameUntilComplete)
{
    const auto encoded = encode_or_fail(Frame{MessageType::Pong, 0, 2, bytes_from_text("payload")});
    FrameDecoder decoder;

    auto first = decoder.push(std::span<const std::byte>(encoded.data(), coredesk::protocol::kFrameHeaderSize + 3));
    ASSERT_TRUE(first.ok());
    EXPECT_TRUE(first.value().empty());

    auto second = decoder.push(
        std::span<const std::byte>(encoded.data() + coredesk::protocol::kFrameHeaderSize + 3, encoded.size() - 27));
    ASSERT_TRUE(second.ok());
    ASSERT_EQ(second.value().size(), 1U);
    EXPECT_EQ(second.value()[0].payload, bytes_from_text("payload"));
}

TEST(FrameProtocolTest, MultipleFramesInOnePush)
{
    auto first = encode_or_fail(Frame{MessageType::Ping, 0, 1, bytes_from_text("one")});
    auto second = encode_or_fail(Frame{MessageType::Pong, 0, 2, bytes_from_text("two")});
    first.insert(first.end(), second.begin(), second.end());

    FrameDecoder decoder;
    auto decoded = decoder.push(first);
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.value().size(), 2U);
    EXPECT_EQ(decoded.value()[0].type, MessageType::Ping);
    EXPECT_EQ(decoded.value()[1].type, MessageType::Pong);
    EXPECT_EQ(decoded.value()[1].payload, bytes_from_text("two"));
}

TEST(FrameProtocolTest, CompleteFramePlusPartialNextFrame)
{
    auto first = encode_or_fail(Frame{MessageType::Ping, 0, 1, bytes_from_text("one")});
    auto second = encode_or_fail(Frame{MessageType::Pong, 0, 2, bytes_from_text("two")});
    std::vector<std::byte> chunk = first;
    chunk.insert(chunk.end(), second.begin(), second.begin() + 8);

    FrameDecoder decoder;
    auto decoded = decoder.push(chunk);
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.value().size(), 1U);
    EXPECT_EQ(decoded.value()[0].type, MessageType::Ping);

    auto final = decoder.push(std::span<const std::byte>(second.data() + 8, second.size() - 8));
    ASSERT_TRUE(final.ok());
    ASSERT_EQ(final.value().size(), 1U);
    EXPECT_EQ(final.value()[0].type, MessageType::Pong);
}

TEST(FrameProtocolTest, ArbitraryChunkBoundary)
{
    const auto encoded = encode_or_fail(Frame{MessageType::SearchResponse, 7, 99, bytes_from_text("abcdef")});

    for (std::size_t split = 0; split <= encoded.size(); ++split) {
        FrameDecoder decoder;
        auto first = decoder.push(std::span<const std::byte>(encoded.data(), split));
        ASSERT_TRUE(first.ok());
        if (split < encoded.size()) {
            EXPECT_TRUE(first.value().empty()) << split;
        } else {
            ASSERT_EQ(first.value().size(), 1U) << split;
            EXPECT_EQ(first.value()[0].payload, bytes_from_text("abcdef")) << split;
        }

        auto second = decoder.push(std::span<const std::byte>(encoded.data() + split, encoded.size() - split));
        ASSERT_TRUE(second.ok()) << split;
        if (split < encoded.size()) {
            ASSERT_EQ(second.value().size(), 1U) << split;
            EXPECT_EQ(second.value()[0].payload, bytes_from_text("abcdef")) << split;
        } else {
            EXPECT_TRUE(second.value().empty()) << split;
        }
    }
}

TEST(FrameProtocolTest, BadMagicReturnsProtocolError)
{
    auto encoded = encode_or_fail(Frame{MessageType::Ping, 0, 1, {}});
    encoded[0] = std::byte{'X'};

    FrameDecoder decoder;
    auto decoded = decoder.push(encoded);
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::ProtocolError);
}

TEST(FrameProtocolTest, BadVersionReturnsProtocolError)
{
    auto encoded = encode_or_fail(Frame{MessageType::Ping, 0, 1, {}});
    write_u16(encoded, 4, 2);

    FrameDecoder decoder;
    auto decoded = decoder.push(encoded);
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::ProtocolError);
}

TEST(FrameProtocolTest, UnknownMessageTypeReturnsProtocolError)
{
    auto encoded = encode_or_fail(Frame{MessageType::Ping, 0, 1, {}});
    write_u16(encoded, 6, 999);

    FrameDecoder decoder;
    auto decoded = decoder.push(encoded);
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::ProtocolError);
}

TEST(FrameProtocolTest, OversizedPayloadReturnsPayloadTooLarge)
{
    auto encoded = encode_or_fail(Frame{MessageType::Ping, 0, 1, {}});
    write_u32(encoded, 20, static_cast<std::uint32_t>(coredesk::protocol::kMaxPayloadSize + 1));

    FrameDecoder decoder;
    auto decoded = decoder.push(encoded);
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().code, ErrorCode::PayloadTooLarge);
}

TEST(FrameProtocolTest, ZeroLengthPayloadIsAllowed)
{
    const auto encoded = encode_or_fail(Frame{MessageType::StatusRequest, 0, 3, {}});

    FrameDecoder decoder;
    auto decoded = decoder.push(encoded);
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.value().size(), 1U);
    EXPECT_TRUE(decoded.value()[0].payload.empty());
}

TEST(FrameProtocolTest, ResetClearsPartialState)
{
    const auto bad_partial = encode_or_fail(Frame{MessageType::Ping, 0, 1, bytes_from_text("bad")});
    const auto good = encode_or_fail(Frame{MessageType::Pong, 0, 2, bytes_from_text("good")});

    FrameDecoder decoder;
    auto partial = decoder.push(std::span<const std::byte>(bad_partial.data(), 8));
    ASSERT_TRUE(partial.ok());
    EXPECT_TRUE(partial.value().empty());

    decoder.reset();
    auto decoded = decoder.push(good);
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.value().size(), 1U);
    EXPECT_EQ(decoded.value()[0].type, MessageType::Pong);
    EXPECT_EQ(decoded.value()[0].payload, bytes_from_text("good"));
}

TEST(FrameProtocolTest, EncoderRejectsOversizedPayload)
{
    Frame frame{MessageType::FileChunk, 0, 5, std::vector<std::byte>(coredesk::protocol::kMaxPayloadSize + 1)};
    auto encoded = FrameEncoder::encode(frame);
    ASSERT_FALSE(encoded.ok());
    EXPECT_EQ(encoded.error().code, ErrorCode::PayloadTooLarge);
}

TEST(FrameProtocolTest, EncoderRejectsUnknownMessageType)
{
    Frame frame{static_cast<MessageType>(999), 0, 5, {}};
    auto encoded = FrameEncoder::encode(frame);
    ASSERT_FALSE(encoded.ok());
    EXPECT_EQ(encoded.error().code, ErrorCode::ProtocolError);
}
