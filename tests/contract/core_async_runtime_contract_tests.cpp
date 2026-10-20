#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src" / "core")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            return std::filesystem::current_path();
        }
        current = current.parent_path();
    }
#endif
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool containsText(const std::string& text, const char* expected)
{
    return text.find(expected) != std::string::npos;
}

bool requireText(const std::string& text, const char* expected, int code)
{
    if (containsText(text, expected)) {
        return true;
    }
    std::cerr << "docs/async-runtime.md: missing contract text: " << expected << "\n";
    return false;
}

} // namespace

int main()
{
    const auto doc = readText(projectRoot() / "docs" / "async-runtime.md");
    assert(!doc.empty());

    if (!requireText(doc, "Status: Contract Complete", 1))
        return 1;
    if (!requireText(doc, "one completion dispatch surface", 2))
        return 2;
    if (!requireText(doc, "WorkService", 4))
        return 4;
    if (!requireText(doc, "FileService", 6))
        return 6;
    if (!requireText(doc, "callbacks run only through explicit AsyncRuntime dispatch", 7))
        return 7;
    if (!requireText(doc, "dispatch budget", 8))
        return 8;
    if (!requireText(doc, "shutdown submission", 9))
        return 9;
    if (!requireText(doc, "queue overflow", 10))
        return 10;
    if (!requireText(doc, "cancelled request", 11))
        return 11;
    if (!requireText(doc, "backend unavailable", 12))
        return 12;
    if (!requireText(doc, "Domain capability/status reporting", 13))
        return 13;
    if (!requireText(doc, "no blocking IO", 14))
        return 14;
    if (!requireText(doc, "no unbounded allocation", 15))
        return 15;
    if (!requireText(doc, "no hidden native handle exposure", 16))
        return 16;
    if (!requireText(doc, "Smoke path: initialize, post, dispatch, shutdown", 17))
        return 17;
    return 0;
}
