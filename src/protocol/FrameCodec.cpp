#include "coredesk/protocol/FrameCodec.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace coredesk::protocol {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'C'},
    std::byte{'D'},
    std::byte{'S'},
    std::byte{'K'},
};

std::uint8_t byte_to_u8(std::byte value) noexcept
{
    return static_cast<std::uint8_t>(value);
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

void write_u64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i) {
        const auto shift = static_cast<unsigned>((7 - i) * 8);
        bytes[offset + i] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
}

std::uint16_t read_u16(const std::vector<std::byte>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(byte_to_u8(bytes[offset])) << 8) |
                                      static_cast<std::uint16_t>(byte_to_u8(bytes[offset + 1])));
}

std::uint32_t read_u32(const std::vector<std::byte>& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(byte_to_u8(bytes[offset])) << 24) |
           (static_cast<std::uint32_t>(byte_to_u8(bytes[offset + 1])) << 16) |
           (static_cast<std::uint32_t>(byte_to_u8(bytes[offset + 2])) << 8) |
           static_cast<std::uint32_t>(byte_to_u8(bytes[offset + 3]));
}

std::uint64_t read_u64(const std::vector<std::byte>& bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(byte_to_u8(bytes[offset + i]));
    }
    return value;
}

bool has_magic(const std::vector<std::byte>& bytes)
{
    return std::equal(kMagic.begin(), kMagic.end(), bytes.begin());
}

Error protocol_error(std::string message)
{
    return {ErrorCode::ProtocolError, std::move(message)};
}

} // namespace

bool is_known_message_type(MessageType type) noexcept
{
    switch (type) {
    case MessageType::Ping:
    case MessageType::Pong:
    case MessageType::ScanRequest:
    case MessageType::ScanAccepted:
    case MessageType::ScanProgress:
    case MessageType::ScanCompleted:
    case MessageType::ScanFailed:
    case MessageType::CancelScanRequest:
    case MessageType::SearchRequest:
    case MessageType::SearchResponse:
    case MessageType::StatusRequest:
    case MessageType::StatusResponse:
    case MessageType::Hello:
    case MessageType::HelloAck:
    case MessageType::FileOffer:
    case MessageType::FileAccept:
    case MessageType::FileReject:
    case MessageType::FileChunk:
    case MessageType::FileFinish:
    case MessageType::FileResult:
        return true;
    }
    return false;
}

Result<MessageType> message_type_from_wire(std::uint16_t value)
{
    const auto type = static_cast<MessageType>(value);
    if (!is_known_message_type(type)) {
        return Result<MessageType>::failure(protocol_error("unknown message type"));
    }
    return Result<MessageType>::success(type);
}

std::uint16_t message_type_to_wire(MessageType type) noexcept
{
    return static_cast<std::uint16_t>(type);
}

Result<std::vector<std::byte>> FrameEncoder::encode(const Frame& frame)
{
    if (!is_known_message_type(frame.type)) {
        return Result<std::vector<std::byte>>::failure(protocol_error("unknown message type"));
    }
    if (frame.payload.size() > kMaxPayloadSize) {
        return Result<std::vector<std::byte>>::failure(
            {ErrorCode::PayloadTooLarge, "frame payload exceeds 1 MiB"});
    }
    if (frame.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::vector<std::byte>>::failure(
            {ErrorCode::PayloadTooLarge, "frame payload exceeds uint32 range"});
    }

    const auto total_size = kFrameHeaderSize + frame.payload.size();
    std::vector<std::byte> bytes(total_size);
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    write_u16(bytes, 4, kFrameVersion);
    write_u16(bytes, 6, message_type_to_wire(frame.type));
    write_u32(bytes, 8, frame.flags);
    write_u64(bytes, 12, frame.request_id);
    write_u32(bytes, 20, static_cast<std::uint32_t>(frame.payload.size()));
    std::copy(frame.payload.begin(), frame.payload.end(), bytes.begin() + kFrameHeaderSize);
    return Result<std::vector<std::byte>>::success(std::move(bytes));
}

Result<std::vector<Frame>> FrameDecoder::push(std::span<const std::byte> bytes)
{
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());

    std::vector<Frame> frames;
    while (buffer_.size() >= kFrameHeaderSize) {
        if (!has_magic(buffer_)) {
            buffer_.clear();
            return Result<std::vector<Frame>>::failure(protocol_error("bad frame magic"));
        }

        const auto version = read_u16(buffer_, 4);
        if (version != kFrameVersion) {
            buffer_.clear();
            return Result<std::vector<Frame>>::failure(protocol_error("bad frame version"));
        }

        auto type_result = message_type_from_wire(read_u16(buffer_, 6));
        if (!type_result.ok()) {
            buffer_.clear();
            return Result<std::vector<Frame>>::failure(type_result.error());
        }

        const auto flags = read_u32(buffer_, 8);
        const auto request_id = read_u64(buffer_, 12);
        const auto payload_length = read_u32(buffer_, 20);
        if (payload_length > kMaxPayloadSize) {
            buffer_.clear();
            return Result<std::vector<Frame>>::failure(
                {ErrorCode::PayloadTooLarge, "frame payload exceeds 1 MiB"});
        }

        const auto total_size = kFrameHeaderSize + static_cast<std::size_t>(payload_length);
        if (buffer_.size() < total_size) {
            break;
        }

        Frame frame;
        frame.type = type_result.value();
        frame.flags = flags;
        frame.request_id = request_id;
        frame.payload.assign(buffer_.begin() + kFrameHeaderSize, buffer_.begin() + total_size);
        frames.push_back(std::move(frame));
        buffer_.erase(buffer_.begin(), buffer_.begin() + total_size);
    }

    return Result<std::vector<Frame>>::success(std::move(frames));
}

void FrameDecoder::reset()
{
    buffer_.clear();
}

} // namespace coredesk::protocol
