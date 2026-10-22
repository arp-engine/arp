#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct LinkViolation {
    std::filesystem::path file;
    std::pmr::string target;
};

struct CMakeCall {
    std::filesystem::path file;
    std::pmr::vector<std::pmr::string> tokens;
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

bool isTokenBoundary(char ch)
{
    return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_');
}

bool isCMakeCommandAt(
    const std::pmr::string& text, size_t position, const std::pmr::string& command)
{
    if (text.compare(position, command.size(), command) != 0) {
        return false;
    }
    const bool leftOk = position == 0 || isTokenBoundary(text[position - 1]);
    const auto after = position + command.size();
    const bool rightOk = after >= text.size() || isTokenBoundary(text[after]);
    return leftOk && rightOk;
}

std::pmr::vector<std::pmr::string> tokenizeCMakeArguments(const std::pmr::string& body)
{
    std::pmr::vector<std::pmr::string> tokens;
    std::pmr::string token;
    bool inQuote = false;
    bool inComment = false;

    for (char ch : body) {
        if (inComment) {
            if (ch == '\n') {
                inComment = false;
            }
            continue;
        }

        if (!inQuote && ch == '#') {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            inComment = true;
            continue;
        }

        if (ch == '"') {
            inQuote = !inQuote;
            continue;
        }

        if (!inQuote && std::isspace(static_cast<unsigned char>(ch))) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            continue;
        }

        token.push_back(ch);
    }

    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

class CMakeTargetLinkScanner {
public:
    std::pmr::vector<CMakeCall> scanFile(const std::filesystem::path& file) const
    {
        const auto text = readText(file);
        std::pmr::vector<CMakeCall> calls;
        size_t position = 0;
        while ((position = text.find(command_, position)) != std::pmr::string::npos) {
            if (!isCMakeCommandAt(text, position, command_)) {
                position += command_.size();
                continue;
            }

            const auto open = text.find('(', position + command_.size());
            if (open == std::pmr::string::npos) {
                break;
            }

            int depth = 1;
            size_t cursor = open + 1;
            while (cursor < text.size() && depth > 0) {
                if (text[cursor] == '(') {
                    ++depth;
                } else if (text[cursor] == ')') {
                    --depth;
                }
                ++cursor;
            }

            if (depth == 0) {
                const auto body = text.substr(open + 1, cursor - open - 2);
                calls.push_back({file, tokenizeCMakeArguments(body)});
            }
            position = cursor;
        }
        return calls;
    }

private:
    const std::pmr::string command_ = "target_link_libraries";
};

class RhiLegacyLinkContract {
public:
    std::pmr::vector<LinkViolation> evaluate(const std::filesystem::path& root) const
    {
        std::pmr::vector<LinkViolation> violations;
        const CMakeTargetLinkScanner scanner;
        for (const auto& file : cmakeContractFiles(root)) {
            for (const auto& call : scanner.scanFile(file)) {
                collectViolation(call, violations);
            }
        }
        return violations;
    }

private:
    std::pmr::vector<std::filesystem::path> cmakeContractFiles(
        const std::filesystem::path& root) const
    {
        std::pmr::vector<std::filesystem::path> files;
        files.push_back(root / "CMakeLists.txt");

        const auto cmakeDir = root / "cmake";
        if (!std::filesystem::exists(cmakeDir)) {
            return files;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(cmakeDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".cmake") {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    void collectViolation(const CMakeCall& call, std::pmr::vector<LinkViolation>& violations) const
    {
        if (call.tokens.empty()) {
            return;
        }

        const auto& target = call.tokens.front();
        if (forbiddenTargets_.find(target) == forbiddenTargets_.end()) {
            return;
        }

        for (size_t index = 1; index < call.tokens.size(); ++index) {
            if (call.tokens[index] == forbiddenLibrary_) {
                violations.push_back({call.file, target});
                return;
            }
        }
    }

    const std::pmr::set<std::pmr::string> forbiddenTargets_ = {
        "arp_rhi_l1",
        "arp_rhi_vulkan",
        "arp_rhi_metal",
        "arp_rhi_webgpu",
    };
    const std::pmr::string forbiddenLibrary_ = "arp_rhi";
};

void printViolations(
    const std::filesystem::path& root, const std::pmr::vector<LinkViolation>& violations)
{
    for (const auto& violation : violations) {
        std::cerr << std::filesystem::relative(violation.file, root).generic_string() << ": "
                  << violation.target << " must not link legacy library arp_rhi\n";
    }
}

} // namespace

int main()
{
    const auto root = findProjectRoot();
    const RhiLegacyLinkContract contract;
    const auto violations = contract.evaluate(root);
    printViolations(root, violations);
    assert(violations.empty());
    return 0;
}
