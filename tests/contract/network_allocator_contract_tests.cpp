#include "core/cpu_allocator.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

struct Violation {
    std::filesystem::path file;
    std::uint32_t line = 0;
    arp::core::pmr::String token;
};

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            return std::filesystem::current_path();
        }
        current = current.parent_path();
    }
#endif
}

bool isSourceFile(const std::filesystem::path& path)
{
    const auto ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".mm";
}

bool containsToken(const arp::core::pmr::String& line, const char* token)
{
    return line.find(token) != arp::core::pmr::String::npos;
}

bool containsDefaultStdString(const arp::core::pmr::String& line)
{
    return containsToken(line, "std::string ") || containsToken(line, "std::string&")
        || containsToken(line, "std::string*") || containsToken(line, "std::string(")
        || containsToken(line, "std::string{") || containsToken(line, "std::basic_string<char");
}

void scanFile(const std::filesystem::path& root,
    const std::filesystem::path& file,
    arp::core::pmr::Vector<Violation>& violations)
{
    std::ifstream input(file);
    arp::core::pmr::String line;
    std::uint32_t lineNumber = 0;
    const char* forbidden[] = {
        "ae::",
        "REPLACEMENT_OPERATOR_NEW_AND_DELETE",
        "_aligned_malloc",
        "std::vector<",
        "std::map<",
        "std::unordered_map<",
        "std::shared_ptr<",
        "std::unique_ptr<",
        "std::make_shared",
        "std::make_unique",
        "default_allocator",
        "SAFE_DELETE",
        "new ",
        "delete ",
    };
    while (std::getline(input, line)) {
        ++lineNumber;
        if (containsDefaultStdString(line)) {
            violations.push_back({
                std::filesystem::relative(file, root),
                lineNumber,
                arp::core::pmr::String("std::string", &arp::core::defaultCpuMemoryResource()),
            });
        }
        for (const auto* token : forbidden) {
            if (containsToken(line, token)) {
                violations.push_back({
                    std::filesystem::relative(file, root),
                    lineNumber,
                    arp::core::pmr::String(token, &arp::core::defaultCpuMemoryResource()),
                });
            }
        }
    }
}

} // namespace

int main()
{
    const auto root = projectRoot();
    const auto networkRoot = root / "src" / "network";
    assert(std::filesystem::exists(networkRoot));

    arp::core::pmr::Vector<Violation> violations{&arp::core::defaultCpuMemoryResource()};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(networkRoot)) {
        if (entry.is_regular_file() && isSourceFile(entry.path())) {
            scanFile(root, entry.path(), violations);
        }
    }

    for (const auto& violation : violations) {
        std::cerr << violation.file.generic_string() << ":" << violation.line
                  << ": network allocation/style violation " << violation.token << "\n";
    }
    assert(violations.empty());
    return 0;
}
