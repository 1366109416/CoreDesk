#include "coredesk/index/Tokenizer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace coredesk::index {
namespace {

std::string bytes(std::u8string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const auto ch : text) {
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

TEST(TokenizerTest, LowercasesAscii)
{
    EXPECT_EQ(tokenize("Report FINAL.TXT"), (std::vector<std::string>{"report", "final", "txt"}));
}

TEST(TokenizerTest, SplitsEverySeparator)
{
    EXPECT_EQ(tokenize("a b.c_d-e/f\\g(h)i[j]k{l}"),
              (std::vector<std::string>{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l"}));
}

TEST(TokenizerTest, MergesConsecutiveSeparators)
{
    EXPECT_EQ(tokenize("alpha...___---beta"), (std::vector<std::string>{"alpha", "beta"}));
}

TEST(TokenizerTest, PreservesChineseUtf8Bytes)
{
    const auto text = bytes(u8"中文报告.txt");
    EXPECT_EQ(tokenize(text), (std::vector<std::string>{bytes(u8"中文报告"), "txt"}));
}

TEST(TokenizerTest, PathTextPreservesNonAsciiBytes)
{
    const std::filesystem::path path(std::u8string(u8"目录/中文.txt"));
    EXPECT_EQ(path_to_index_text(path), bytes(u8"目录/中文.txt"));
}

TEST(TokenizerTest, KeepsTokenAtOneHundredTwentyEightBytes)
{
    const std::string token(128, 'A');
    const auto tokens = tokenize(token);
    ASSERT_EQ(tokens.size(), 1U);
    EXPECT_EQ(tokens.front(), std::string(128, 'a'));
}

TEST(TokenizerTest, TruncatesOversizedToken)
{
    const std::string token(140, 'b');
    const auto tokens = tokenize(token);
    ASSERT_EQ(tokens.size(), 1U);
    EXPECT_EQ(tokens.front().size(), 128U);
}

} // namespace
} // namespace coredesk::index
