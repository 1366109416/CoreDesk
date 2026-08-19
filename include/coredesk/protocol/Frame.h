#pragma once

#include "coredesk/common/Types.h"
#include "coredesk/protocol/MessageTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace coredesk::protocol {

inline constexpr std::size_t kFrameHeaderSize = 24;
inline constexpr std::size_t kMaxPayloadSize = 1024 * 1024;
inline constexpr std::uint16_t kFrameVersion = 1;

struct Frame {
    MessageType type{};
    std::uint32_t flags{};
    RequestId request_id{};
    std::vector<std::byte> payload;
};

} // namespace coredesk::protocol
