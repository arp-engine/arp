#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

struct Violation {
    std::filesystem::path file;
    std::uint32_t line = 0;
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

bool containsToken(const arp::core::pmr::String& line, const char* token)
{
    return line.find(token) != arp::core::pmr::String::npos;
}

bool isSourceFile(const std::filesystem::path& path)
{
    const auto ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".mm";
}

bool containsDefaultStdString(const arp::core::pmr::String& line)
{
    return containsToken(line, "std::string ") || containsToken(line, "std::string&")
        || containsToken(line, "std::string*") || containsToken(line, "std::string(")
        || containsToken(line, "std::string{") || containsToken(line, "std::basic_string<char");
}

void report(arp::core::pmr::Vector<Violation>& violations,
    const std::filesystem::path& file,
    std::uint32_t line,
    const char* message)
{
    violations.push_back({
        file,
        line,
        arp::core::pmr::String(message, &arp::core::defaultCpuMemoryResource()),
    });
}

void requirePath(const std::filesystem::path& root,
    const std::filesystem::path& relativePath,
    arp::core::pmr::Vector<Violation>& violations)
{
    if (!std::filesystem::exists(root / relativePath)) {
        report(violations, relativePath, 0, "required protocol path is missing");
    }
}

void requireCMakeSource(const arp::core::pmr::String& cmake,
    const std::filesystem::path& source,
    arp::core::pmr::Vector<Violation>& violations)
{
    const auto generic = source.generic_string();
    if (cmake.find(generic.c_str()) == arp::core::pmr::String::npos) {
        report(violations, "CMakeLists.txt", 0, generic.c_str());
    }
}

void scanNetworkAllocationStyle(const std::filesystem::path& root,
    const std::filesystem::path& file,
    arp::core::pmr::Vector<Violation>& violations)
{
    std::ifstream input(file);
    arp::core::pmr::String line{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t lineNumber = 0;
    const char* forbidden[] = {
        "ae::",
        "REPLACEMENT_OPERATOR_NEW_AND_DELETE",
        "_aligned_malloc",
        "std::vector<",
        "std::map<",
        "std::unordered_map<",
        "std::shared_ptr<",
        "std::unique_ptr<",
        "std::make_shared",
        "std::make_unique",
        "default_allocator",
        "SAFE_DELETE",
        "new ",
        "delete ",
    };
    while (std::getline(input, line)) {
        ++lineNumber;
        if (containsDefaultStdString(line)) {
            report(violations,
                std::filesystem::relative(file, root),
                lineNumber,
                "network allocation/style violation std::string");
        }
        for (const auto* token : forbidden) {
            if (containsToken(line, token)) {
                arp::core::pmr::String message(
                    "network allocation/style violation ", &arp::core::defaultCpuMemoryResource());
                message += token;
                violations.push_back({
                    std::filesystem::relative(file, root),
                    lineNumber,
                    message,
                });
            }
        }
    }
}

void scanPublicTlsHeader(const std::filesystem::path& root,
    const std::filesystem::path& file,
    arp::core::pmr::Vector<Violation>& violations)
{
    if (!std::filesystem::exists(file)) {
        return;
    }
    std::ifstream input(file);
    arp::core::pmr::String line{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (containsToken(line, "openssl/") || containsToken(line, "OpenSSL")
            || containsToken(line, "SSL_CTX") || containsToken(line, "SSL*")
            || containsToken(line, "X509")) {
            report(violations,
                std::filesystem::relative(file, root),
                lineNumber,
                "public TLS header must not expose OpenSSL native types");
        }
    }
}

} // namespace

int main()
{
    const auto root = projectRoot();
    const auto cmake = readText(root / "CMakeLists.txt");
    arp::core::pmr::Vector<Violation> violations{&arp::core::defaultCpuMemoryResource()};

    const std::filesystem::path requiredDirectories[] = {
        "src/network/http",
        "src/network/tls",
        "src/network/websocket",
    };
    const std::filesystem::path requiredFiles[] = {
        "src/network/http/uri.h",
        "src/network/http/uri.cpp",
        "src/network/http/header.h",
        "src/network/http/header.cpp",
        "src/network/http/message.h",
        "src/network/http/message.cpp",
        "src/network/http/parser.h",
        "src/network/http/parser.cpp",
        "src/network/http/serializer.h",
        "src/network/http/serializer.cpp",
        "src/network/http/session.h",
        "src/network/http/session.cpp",
        "src/network/http/client.h",
        "src/network/http/client.cpp",
        "src/network/tls/tls_config.h",
        "src/network/tls/tls_context.h",
        "src/network/tls/tls_context.cpp",
        "src/network/tls/ssl_stream.h",
        "src/network/tls/ssl_stream.cpp",
        "src/network/websocket/diagnostic.h",
        "src/network/websocket/frame.h",
        "src/network/websocket/frame.cpp",
        "src/network/websocket/handshake.h",
        "src/network/websocket/handshake.cpp",
        "src/network/websocket/stream.h",
        "src/network/websocket/stream.cpp",
        "src/network/websocket/client.h",
        "src/network/websocket/client.cpp",
    };
    const std::filesystem::path requiredSources[] = {
        "src/network/http/uri.cpp",
        "src/network/http/header.cpp",
        "src/network/http/message.cpp",
        "src/network/http/parser.cpp",
        "src/network/http/serializer.cpp",
        "src/network/http/session.cpp",
        "src/network/http/client.cpp",
        "src/network/tls/tls_context.cpp",
        "src/network/tls/ssl_stream.cpp",
        "src/network/websocket/frame.cpp",
        "src/network/websocket/handshake.cpp",
        "src/network/websocket/stream.cpp",
        "src/network/websocket/client.cpp",
    };

    for (const auto& directory : requiredDirectories) {
        requirePath(root, directory, violations);
    }
    for (const auto& file : requiredFiles) {
        requirePath(root, file, violations);
    }
    for (const auto& source : requiredSources) {
        requireCMakeSource(cmake, source, violations);
    }

    const auto tlsRoot = root / "src" / "network" / "tls";
    for (const auto& header : {"tls_config.h", "tls_context.h", "ssl_stream.h"}) {
        scanPublicTlsHeader(root, tlsRoot / header, violations);
    }

    const auto networkRoot = root / "src" / "network";
    assert(std::filesystem::exists(networkRoot));
    for (const auto& entry : std::filesystem::recursive_directory_iterator(networkRoot)) {
        if (entry.is_regular_file() && isSourceFile(entry.path())) {
            scanNetworkAllocationStyle(root, entry.path(), violations);
        }
    }

    for (const auto& violation : violations) {
        std::cerr << violation.file.generic_string();
        if (violation.line != 0) {
            std::cerr << ":" << violation.line;
        }
        std::cerr << ": " << violation.message << "\n";
    }
    assert(violations.empty());
    return 0;
}
