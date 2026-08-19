#pragma once

#include "coredesk/common/Result.h"
#include "coredesk/protocol/Frame.h"

#include <cstddef>
#include <span>
#include <vector>

namespace coredesk::protocol {

class FrameEncoder {
public:
    static Result<std::vector<std::byte>> encode(const Frame& frame);
};

class FrameDecoder {
public:
    Result<std::vector<Frame>> push(std::span<const std::byte> bytes);
    void reset();

private:
    std::vector<std::byte> buffer_;
};

} // namespace coredesk::protocol
