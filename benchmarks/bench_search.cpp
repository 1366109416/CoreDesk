#include "coredesk/common/Cancellation.h"
#include "coredesk/filesystem/FileScanner.h"
#include "coredesk/index/IndexBuilder.h"
#include "coredesk/index/SearchEngine.h"
#include "coredesk/index/Tokenizer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::nanoseconds;

coredesk::filesystem::FileRecord make_record(std::size_t index)
{
    coredesk::filesystem::FileRecord record;
    const bool report = index % 10 == 0;
    const bool budget = index % 25 == 0;
    const auto directory = std::filesystem::path("docs") / ("group_" + std::to_string(index % 100));
    const auto file = std::string(report ? "report_" : "note_") +
                      (budget ? "budget_" : "") +
                      std::to_string(index) + ".txt";
    record.relative_path = directory / file;
    record.absolute_path = std::filesystem::path("D:/synthetic/root") / record.relative_path;
    record.file_name = file;
    record.extension = ".txt";
    record.type = coredesk::EntryType::RegularFile;
    record.size_bytes = index;
    return record;
}

coredesk::filesystem::ScanOutput make_scan(std::size_t count)
{
    coredesk::filesystem::ScanOutput scan;
    scan.root = "D:/synthetic/root";
    scan.records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        scan.records.push_back(make_record(i));
    }
    scan.stats.discovered = count;
    scan.stats.processed = count;
    return scan;
}

Duration median(std::vector<Duration> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

Duration average(const std::vector<Duration>& values)
{
    const auto total = std::accumulate(values.begin(), values.end(), Duration{0});
    return total / static_cast<int>(values.size());
}

std::size_t linear_substring_count(const coredesk::index::IndexSnapshot& snapshot, std::string_view query)
{
    const auto normalized = coredesk::index::normalize_index_text(query);
    std::size_t count = 0;
    for (const auto& name : snapshot.normalized_names) {
        if (name.find(normalized) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

std::size_t posting_count(const coredesk::index::IndexSnapshot& snapshot)
{
    std::size_t total = 0;
    for (const auto& [_, posting] : snapshot.token_index) {
        total += posting.ids.size();
    }
    return total;
}

template <class Fn>
std::vector<Duration> measure(std::size_t iterations, std::uint64_t& checksum, Fn&& fn)
{
    std::vector<Duration> timings;
    timings.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto started = Clock::now();
        checksum += fn();
        timings.push_back(std::chrono::duration_cast<Duration>(Clock::now() - started));
    }
    return timings;
}

long long as_microseconds(Duration duration)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace

int main()
{
    constexpr std::size_t kRecords = 100000;
    constexpr std::size_t kWarmup = 5;
    constexpr std::size_t kIterations = 30;
    const std::string query = "1000";
    std::uint64_t checksum = 0;

    coredesk::CancellationSource cancellation;
    coredesk::index::IndexBuilder builder;
    const auto build_started = Clock::now();
    auto snapshot_result = builder.build(1, make_scan(kRecords), cancellation.token());
    const auto build_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - build_started);
    if (!snapshot_result.ok()) {
        std::cerr << "index build failed\n";
        return 1;
    }

    const auto snapshot = snapshot_result.value();
    coredesk::index::SearchEngine search_engine;

    for (std::size_t i = 0; i < kWarmup; ++i) {
        checksum += linear_substring_count(*snapshot, query);
        const auto warmup_search = search_engine.search(*snapshot, {query, 100});
        if (warmup_search.ok()) {
            checksum += warmup_search.value().hits.size();
        }
    }
    search_engine.clear_cache();

    const auto linear_check_count = linear_substring_count(*snapshot, query);
    auto indexed_check = search_engine.search(*snapshot, {query, 100});
    if (!indexed_check.ok() || indexed_check.value().hits.empty()) {
        std::cerr << "benchmark search sanity check failed\n";
        return 2;
    }
    const auto indexed_check_hits = indexed_check.value().hits.size();

    const auto baseline = measure(kIterations, checksum, [&] {
        return static_cast<std::uint64_t>(linear_substring_count(*snapshot, query));
    });

    const auto indexed = measure(kIterations, checksum, [&] {
        search_engine.clear_cache();
        const auto response = search_engine.search(*snapshot, {query, 100});
        if (!response.ok()) {
            return std::uint64_t{0};
        }
        return static_cast<std::uint64_t>(response.value().hits.size() + response.value().generation +
                                          (response.value().from_cache ? 1 : 0));
    });

    search_engine.clear_cache();
    const auto priming_response = search_engine.search(*snapshot, {query, 100});
    if (priming_response.ok()) {
        checksum += priming_response.value().hits.size();
    }
    const auto cached = measure(kIterations, checksum, [&] {
        const auto response = search_engine.search(*snapshot, {query, 100});
        if (!response.ok()) {
            return std::uint64_t{0};
        }
        return static_cast<std::uint64_t>(response.value().hits.size() + response.value().generation +
                                          (response.value().from_cache ? 1 : 0));
    });

    std::cout << "records: " << snapshot->records.size() << '\n'
              << "tokens: " << snapshot->sorted_tokens.size() << '\n'
              << "postings: " << posting_count(*snapshot) << '\n'
              << "build_us: " << build_elapsed.count() << '\n'
              << "query_iterations: " << kIterations << '\n'
              << "linear_check_count: " << linear_check_count << '\n'
              << "indexed_check_hits: " << indexed_check_hits << '\n'
              << "linear_avg_us: " << as_microseconds(average(baseline)) << '\n'
              << "linear_median_us: " << as_microseconds(median(baseline)) << '\n'
              << "indexed_avg_us: " << as_microseconds(average(indexed)) << '\n'
              << "indexed_median_us: " << as_microseconds(median(indexed)) << '\n'
              << "cached_avg_us: " << as_microseconds(average(cached)) << '\n'
              << "cached_median_us: " << as_microseconds(median(cached)) << '\n'
              << "cached_median_ns: " << median(cached).count() << '\n'
              << "checksum: " << checksum << '\n';
    return 0;
}
