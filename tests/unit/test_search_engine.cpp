#include "coredesk/index/IndexBuilder.h"
#include "coredesk/index/SearchEngine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <future>
#include <thread>

namespace coredesk::index {
namespace {

filesystem::FileRecord make_record(std::string name, std::filesystem::path relative)
{
    filesystem::FileRecord file;
    file.file_name = std::move(name);
    file.extension = file.file_name.extension();
    file.relative_path = std::move(relative);
    file.absolute_path = std::filesystem::path("D:/root") / file.relative_path;
    file.type = EntryType::RegularFile;
    return file;
}

std::shared_ptr<const IndexSnapshot> build_snapshot(std::vector<filesystem::FileRecord> records)
{
    filesystem::ScanOutput scan;
    scan.root = "D:/root";
    scan.records = std::move(records);
    IndexBuilder builder;
    CancellationSource cancellation;
    auto result = builder.build(3, std::move(scan), cancellation.token());
    EXPECT_TRUE(result.ok()) << result.error().message;
    return result.value();
}

std::vector<FileId> hit_ids(const SearchResponse& response)
{
    std::vector<FileId> ids;
    for (const auto& hit : response.hits) {
        ids.push_back(hit.id);
    }
    return ids;
}

SearchResponse search_or_fail(SearchEngine& engine, const IndexSnapshot& snapshot, std::string query, std::size_t limit = 100)
{
    auto result = engine.search(snapshot, {std::move(query), limit});
    EXPECT_TRUE(result.ok()) << result.error().message;
    return result.value();
}

TEST(SearchEngineTest, EmptyQueryReturnsEmptyHits)
{
    const auto snapshot = build_snapshot({make_record("report.txt", "report.txt")});
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "   ");
    EXPECT_TRUE(response.hits.empty());
}

TEST(SearchEngineTest, OversizedQueryReturnsInvalidArgument)
{
    const auto snapshot = build_snapshot({make_record("report.txt", "report.txt")});
    SearchEngine engine;
    auto result = engine.search(*snapshot, {std::string(257, 'a'), 100});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST(SearchEngineTest, ExactSearchUsesTokenIndex)
{
    const auto snapshot = build_snapshot({
        make_record("report.txt", "report.txt"),
        make_record("notes.txt", "notes.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report");
    EXPECT_EQ(hit_ids(response), (std::vector<FileId>{1}));
    EXPECT_EQ(response.hits[0].score, 80);
}

TEST(SearchEngineTest, PrefixSearchUsesSortedTokens)
{
    const auto snapshot = build_snapshot({
        make_record("annual_budget.txt", "annual_budget.txt"),
        make_record("report.txt", "report.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "budg");
    EXPECT_EQ(hit_ids(response), (std::vector<FileId>{1}));
    EXPECT_EQ(response.hits[0].score, 50);
}

TEST(SearchEngineTest, ExactTokenMatchTakesPriorityOverPrefixExpansion)
{
    const auto snapshot = build_snapshot({
        make_record("rep.txt", "rep.txt"),
        make_record("report.txt", "report.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "rep");
    EXPECT_EQ(hit_ids(response), (std::vector<FileId>{1}));
}

TEST(SearchEngineTest, AndSemanticsKeepsOnlyFilesMatchingAllTokens)
{
    const auto snapshot = build_snapshot({
        make_record("report_2026.txt", "report_2026.txt"),
        make_record("report_2025.txt", "report_2025.txt"),
        make_record("summary_2026.txt", "summary_2026.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report 2026");
    EXPECT_EQ(hit_ids(response), (std::vector<FileId>{1}));
    EXPECT_EQ(response.hits[0].score, 60);
}

TEST(SearchEngineTest, AndDoesNotBecomeOr)
{
    const auto snapshot = build_snapshot({
        make_record("report.txt", "report.txt"),
        make_record("budget.txt", "budget.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report budget");
    EXPECT_TRUE(response.hits.empty());
}

TEST(SearchEngineTest, SubstringFallbackRunsWhenIndexCandidatesAreEmpty)
{
    const auto snapshot = build_snapshot({make_record("annual_budget.txt", "annual_budget.txt")});
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "nnua");
    EXPECT_EQ(hit_ids(response), (std::vector<FileId>{1}));
    EXPECT_EQ(response.hits[0].score, 30);
}

TEST(SearchEngineTest, DoesNotFallbackWhenIndexCandidateExists)
{
    const auto snapshot = build_snapshot({
        make_record("alpha.txt", "alpha.txt"),
        make_record("xalpha.txt", "xalpha.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "alpha");
    EXPECT_EQ(hit_ids(response), (std::vector<FileId>{1}));
}

TEST(SearchEngineTest, ScoreUsesHighestLevelWithoutAccumulation)
{
    const auto snapshot = build_snapshot({make_record("report_2026.txt", "report_2026.txt")});
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report 2026");
    ASSERT_EQ(response.hits.size(), 1U);
    EXPECT_EQ(response.hits[0].score, 60);
}

TEST(SearchEngineTest, FilenamePrefixBeatsTokenExact)
{
    const auto snapshot = build_snapshot({make_record("report_final.txt", "report_final.txt")});
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report");
    ASSERT_EQ(response.hits.size(), 1U);
    EXPECT_EQ(response.hits[0].score, 80);
}

TEST(SearchEngineTest, FullFilenameExactScoresOneHundred)
{
    const auto snapshot = build_snapshot({make_record("report.txt", "report.txt")});
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report.txt");
    ASSERT_EQ(response.hits.size(), 1U);
    EXPECT_EQ(response.hits[0].score, 100);
}

TEST(SearchEngineTest, TieBreaksByFilenameLength)
{
    const auto snapshot = build_snapshot({
        make_record("long_report.txt", "long_report.txt"),
        make_record("report.txt", "report.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report");
    ASSERT_GE(response.hits.size(), 2U);
    EXPECT_EQ(response.hits[0].id, 2U);
}

TEST(SearchEngineTest, TieBreaksByRelativePath)
{
    const auto snapshot = build_snapshot({
        make_record("report.txt", "b/report.txt"),
        make_record("report.txt", "a/report.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report");
    ASSERT_GE(response.hits.size(), 2U);
    EXPECT_EQ(response.hits[0].id, 2U);
}

TEST(SearchEngineTest, LimitIsClampedToOneHundredAndOrderingIsDeterministic)
{
    std::vector<filesystem::FileRecord> records;
    for (int i = 0; i < 150; ++i) {
        records.push_back(make_record("report_" + std::to_string(i) + ".txt",
                                      "dir/report_" + std::to_string(i) + ".txt"));
    }
    const auto snapshot = build_snapshot(std::move(records));
    SearchEngine engine;
    const auto first = search_or_fail(engine, *snapshot, "report", 1000);
    engine.clear_cache();
    const auto second = search_or_fail(engine, *snapshot, "report", 1000);

    EXPECT_EQ(first.hits.size(), 100U);
    EXPECT_EQ(hit_ids(first), hit_ids(second));
}

TEST(SearchEngineTest, CacheMissThenHit)
{
    const auto snapshot = build_snapshot({make_record("report.txt", "report.txt")});
    SearchEngine engine;
    const auto first = search_or_fail(engine, *snapshot, "report");
    const auto second = search_or_fail(engine, *snapshot, "report");
    EXPECT_FALSE(first.from_cache);
    EXPECT_TRUE(second.from_cache);
}

TEST(SearchEngineTest, CacheHitElapsedIsMeasuredForCurrentCall)
{
    std::vector<filesystem::FileRecord> records;
    for (int i = 0; i < 2000; ++i) {
        records.push_back(make_record("report_" + std::to_string(i) + ".txt",
                                      "dir/report_" + std::to_string(i) + ".txt"));
    }
    const auto snapshot = build_snapshot(std::move(records));
    SearchEngine engine;
    const auto first = search_or_fail(engine, *snapshot, "report");
    const auto second = search_or_fail(engine, *snapshot, "report");

    EXPECT_FALSE(first.from_cache);
    EXPECT_TRUE(second.from_cache);
    EXPECT_EQ(second.generation, snapshot->generation);
    EXPECT_EQ(hit_ids(second), hit_ids(first));
    EXPECT_GE(first.elapsed.count(), 0);
    EXPECT_GE(second.elapsed.count(), 0);
}

TEST(SearchEngineTest, ClearCacheForcesMiss)
{
    const auto snapshot = build_snapshot({make_record("report.txt", "report.txt")});
    SearchEngine engine;
    static_cast<void>(search_or_fail(engine, *snapshot, "report"));
    EXPECT_TRUE(search_or_fail(engine, *snapshot, "report").from_cache);
    engine.clear_cache();
    EXPECT_FALSE(search_or_fail(engine, *snapshot, "report").from_cache);
}

TEST(SearchEngineTest, FilenamePrefixUsesFileNameNotPathPrefix)
{
    const auto snapshot = build_snapshot({
        make_record("budget.txt", "report/budget.txt"),
    });
    SearchEngine engine;
    const auto response = search_or_fail(engine, *snapshot, "report");
    ASSERT_EQ(response.hits.size(), 1U);
    EXPECT_EQ(response.hits[0].score, 60);
}

TEST(SearchEngineTest, ConcurrentSearchesShareCacheSafely)
{
    std::vector<filesystem::FileRecord> records;
    for (int i = 0; i < 500; ++i) {
        records.push_back(make_record("project_report_" + std::to_string(i) + ".txt",
                                      "dir/project_report_" + std::to_string(i) + ".txt"));
    }
    const auto snapshot = build_snapshot(std::move(records));
    SearchEngine engine;

    std::vector<std::future<void>> workers;
    for (int worker = 0; worker < 8; ++worker) {
        workers.push_back(std::async(std::launch::async, [&] {
            for (int i = 0; i < 50; ++i) {
                auto broad = engine.search(*snapshot, {"project", 100});
                ASSERT_TRUE(broad.ok()) << broad.error().message;
                ASSERT_FALSE(broad.value().hits.empty());
                EXPECT_LE(broad.value().hits.size(), 100U);

                auto narrow = engine.search(*snapshot, {"project_report_42", 100});
                ASSERT_TRUE(narrow.ok()) << narrow.error().message;
                ASSERT_FALSE(narrow.value().hits.empty());
                EXPECT_EQ(narrow.value().hits.front().id, 43U);
            }
        }));
    }

    for (auto& worker : workers) {
        worker.get();
    }
}

} // namespace
} // namespace coredesk::index
