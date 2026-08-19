#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace coredesk::index {

std::string path_to_index_text(const std::filesystem::path& path);
std::string normalize_index_text(std::string_view text);
std::string trim_ascii(std::string_view text);
std::vector<std::string> tokenize(std::string_view text);
std::vector<std::string> tokenize_path(const std::filesystem::path& path);

} // namespace coredesk::index
