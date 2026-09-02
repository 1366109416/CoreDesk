#include "coredesk/common/Cancellation.h"
#include "coredesk/filesystem/FileScanner.h"

#include <charconv>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

bool parse_worker_count(std::string_view text, std::size_t& value)
{
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end && value > 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: coredesk_bench_scan <root> <worker_count>\n";
        return 1;
    }

    std::size_t worker_count = 0;
    if (!parse_worker_count(argv[2], worker_count)) {
        std::cerr << "worker_count must be a positive integer\n";
        return 1;
    }

    coredesk::filesystem::ScanOptions options;
    options.worker_count = worker_count;
    coredesk::CancellationSource cancellation;
    coredesk::filesystem::FileScanner scanner;
    auto result = scanner.scan(std::filesystem::path(argv[1]), options, cancellation.token(), {});
    if (!result.ok()) {
        std::cerr << "scan failed: " << coredesk::to_string(result.error().code) << ' '
                  << result.error().message << '\n';
        return 2;
    }

    const auto& output = result.value();
    const auto directory_count = static_cast<std::size_t>(std::count_if(
        output.records.begin(), output.records.end(), [](const auto& record) {
            return record.type == coredesk::EntryType::Directory;
        }));
    const auto file_count = output.records.size() - directory_count;
    std::cout << "root: " << output.root << '\n'
              << "worker_count: " << worker_count << '\n'
              << "records: " << output.records.size() << '\n'
              << "file_count: " << file_count << '\n'
              << "directory_count: " << directory_count << '\n'
              << "discovered: " << output.stats.discovered << '\n'
              << "processed: " << output.stats.processed << '\n'
              << "skipped: " << output.stats.skipped << '\n'
              << "failed: " << output.stats.failed << '\n'
              << "elapsed_ms: " << output.elapsed.count() << '\n';
    return 0;
}
