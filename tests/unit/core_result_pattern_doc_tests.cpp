#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src" / "core")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
}

bool containsText(const std::string& text, const char* expected)
{
    return text.find(expected) != std::string::npos;
}

} // namespace

int main()
{
    const auto doc = readText(projectRoot() / "docs" / "core-result-status-pattern.md");
    assert(!doc.empty());
    if (!containsText(doc, "Status: Prototype Complete"))
        return 1;
    if (!containsText(doc, "docs-only decision"))
        return 2;
    if (!containsText(doc, "Do not add `src/core/result.h` in this phase."))
        return 3;
    if (!containsText(doc, "OOP result classes"))
        return 4;
    if (!containsText(doc, "plain status structs"))
        return 5;
    if (!containsText(doc, "core::pmr"))
        return 6;
    if (!containsText(doc, "deterministic diagnostics"))
        return 7;
    if (!containsText(doc, "single low-risk call site"))
        return 8;
    return 0;
}
