#pragma once

#include "coredesk/common/Result.h"

#include <cstdint>

namespace coredesk::protocol {

enum class MessageType : std::uint16_t {
    Ping = 1,
    Pong = 2,
    ScanRequest = 10,
    ScanAccepted = 11,
    ScanProgress = 12,
    ScanCompleted = 13,
    ScanFailed = 14,
    CancelScanRequest = 15,
    SearchRequest = 20,
    SearchResponse = 21,
    StatusRequest = 30,
    StatusResponse = 31,
    Hello = 100,
    HelloAck = 101,
    FileOffer = 110,
    FileAccept = 111,
    FileReject = 112,
    FileChunk = 113,
    FileFinish = 114,
    FileResult = 115
};

bool is_known_message_type(MessageType type) noexcept;
Result<MessageType> message_type_from_wire(std::uint16_t value);
std::uint16_t message_type_to_wire(MessageType type) noexcept;

} // namespace coredesk::protocol
