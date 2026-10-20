#include "core/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Violation {
    std::filesystem::path file;
    std::pmr::string token;
};

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

void requireText(const std::filesystem::path& file,
    const std::pmr::string& text,
    const std::pmr::string& token,
    std::pmr::vector<Violation>& violations)
{
    if (text.find(token) == std::pmr::string::npos) {
        violations.push_back({file, std::pmr::string("missing required token: ") + token});
    }
}

void forbidText(const std::filesystem::path& file,
    const std::pmr::string& text,
    const std::pmr::string& token,
    std::pmr::vector<Violation>& violations)
{
    if (text.find(token) != std::pmr::string::npos) {
        violations.push_back({file, token});
    }
}

void printViolations(
    const std::filesystem::path& root, const std::pmr::vector<Violation>& violations)
{
    for (const auto& violation : violations) {
        std::cerr << std::filesystem::relative(violation.file, root).generic_string()
                  << ": forbidden backend token '" << violation.token << "'\n";
    }
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    std::pmr::vector<Violation> violations;

    const auto rhiDesignPath = root / "docs" / "rhi-v2-layered-design.md";
    const auto rhiDesignText = readText(rhiDesignPath);
    const char* requiredDesignTokens[] = {
        "RHI backend targets own their runtime boundaries after the removed `src/"
        "rhi` module.",
        "Migrated RHI startup never redirects to removed v1 device or command-list paths; backend creation either succeeds, fails with deterministic diagnostics, or uses an explicit test-only Null path.",
        "The removed `src/"
        "rhi` module is not a production compatibility route.",
    };
    for (const auto* token : requiredDesignTokens) {
        requireText(rhiDesignPath, rhiDesignText, token, violations);
    }

    const auto roadmapPath = root / "docs" / "roadmap.md";
    const auto roadmapText = readText(roadmapPath);
    const char* requiredRoadmapTokens[] = {
        "Migrated RHI paths do not list removed v1 fallback as a supported compatibility route.",
        "RHI backend targets remain build-graph and source-graph independent after `src/"
        "rhi` removal.",
        "No production startup path selects a removed v1 backend route.",
    };
    for (const auto* token : requiredRoadmapTokens) {
        requireText(roadmapPath, roadmapText, token, violations);
    }

    const std::filesystem::path guardedDocs[] = {
        rhiDesignPath,
        roadmapPath,
    };
    const char* forbiddenLegacyFallbackClaims[] = {
        "RHI falls back to legacy RHI",
        "RHI can fall back to legacy RHI",
        "fall back to legacy RHI",
        "v1 fallback path",
        "v1 render path available as a fallback",
        "Existing `src/"
        "rhi` remains the v1 compatibility path",
        "Legacy `src/"
        "rhi` remains the production compatibility path",
        "legacy RHI fallback as a supported compatibility route for migrated paths",
    };
    for (const auto& docPath : guardedDocs) {
        const auto text = readText(docPath);
        for (const auto* token : forbiddenLegacyFallbackClaims) {
            forbidText(docPath, text, token, violations);
        }
    }

    printViolations(root, violations);
    assert(violations.empty());
    return 0;
}
