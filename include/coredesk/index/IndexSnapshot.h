#pragma once

#include "coredesk/common/Types.h"
#include "coredesk/filesystem/FileRecord.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace coredesk::index {

struct PostingList {
    std::vector<FileId> ids;
};

struct IndexSnapshot {
    IndexGeneration generation{};
    std::filesystem::path root;
    std::vector<filesystem::FileRecord> records;
    std::unordered_map<FileId, std::size_t> id_to_pos;
    std::unordered_map<std::string, PostingList> token_index;
    std::vector<std::string> sorted_tokens;
    std::vector<std::string> normalized_names;
};

} // namespace coredesk::index
