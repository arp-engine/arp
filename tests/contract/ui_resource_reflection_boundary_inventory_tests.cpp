#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#undef assert
#define assert(condition)                                                                          \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ":" << __LINE__ << " assertion failed: " << #condition        \
                      << "\n";                                                                     \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

namespace {

struct SymbolOccurrence {
    const char* symbol;
    std::string filePath;
    std::size_t lineNumber;
};

arp::core::pmr::String readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    arp::core::pmr::String content(&arp::core::defaultCpuMemoryResource());
    if (!input.good()) {
        return content;
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    content.resize(static_cast<std::size_t>(size));
    input.read(content.data(), size);
    return content;
}

std::vector<SymbolOccurrence> findOccurrences(const std::filesystem::path& root, const char* symbol)
{
    std::vector<SymbolOccurrence> result;
    const auto symbolLen = std::strlen(symbol);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& path = entry.path();
        const auto ext = path.extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp") {
            continue;
        }
        const auto content = readFile(path);
        std::size_t pos = 0;
        std::size_t lineNumber = 1;
        std::size_t lastNewline = 0;
        while ((pos = content.find(symbol, pos)) != arp::core::pmr::String::npos) {
            while (lastNewline < pos) {
                if (content[lastNewline] == '\n') {
                    ++lineNumber;
                }
                ++lastNewline;
            }
            const auto relative = std::filesystem::relative(path, root).generic_string();
            result.push_back({symbol, relative.c_str(), lineNumber});
            ++pos;
        }
    }
    return result;
}

bool pathMatches(const std::string& path, const char* suffix)
{
    const auto suffixLen = std::strlen(suffix);
    return path.size() >= suffixLen
        && std::strcmp(path.c_str() + path.size() - suffixLen, suffix) == 0;
}

} // namespace

int main()
{
    const std::filesystem::path sourceRoot = ARP_SOURCE_DIR;
    const auto srcRoot = sourceRoot / "src";

    // Inventory loadFromEnvelope production callers.
    {
        const auto occurrences = findOccurrences(srcRoot, "loadFromEnvelope(");
        std::size_t declarationCount = 0;
        std::size_t internalCount = 0;
        std::size_t productionCallerCount = 0;
        for (const auto& occ : occurrences) {
            if (pathMatches(occ.filePath, "resource/resource_manager.h")) {
                ++declarationCount;
            } else if (pathMatches(occ.filePath, "resource/resource_manager.cpp")) {
                ++internalCount;
            } else if (pathMatches(occ.filePath, "resource/internal_asset_package.cpp")) {
                ++productionCallerCount;
            } else if (pathMatches(occ.filePath, "resource/prefab_asset_resources.cpp")
                || pathMatches(occ.filePath, "resource/prefab_asset_resources.h")) {
                // Quarantined compatibility wrapper for legacy envelope loading.
                ++internalCount;
            } else {
                std::cerr << "Unexpected loadFromEnvelope occurrence: " << occ.filePath << ":"
                          << occ.lineNumber << "\n";
                assert(false);
            }
        }
        assert(declarationCount == 1);
        assert(internalCount == 5);
        assert(productionCallerCount == 1);
        assert(occurrences.size() == 7);
    }

    // Inventory loadObjectFromEnvelope production callers.
    {
        const auto occurrences = findOccurrences(srcRoot, "loadObjectFromEnvelope(");
        std::size_t declarationCount = 0;
        std::size_t definitionCount = 0;
        std::size_t productionCallerCount = 0;
        for (const auto& occ : occurrences) {
            if (pathMatches(occ.filePath, "resource/resource_manager.h")) {
                ++declarationCount;
            } else if (pathMatches(occ.filePath, "resource/resource_manager.cpp")) {
                ++definitionCount;
            } else {
                std::cerr << "Unexpected loadObjectFromEnvelope occurrence: " << occ.filePath << ":"
                          << occ.lineNumber << "\n";
                assert(false);
            }
        }
        assert(declarationCount == 1);
        assert(definitionCount == 1);
        assert(productionCallerCount == 0);
        assert(occurrences.size() == 2);
    }

    // Inventory ResourceRecord::value production usage.
    {
        const auto occurrences = findOccurrences(srcRoot, "record->value.cast");
        std::size_t resourceManagerFallbackCount = 0;
        for (const auto& occ : occurrences) {
            if (pathMatches(occ.filePath, "resource/resource_manager.h")) {
                ++resourceManagerFallbackCount;
            } else {
                std::cerr << "Unexpected record->value.cast occurrence: " << occ.filePath << ":"
                          << occ.lineNumber << "\n";
                assert(false);
            }
        }
        assert(resourceManagerFallbackCount == 0);
        assert(occurrences.size() == 0);
    }

    // Inventory ResourceRecord::ownedObjects production usage.
    {
        const auto occurrences = findOccurrences(srcRoot, "ownedObjects");
        std::size_t resourceManagerCount = 0;
        std::size_t reflectionCount = 0;
        for (const auto& occ : occurrences) {
            if (pathMatches(occ.filePath, "resource/resource_manager.h")
                || pathMatches(occ.filePath, "resource/resource_manager.cpp")) {
                ++resourceManagerCount;
            } else if (pathMatches(occ.filePath, "reflection/serialization.h")
                || pathMatches(occ.filePath, "reflection/serialization.cpp")) {
                ++reflectionCount;
            } else {
                std::cerr << "Unexpected ownedObjects occurrence: " << occ.filePath << ":"
                          << occ.lineNumber << "\n";
                assert(false);
            }
        }
        assert(resourceManagerCount >= 1);
        assert(reflectionCount >= 1);
        assert(occurrences.size() == resourceManagerCount + reflectionCount);
    }

    std::cout
        << "ui_resource_reflection_boundary_inventory: all boundaries match known inventory\n";
    return 0;
}
