#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

struct Violation {
    std::filesystem::path file;
    arp::core::pmr::String message;
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

arp::core::pmr::String readText(const std::filesystem::path& file)
{
    std::ifstream input(file);
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String line{&arp::core::defaultCpuMemoryResource()};
    while (std::getline(input, line)) {
        text += line;
        text += '\n';
    }
    return text;
}

void scanForbiddenNetworkDependency(const std::filesystem::path& root,
    const std::filesystem::path& scanRoot,
    arp::core::pmr::Vector<Violation>& violations)
{
    if (!std::filesystem::exists(scanRoot)) {
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(scanRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        const auto text = readText(entry.path());
        if (text.find("\"network/") != arp::core::pmr::String::npos
            || text.find("<network/") != arp::core::pmr::String::npos
            || text.find("arp::network") != arp::core::pmr::String::npos
            || text.find("socket(") != arp::core::pmr::String::npos
            || text.find("WinHttp") != arp::core::pmr::String::npos
            || text.find("curl_") != arp::core::pmr::String::npos) {
            violations.push_back({
                std::filesystem::relative(entry.path(), root),
                arp::core::pmr::String("render/scene/RHI hot-path layer must not depend on network",
                    &arp::core::defaultCpuMemoryResource()),
            });
        }
    }
}

void requireText(const std::filesystem::path& file,
    const arp::core::pmr::String& text,
    const char* token,
    arp::core::pmr::Vector<Violation>& violations)
{
    if (text.find(token) == arp::core::pmr::String::npos) {
        violations.push_back({
            file,
            arp::core::pmr::String(token, &arp::core::defaultCpuMemoryResource()),
        });
    }
}

void requireProtocolStatusDocument(
    const std::filesystem::path& root, arp::core::pmr::Vector<Violation>& violations)
{
    const auto document = root / "docs" / "network-protocols.md";
    if (!std::filesystem::exists(document)) {
        violations.push_back({
            std::filesystem::relative(document, root),
            arp::core::pmr::String("network protocol status document is missing",
                &arp::core::defaultCpuMemoryResource()),
        });
        return;
    }
    const auto text = readText(document);
    const auto relative = std::filesystem::relative(document, root);
    requireText(relative,
        text,
        "TCP socket/event loop -> optional TLS stream -> HTTP session -> WebSocket upgrade/frame stream",
        violations);
    requireText(relative, text, "Contract Complete", violations);
    requireText(relative, text, "Prototype Complete", violations);
    requireText(relative, text, "Backend Validated", violations);
    requireText(relative, text, "Platform Validated", violations);
    requireText(relative, text, "Production Ready", violations);
    requireText(relative,
        text,
        "TLS disabled/unavailable paths fail diagnostically and never downgrade to plaintext",
        violations);
    requireText(relative, text, "HTTP", violations);
    requireText(relative, text, "TLS/HTTPS", violations);
    requireText(relative, text, "WebSocket", violations);
    requireText(relative, text, "WSS", violations);
}

} // namespace

int main()
{
    const auto root = projectRoot();
    const std::filesystem::path forbiddenRoots[] = {
        root / "src" / "scene",
        root / "src" / "input",
        root / "src" / "ui",
        root / "src" / "2d",
        root / "src" / "3d",
        root / "src" / "renderer",
        root / "src" / "render_graph",
        root / "src" / "rhi",
    };

    arp::core::pmr::Vector<Violation> violations{&arp::core::defaultCpuMemoryResource()};
    requireProtocolStatusDocument(root, violations);
    for (const auto& scanRoot : forbiddenRoots) {
        scanForbiddenNetworkDependency(root, scanRoot, violations);
    }

    for (const auto& violation : violations) {
        std::cerr << violation.file.generic_string() << ": " << violation.message << "\n";
    }
    assert(violations.empty());
    return 0;
}
