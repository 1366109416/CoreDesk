#pragma once

#include <cstdint>

namespace coredesk {

using FileId = std::uint64_t;
using IndexGeneration = std::uint64_t;
using RequestId = std::uint64_t;

enum class EntryType : std::uint8_t {
    RegularFile,
    Directory,
    Symlink,
    Other
};

} // namespace coredesk
