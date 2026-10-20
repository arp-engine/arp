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
    std::uint32_t line = 0;
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
            && std::filesystem::exists(current / "src")) {
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

bool isSourceFile(const std::filesystem::path& path)
{
    const auto ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".mm";
}

bool isAllowedAllocatorImplementation(const std::filesystem::path& relative)
{
    const auto path = relative.generic_string();
    return path == "src/core/cpu_allocator.h" || path == "src/core/cpu_allocator.cpp";
}

bool isLikelyDeleteDeclaration(const std::pmr::string& line)
{
    return line.find("= delete") != std::pmr::string::npos;
}

bool isQuotedJavaScriptSnippet(const std::pmr::string& line, const std::pmr::string& token)
{
    const auto tokenPos = line.find(token);
    if (tokenPos == std::pmr::string::npos) {
        return false;
    }
    const auto quotePos = line.find('"');
    return quotePos != std::pmr::string::npos && quotePos < tokenPos;
}

bool isIdentifierTail(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
        || ch == '_';
}

bool isIdentifierHead(char ch)
{
    return isIdentifierTail(ch);
}

bool containsForbiddenSymbol(const std::pmr::string& line, const std::pmr::string& token)
{
    std::size_t offset = 0;
    while ((offset = line.find(token, offset)) != std::pmr::string::npos) {
        const auto tail = offset + token.size();
        if (tail >= line.size() || !isIdentifierTail(line[tail])) {
            return true;
        }
        offset = tail;
    }
    return false;
}

bool containsForbiddenFunctionCall(const std::pmr::string& line, const std::pmr::string& name)
{
    const std::pmr::string token = name + "(";
    std::size_t offset = 0;
    while ((offset = line.find(token, offset)) != std::pmr::string::npos) {
        if (offset == 0u || !isIdentifierHead(line[offset - 1u])) {
            return true;
        }
        offset += token.size();
    }
    return false;
}

void scanFile(const std::filesystem::path& root,
    const std::filesystem::path& file,
    std::pmr::vector<Violation>& violations)
{
    const auto relative = std::filesystem::relative(file, root);
    if (isAllowedAllocatorImplementation(relative)) {
        return;
    }

    std::ifstream input(file);
    std::pmr::string line;
    std::uint32_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;

        const char* forbiddenContainers[] = {
            "std::vector",
            "std::string",
            "std::map",
            "std::unordered_map",
            "std::set",
            "std::unordered_set",
            "std::deque",
            "std::list",
            "std::forward_list",
            "std::basic_string",
        };
        for (const auto* token : forbiddenContainers) {
            if (containsForbiddenSymbol(line, token)) {
                violations.push_back({relative, lineNumber, token});
            }
        }

        const char* forbiddenStdAllocationHelpers[] = {
            "std::make_shared",
        };
        for (const auto* token : forbiddenStdAllocationHelpers) {
            if (containsForbiddenSymbol(line, token)) {
                violations.push_back({relative, lineNumber, token});
            }
        }

        const char* forbiddenAllocatorCalls[] = {
            "malloc",
            "calloc",
            "realloc",
            "free",
        };
        for (const auto* token : forbiddenAllocatorCalls) {
            if (containsForbiddenFunctionCall(line, token)) {
                violations.push_back({relative, lineNumber, token});
            }
        }
    }
}

void scanRoot(const std::filesystem::path& root,
    const std::filesystem::path& sourceRoot,
    std::pmr::vector<Violation>& violations)
{
    if (!std::filesystem::exists(sourceRoot)) {
        return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot)) {
        if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
            continue;
        }
        scanFile(root, entry.path(), violations);
    }
}

void printViolations(const std::pmr::vector<Violation>& violations)
{
    for (const auto& violation : violations) {
        std::cerr << violation.file.generic_string() << ":" << violation.line
                  << ": CPU allocation policy violation '" << violation.token << "'\n";
    }
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    std::pmr::vector<Violation> violations;
    scanRoot(root, root / "src", violations);
    scanRoot(root, root / "games", violations);
    scanRoot(root, root / "samples", violations);
    scanRoot(root, root / "tools", violations);
    printViolations(violations);
    assert(violations.empty());
    return 0;
}
