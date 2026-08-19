#include "coredesk/common/Cancellation.h"
#include "coredesk/filesystem/FileScanner.h"
#include "coredesk/index/IndexBuilder.h"
#include "coredesk/index/SearchEngine.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view kVersion = "CoreDesk 1.0.0";

void print_usage()
{
    std::cout << "Usage: coredesk_cli --version\n"
              << "       coredesk_cli scan <root>\n"
              << "       coredesk_cli search <root> <query> [query...]\n";
}

void print_scan_output(const coredesk::filesystem::ScanOutput& output)
{
    std::cout << "root: " << output.root << '\n'
              << "records: " << output.records.size() << '\n'
              << "discovered: " << output.stats.discovered << '\n'
              << "processed: " << output.stats.processed << '\n'
              << "skipped: " << output.stats.skipped << '\n'
              << "failed: " << output.stats.failed << '\n'
              << "elapsed_ms: " << output.elapsed.count() << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << kVersion << '\n';
        return 0;
    }

    if (argc == 3 && std::string_view(argv[1]) == "scan") {
        coredesk::filesystem::FileScanner scanner;
        coredesk::filesystem::ScanOptions options;
        coredesk::CancellationSource cancellation;
        const auto result = scanner.scan(std::filesystem::path(argv[2]), options, cancellation.token(), {});

        if (!result.ok()) {
            std::cerr << "scan failed: " << coredesk::to_string(result.error().code)
                      << " " << result.error().message << '\n';
            return 2;
        }

        print_scan_output(result.value());
        return 0;
    }

    if (argc >= 4 && std::string_view(argv[1]) == "search") {
        coredesk::CancellationSource cancellation;
        coredesk::filesystem::FileScanner scanner;
        auto scan_result = scanner.scan(std::filesystem::path(argv[2]), {}, cancellation.token(), {});
        if (!scan_result.ok()) {
            std::cerr << "scan failed: " << coredesk::to_string(scan_result.error().code)
                      << " " << scan_result.error().message << '\n';
            return 2;
        }

        coredesk::index::IndexBuilder builder;
        auto snapshot_result = builder.build(1, std::move(scan_result).value(), cancellation.token());
        if (!snapshot_result.ok()) {
            std::cerr << "index build failed: " << coredesk::to_string(snapshot_result.error().code)
                      << " " << snapshot_result.error().message << '\n';
            return 3;
        }

        coredesk::index::SearchEngine search_engine;
        search_engine.clear_cache();
        const auto snapshot = snapshot_result.value();

        std::cout << "indexed_records: " << snapshot->records.size() << '\n';
        for (int i = 3; i < argc; ++i) {
            coredesk::index::SearchRequest request;
            request.query_utf8 = argv[i];
            const auto search_result = search_engine.search(*snapshot, request);
            if (!search_result.ok()) {
                std::cerr << "search failed: " << coredesk::to_string(search_result.error().code)
                          << " " << search_result.error().message << '\n';
                return 4;
            }

            const auto& response = search_result.value();
            std::cout << "query: " << request.query_utf8 << '\n'
                      << "hits: " << response.hits.size() << '\n'
                      << "from_cache: " << (response.from_cache ? "true" : "false") << '\n'
                      << "elapsed_us: " << response.elapsed.count() << '\n';
            for (const auto& hit : response.hits) {
                const auto pos = snapshot->id_to_pos.find(hit.id);
                if (pos == snapshot->id_to_pos.end()) {
                    continue;
                }
                const auto& record = snapshot->records[pos->second];
                std::cout << "  " << hit.id << " score=" << hit.score
                          << " path=" << record.relative_path << '\n';
            }
        }
        return 0;
    }

    print_usage();
    return argc == 1 ? 0 : 1;
}
