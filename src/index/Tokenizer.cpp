#include "coredesk/index/Tokenizer.h"

#include <algorithm>

namespace coredesk::index {
namespace {

constexpr std::size_t kMaxTokenBytes = 128;

bool is_separator(unsigned char ch) noexcept
{
    switch (ch) {
    case ' ':
    case '.':
    case '_':
    case '-':
    case '/':
    case '\\':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

bool is_ascii_space(unsigned char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

char ascii_lower(unsigned char ch) noexcept
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return static_cast<char>(ch);
}

} // namespace

std::string path_to_index_text(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    std::string result;
    result.reserve(text.size());
    for (const auto ch : text) {
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

std::string normalize_index_text(std::string_view text)
{
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char ch : text) {
        normalized.push_back(ascii_lower(ch));
    }
    return normalized;
}

std::string trim_ascii(std::string_view text)
{
    auto first = text.begin();
    while (first != text.end() && is_ascii_space(static_cast<unsigned char>(*first))) {
        ++first;
    }

    auto last = text.end();
    while (last != first && is_ascii_space(static_cast<unsigned char>(*(last - 1)))) {
        --last;
    }

    return std::string(first, last);
}

std::vector<std::string> tokenize(std::string_view text)
{
    std::vector<std::string> tokens;
    std::string current;

    auto flush = [&] {
        if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    };

    for (const unsigned char ch : text) {
        if (is_separator(ch)) {
            flush();
            continue;
        }

        if (current.size() < kMaxTokenBytes) {
            current.push_back(ascii_lower(ch));
        }
    }

    flush();
    return tokens;
}

std::vector<std::string> tokenize_path(const std::filesystem::path& path)
{
    return tokenize(path_to_index_text(path));
}

} // namespace coredesk::index
