#include "coredesk/common/Logger.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef COREDESK_TEST_DATA_DIR
#error COREDESK_TEST_DATA_DIR must be defined for logger tests
#endif

namespace {

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        directory_ = std::filesystem::path(COREDESK_TEST_DATA_DIR) / test_name();
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
        ASSERT_TRUE(std::filesystem::create_directories(directory_));
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::string test_name() const
    {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }

    std::vector<std::string> read_lines(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::vector<std::string> lines;
        for (std::string line; std::getline(input, line);) {
            lines.push_back(std::move(line));
        }
        return lines;
    }

    std::filesystem::path directory_;
};

TEST_F(LoggerTest, WritesOneCompleteLine)
{
    const auto path = directory_ / "one.log";
    coredesk::Logger logger;
    ASSERT_TRUE(logger.open(path).ok());
    logger.log(coredesk::LogLevel::Info, "service", "Service started");
    logger.close();

    const auto lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(std::regex_match(lines[0], std::regex(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z \| INFO \| pid=\d+ \| tid=.+ \| service \| Service started$)")));
}

TEST_F(LoggerTest, ConcurrentWritesRemainWholeLines)
{
    const auto path = directory_ / "concurrent.log";
    coredesk::Logger logger;
    ASSERT_TRUE(logger.open(path).ok());
    constexpr int thread_count = 8;
    constexpr int lines_per_thread = 100;
    std::vector<std::thread> threads;
    for (int thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&logger, thread]() {
            for (int line = 0; line < lines_per_thread; ++line) {
                logger.log(coredesk::LogLevel::Info,
                           "concurrency",
                           "thread=" + std::to_string(thread) + " line=" + std::to_string(line));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    logger.close();

    const auto lines = read_lines(path);
    ASSERT_EQ(lines.size(), static_cast<std::size_t>(thread_count * lines_per_thread));
    const std::regex complete(R"(^.+ \| INFO \| pid=\d+ \| tid=.+ \| concurrency \| thread=\d+ line=\d+$)");
    std::unordered_map<std::string, std::size_t> message_counts;
    for (const auto& line : lines) {
        EXPECT_TRUE(std::regex_match(line, complete));
        const auto marker = line.find(" | concurrency | ");
        ASSERT_NE(marker, std::string::npos);
        ++message_counts[line.substr(marker + std::string(" | concurrency | ").size())];
    }
    ASSERT_EQ(message_counts.size(), static_cast<std::size_t>(thread_count * lines_per_thread));
    for (int thread = 0; thread < thread_count; ++thread) {
        for (int line = 0; line < lines_per_thread; ++line) {
            const auto expected = "thread=" + std::to_string(thread) + " line=" + std::to_string(line);
            EXPECT_EQ(message_counts[expected], 1U) << expected;
        }
    }
}

TEST_F(LoggerTest, NewlinesAreSanitized)
{
    const auto path = directory_ / "newlines.log";
    coredesk::Logger logger;
    ASSERT_TRUE(logger.open(path).ok());
    logger.log(coredesk::LogLevel::Warning, "ipc\nserver", "first line\r\nsecond line");
    logger.close();

    const auto lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_NE(lines[0].find(" | ipc server | first line  second line"), std::string::npos);
}

TEST_F(LoggerTest, DifferentLevelsAreRecorded)
{
    const auto path = directory_ / "levels.log";
    coredesk::Logger logger;
    ASSERT_TRUE(logger.open(path).ok());
    logger.log(coredesk::LogLevel::Debug, "test", "debug");
    logger.log(coredesk::LogLevel::Info, "test", "info");
    logger.log(coredesk::LogLevel::Warning, "test", "warning");
    logger.log(coredesk::LogLevel::Error, "test", "error");
    logger.close();

    const auto lines = read_lines(path);
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_NE(lines[0].find(" | DEBUG | "), std::string::npos);
    EXPECT_NE(lines[1].find(" | INFO | "), std::string::npos);
    EXPECT_NE(lines[2].find(" | WARN | "), std::string::npos);
    EXPECT_NE(lines[3].find(" | ERROR | "), std::string::npos);
}

TEST_F(LoggerTest, OpenFailureIsHandled)
{
    const auto blocker = directory_ / "not-a-directory";
    std::ofstream(blocker) << "block";
    coredesk::Logger logger;
    const auto result = logger.open(blocker / "logger.log");
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(logger.is_open());
}

TEST_F(LoggerTest, CloseAndReopen)
{
    const auto first = directory_ / "first.log";
    const auto second = directory_ / "second.log";
    coredesk::Logger logger;
    ASSERT_TRUE(logger.open(first).ok());
    logger.log(coredesk::LogLevel::Info, "test", "first");
    logger.close();
    ASSERT_FALSE(logger.is_open());
    ASSERT_TRUE(logger.open(second).ok());
    logger.log(coredesk::LogLevel::Info, "test", "second");
    logger.close();
    EXPECT_EQ(read_lines(first).size(), 1U);
    EXPECT_EQ(read_lines(second).size(), 1U);
}

} // namespace
