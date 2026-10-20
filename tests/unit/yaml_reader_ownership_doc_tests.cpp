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
            && std::filesystem::exists(current / "src" / "resource")) {
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
    const auto doc = readText(projectRoot() / "docs" / "yaml-reader-ownership.md");
    assert(!doc.empty());
    if (!containsText(doc, "Status: Prototype Complete"))
        return 1;
    if (!containsText(doc, "Do not move YAML helpers into `src/core`."))
        return 2;
    if (!containsText(doc, "src/resource/yaml_reader.h"))
        return 3;
    if (!containsText(doc, "required string"))
        return 4;
    if (!containsText(doc, "optional string"))
        return 5;
    if (!containsText(doc, "number"))
        return 6;
    if (!containsText(doc, "integer"))
        return 7;
    if (!containsText(doc, "bool"))
        return 8;
    if (!containsText(doc, "mapping"))
        return 9;
    if (!containsText(doc, "sequence"))
        return 10;
    if (!containsText(doc, "deterministic diagnostics"))
        return 11;
    if (!containsText(doc, "fkYAML"))
        return 12;
    return 0;
}
