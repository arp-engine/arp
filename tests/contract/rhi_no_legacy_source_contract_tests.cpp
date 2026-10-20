#include "core/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SourceViolation {
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
            && std::filesystem::exists(current / "src" / "rhi")) {
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
    assert(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

bool isSourceFile(const std::filesystem::path& path)
{
    const auto ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cc" || ext == ".cxx"
        || ext == ".mm";
}

std::pmr::string makeIncludePrefix(
    char introducer, const char* packagePrefix, const char* packageSuffix)
{
    std::pmr::string prefix(1, introducer);
    prefix += packagePrefix;
    prefix += packageSuffix;
    prefix += "/";
    return prefix;
}

class RhiSourceBoundaryContract {
public:
    std::pmr::vector<SourceViolation> evaluate(const std::filesystem::path& root) const
    {
        std::pmr::vector<SourceViolation> violations;
        rejectForbiddenRoots(root, violations);
        for (const auto& module : sourceScanRoots_) {
            scanExistingModule(root / module, violations);
        }
        for (const auto& scope : includePrefixScanRoots_) {
            scanIncludePrefixScope(root / scope, violations);
        }
        return violations;
    }

private:
    void rejectForbiddenRoots(
        const std::filesystem::path& root, std::pmr::vector<SourceViolation>& violations) const
    {
        for (const auto& packageRoot : forbiddenPackageRoots_) {
            const auto path = root / packageRoot;
            if (std::filesystem::exists(path)) {
                violations.push_back({path, "forbidden RHI legacy package root"});
            }
        }
    }

    void scanExistingModule(const std::filesystem::path& moduleRoot,
        std::pmr::vector<SourceViolation>& violations) const
    {
        if (!std::filesystem::exists(moduleRoot)) {
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(moduleRoot)) {
            if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
                continue;
            }
            scanFile(entry.path(), violations);
        }
    }

    void scanIncludePrefixScope(
        const std::filesystem::path& scopeRoot, std::pmr::vector<SourceViolation>& violations) const
    {
        if (!std::filesystem::exists(scopeRoot)) {
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(scopeRoot)) {
            if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
                continue;
            }
            scanIncludePrefixFile(entry.path(), violations);
        }
    }

    void scanFile(
        const std::filesystem::path& file, std::pmr::vector<SourceViolation>& violations) const
    {
        const auto text = readText(file);
        for (const auto* token : forbiddenTokens_) {
            if (text.find(token) != std::pmr::string::npos) {
                violations.push_back({file, token});
            }
        }
    }

    void scanIncludePrefixFile(
        const std::filesystem::path& file, std::pmr::vector<SourceViolation>& violations) const
    {
        const auto text = readText(file);
        for (const auto& token : forbiddenIncludePrefixes_) {
            if (text.find(token) != std::pmr::string::npos) {
                violations.push_back({file, token});
            }
        }
    }

    const std::pmr::vector<std::filesystem::path> sourceScanRoots_ = {
        std::filesystem::path("src") / "core",
        std::filesystem::path("src") / "platform",
        std::filesystem::path("src") / "renderer",
        std::filesystem::path("src") / "rhi",
        std::filesystem::path("src") / "rhi_l1",
        std::filesystem::path("src") / "rhi_vulkan",
        std::filesystem::path("src") / "rhi_metal",
        std::filesystem::path("src") / "rhi_webgpu",
        "samples",
        "games",
        "tools",
    };

    const std::pmr::vector<std::filesystem::path> forbiddenPackageRoots_ = {
        std::filesystem::path("src") / "rhi" / "l1",
        std::filesystem::path("src") / "rhi_l1",
        std::filesystem::path("src") / "rhi_vulkan",
        std::filesystem::path("src") / "rhi_metal",
        std::filesystem::path("src") / "rhi_webgpu",
    };

    const std::pmr::vector<std::filesystem::path> includePrefixScanRoots_ = {
        "src",
        "tests",
        "games",
        "samples",
    };

    const std::pmr::vector<const char*> forbiddenTokens_ = {
        "#include \"rhi/",
        "#include <"
        "rhi/",
        "src/"
        "rhi/",
        "arp::rhi"
        "::",
        "rhi"
        "::",
        "--legacy-rhi-fallback",
        "legacy-rhi-fallback",
        "legacyRhiFallback",
        "legacyRhiCompatibility",
        "legacyRhiHotPath",
        "usedLegacyRhi",
        "guiUsedLegacyRhi",
        "LegacyRhi",
        "LegacyRhiFallback",
        "markLegacyRhiFallback",
        "legacy_rhi_compatibility_fallback",
        "LegacyRhiCompatibilityFallback",
        "legacy RHI fallback",
        "legacy RHI",
    };

    const std::pmr::vector<std::pmr::string> forbiddenIncludePrefixes_ = {
        makeIncludePrefix('"', "rhi/", "l1"),
        makeIncludePrefix('<', "rhi/", "l1"),
        makeIncludePrefix('"', "rhi_", "l1"),
        makeIncludePrefix('<', "rhi_", "l1"),
        makeIncludePrefix('"', "rhi_", "vulkan"),
        makeIncludePrefix('<', "rhi_", "vulkan"),
        makeIncludePrefix('"', "rhi_", "metal"),
        makeIncludePrefix('<', "rhi_", "metal"),
        makeIncludePrefix('"', "rhi_", "webgpu"),
        makeIncludePrefix('<', "rhi_", "webgpu"),
    };
};

void printViolations(
    const std::filesystem::path& root, const std::pmr::vector<SourceViolation>& violations)
{
    for (const auto& violation : violations) {
        std::cerr << std::filesystem::relative(violation.file, root).generic_string()
                  << ": forbidden legacy RHI source token '" << violation.token << "'\n";
    }
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    const RhiSourceBoundaryContract contract;
    const auto violations = contract.evaluate(root);
    printViolations(root, violations);
    assert(violations.empty());
    return 0;
}
