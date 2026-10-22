#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path findProjectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "TODO.md")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
#endif
}

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

bool contains(const std::pmr::string& text, const std::pmr::string& token)
{
    return text.find(token) != std::pmr::string::npos;
}

void requireToken(const std::pmr::string& text, const std::pmr::string& token)
{
    assert(contains(text, token));
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    const std::filesystem::path docs[] = {
        root / "TODO.md",
        root / "docs" / "rhi-v2-layered-design.md",
        root / "docs" / "roadmap.md",
    };
    const char* requiredTokens[] = {
        "Production Ready remains blocked by Pending Confirmation / External Validation.",
        "Production Ready is not claimed",
        "macOS Metal runtime",
        "broader hardware",
    };

    for (const auto& doc : docs) {
        const auto text = readText(doc);
        for (const auto* token : requiredTokens) {
            requireToken(text, token);
        }
    }
    return 0;
}
