#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view kVersion = "CoreDesk 1.0.0";

void print_usage()
{
    std::cout << "Usage: coredesk_cli --version\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << kVersion << '\n';
        return 0;
    }

    print_usage();
    return argc == 1 ? 0 : 1;
}
