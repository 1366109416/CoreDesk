#include "coredesk/common/Cancellation.h"
#include "coredesk/filesystem/FileScanner.h"

#include <iostream>
#include <filesystem>
#include <string_view>

namespace {

constexpr std::string_view kVersion = "CoreDesk 1.0.0";

void print_usage()
{
    std::cout << "Usage: coredesk_cli --version\n"
              << "       coredesk_cli scan <root>\n";
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

        const auto& output = result.value();
        std::cout << "root: " << output.root << '\n'
                  << "records: " << output.records.size() << '\n'
                  << "discovered: " << output.stats.discovered << '\n'
                  << "processed: " << output.stats.processed << '\n'
                  << "skipped: " << output.stats.skipped << '\n'
                  << "failed: " << output.stats.failed << '\n'
                  << "elapsed_ms: " << output.elapsed.count() << '\n';
        return 0;
    }

    print_usage();
    return argc == 1 ? 0 : 1;
}
