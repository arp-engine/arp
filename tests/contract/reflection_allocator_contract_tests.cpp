#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

struct Violation {
    std::filesystem::path file;
    std::uint32_t line = 0;
    arp::core::pmr::String token;
    arp::core::pmr::String category;
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

void scanFile(const std::filesystem::path& root,
    const std::filesystem::path& file,
    arp::core::pmr::Vector<Violation>& violations)
{
    std::ifstream input(file);
    arp::core::pmr::String line;
    std::uint32_t lineNumber = 0;

    struct ForbiddenToken {
        const char* token = nullptr;
        const char* category = nullptr;
    };

    const ForbiddenToken forbidden[] = {
        {"ae::hybrid", "allocation/style"},
        {"REPLACEMENT_OPERATOR_NEW_AND_DELETE", "allocation/style"},
        {"_aligned_malloc", "allocation/style"},
        {"std::allocator<", "allocation/style"},
        {"std::allocator_traits<std::allocator", "allocation/style"},
        {"std::make_shared", "allocation/style"},
        {"std::make_unique", "allocation/style"},
        {"new ", "allocation/style"},
        {"delete ", "allocation/style"},
        {"ReflectionStatus", "module boundary"},
        {"statusLabel(", "module boundary"},
    };
    while (std::getline(input, line)) {
        ++lineNumber;
        for (const auto& forbiddenToken : forbidden) {
            if (containsToken(line, forbiddenToken.token)) {
                violations.push_back({
                    std::filesystem::relative(file, root),
                    lineNumber,
                    arp::core::pmr::String(forbiddenToken.token),
                    arp::core::pmr::String(forbiddenToken.category),
                });
            }
        }
    }
}

} // namespace

int main()
{
    const auto root = projectRoot();
    const auto reflectionRoot = root / "src" / "reflection";
    assert(std::filesystem::exists(reflectionRoot));

    arp::core::pmr::Vector<Violation> violations;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(reflectionRoot)) {
        if (entry.is_regular_file() && isSourceFile(entry.path())) {
            scanFile(root, entry.path(), violations);
        }
    }

    for (const auto& violation : violations) {
        std::cerr << violation.file.generic_string() << ":" << violation.line << ": reflection "
                  << violation.category << " violation " << violation.token << "\n";
    }
    assert(violations.empty());
    return 0;
}
