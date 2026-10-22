#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

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
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cc" || ext == ".cxx"
        || ext == ".mm" || ext == ".inl" || ext == ".ipp" || ext == ".inc";
}

bool containsWithoutWhitespace(std::string_view line, std::string_view token)
{
    std::size_t tokenIndex = 0;
    for (const char character : line) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            continue;
        }
        if (tokenIndex == token.size() || character != token[tokenIndex]) {
            tokenIndex = character == token.front() ? 1u : 0u;
            continue;
        }
        ++tokenIndex;
        if (tokenIndex == token.size()) {
            return true;
        }
    }
    return false;
}

void scanFile(const std::filesystem::path& root,
    const std::filesystem::path& file,
    arp::core::pmr::Vector<Violation>& violations)
{
    std::ifstream input(file);
    arp::core::pmr::String line(&arp::core::defaultCpuMemoryResource());
    std::uint32_t lineNumber = 0;
    const std::string_view forbidden[] = {
        "#include<vector>",
        "std::vector",
        "std::pmr::vector",
        "#include<deque>",
        "std::deque",
        "std::pmr::deque",
        "std::make_unique",
        "newAsyncReadbackState",
        "newProductionReadbackState",
        "deletestate",
    };
    while (std::getline(input, line)) {
        ++lineNumber;
        for (const auto token : forbidden) {
            if (containsWithoutWhitespace(line, token)) {
                violations.push_back({
                    std::filesystem::relative(file, root),
                    lineNumber,
                    arp::core::pmr::String(
                        token.data(), token.size(), &arp::core::defaultCpuMemoryResource()),
                });
            }
        }
    }
}

} // namespace

int main()
{
    const auto root = projectRoot();
    const auto webGpuRoot = root / "src" / "rhi" / "backends" / "webgpu";
    const auto webGpuTelemetryHeader =
        root / "src" / "rhi" / "semantics" / "webgpu_runtime_telemetry.h";
    assert(std::filesystem::exists(webGpuRoot));
    assert(std::filesystem::exists(webGpuTelemetryHeader));

    arp::core::pmr::Vector<Violation> violations{
        &arp::core::defaultCpuMemoryResource(),
    };
    for (const auto& entry : std::filesystem::recursive_directory_iterator(webGpuRoot)) {
        if (entry.is_regular_file() && isSourceFile(entry.path())) {
            scanFile(root, entry.path(), violations);
        }
    }
    scanFile(root, webGpuTelemetryHeader, violations);

    for (const auto& violation : violations) {
        std::cerr << violation.file.generic_string() << ":" << violation.line
                  << ": WebGPU allocator contract forbids " << violation.token << "\n";
    }
    assert(violations.empty());
    return 0;
}
