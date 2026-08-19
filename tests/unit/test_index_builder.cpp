#include "coredesk/index/IndexBuilder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <type_traits>

namespace coredesk::index {
namespace {

filesystem::FileRecord record(std::string name, std::filesystem::path relative)
{
    filesystem::FileRecord file;
    file.file_name = std::move(name);
    file.extension = file.file_name.extension();
    file.relative_path = std::move(relative);
    file.absolute_path = std::filesystem::path("D:/root") / file.relative_path;
    file.type = EntryType::RegularFile;
    return file;
}

filesystem::ScanOutput scan_with_records(std::vector<filesystem::FileRecord> records)
{
    filesystem::ScanOutput scan;
    scan.root = "D:/root";
    scan.records = std::move(records);
    return scan;
}

TEST(IndexBuilderTest, AssignsFileIdsAndIdToPosition)
{
    IndexBuilder builder;
    CancellationSource cancellation;
    auto result = builder.build(7, scan_with_records({
                                     record("beta.txt", "beta.txt"),
                                     record("alpha.txt", "sub/alpha.txt"),
                                 }),
                                cancellation.token());

    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto snapshot = result.value();
    EXPECT_EQ(snapshot->generation, 7U);
    ASSERT_EQ(snapshot->records.size(), 2U);
    EXPECT_EQ(snapshot->records[0].id, 1U);
    EXPECT_EQ(snapshot->records[1].id, 2U);
    EXPECT_EQ(snapshot->id_to_pos.at(1), 0U);
    EXPECT_EQ(snapshot->id_to_pos.at(2), 1U);
}

TEST(IndexBuilderTest, PostingListsAreDeduplicatedAndSorted)
{
    IndexBuilder builder;
    CancellationSource cancellation;
    auto result = builder.build(1, scan_with_records({
                                     record("report_report.txt", "z/report_report.txt"),
                                     record("report.txt", "a/report.txt"),
                                 }),
                                cancellation.token());

    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& ids = result.value()->token_index.at("report").ids;
    EXPECT_EQ(ids, (std::vector<FileId>{1, 2}));
}

TEST(IndexBuilderTest, SortedTokensAreUniqueAndSorted)
{
    IndexBuilder builder;
    CancellationSource cancellation;
    auto result = builder.build(1, scan_with_records({
                                     record("Beta.txt", "Beta.txt"),
                                     record("alpha.txt", "alpha.txt"),
                                 }),
                                cancellation.token());

    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& sorted_tokens = result.value()->sorted_tokens;
    EXPECT_TRUE(std::is_sorted(sorted_tokens.begin(), sorted_tokens.end()));
    EXPECT_EQ(std::adjacent_find(sorted_tokens.begin(), sorted_tokens.end()), sorted_tokens.end());
}

TEST(IndexBuilderTest, NormalizedNamesMatchRecords)
{
    IndexBuilder builder;
    CancellationSource cancellation;
    auto result = builder.build(1, scan_with_records({
                                     record("Report.txt", "Reports/Report.txt"),
                                 }),
                                cancellation.token());

    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value()->normalized_names.size(), result.value()->records.size());
    EXPECT_NE(result.value()->normalized_names[0].find("reports/report.txt"), std::string::npos);
}

TEST(IndexBuilderTest, ReturnsSharedPtrToConstSnapshot)
{
    IndexBuilder builder;
    CancellationSource cancellation;
    auto result = builder.build(1, scan_with_records({record("a.txt", "a.txt")}), cancellation.token());

    ASSERT_TRUE(result.ok()) << result.error().message;
    static_assert(std::is_same_v<std::remove_reference_t<decltype(*result.value())>, const IndexSnapshot>);
}

TEST(IndexBuilderTest, CancellationReturnsCancelledWithoutSnapshot)
{
    IndexBuilder builder;
    CancellationSource cancellation;
    cancellation.cancel();
    auto result = builder.build(1, scan_with_records({record("a.txt", "a.txt")}), cancellation.token());

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

} // namespace
} // namespace coredesk::index
