#pragma once

#include "coredesk/common/Types.h"

#include <cstdint>
#include <filesystem>

namespace coredesk::filesystem {

struct FileRecord {
    FileId id{};
    std::filesystem::path absolute_path;
    std::filesystem::path relative_path;
    std::filesystem::path file_name;
    std::filesystem::path extension;
    std::uintmax_t size_bytes{};
    std::filesystem::file_time_type modified_time{};
    EntryType type{EntryType::Other};
};

} // namespace coredesk::filesystem
