#include "core/io/filesystem_path.h"
#include "core/text/ascii_token_cursor.h"
#include "scriptvm/aot/aot_package.h"
#include "tools/script_aotc/aot_cli_sources.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string_view>

namespace {

void printDiagnostic(const arp::scriptvm::ScriptDiagnostic& diagnostic)
{
    if (!diagnostic.file.empty())
        std::fprintf(
            stderr, "%s:%d:%d: ", diagnostic.file.c_str(), diagnostic.line, diagnostic.column);
    std::fprintf(stderr, "%s\n", diagnostic.message.c_str());
}

void usage()
{
    std::fprintf(stderr,
        "usage: arp_script_aotc <validate|compile|package|report> "
        "<manifest.yaml> [--output <directory>] [--max-manifest-bytes N] "
        "[--max-modules N] [--max-file-bytes N] [--max-source-bytes N] [--max-text-bytes N]\n");
}

} // namespace

class AotCompilerApplication final {
public:
#if defined(_WIN32)
    int run(int argc, wchar_t** argv) const
    {
        arp::core::pmr::Vector<arp::core::pmr::String> arguments{
            &arp::core::defaultCpuMemoryResource()};
        arp::core::pmr::Vector<char*> pointers{&arp::core::defaultCpuMemoryResource()};
        arguments.reserve(argc);
        pointers.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            // Only declared path arguments pass through the native path codec.
            // Command names, flags and decimal budgets have an ASCII grammar.
            if (i == 0 || i == 2 || (i >= 3 && std::wstring_view(argv[i - 1]) == L"--output")) {
                arguments.push_back(arp::core::FilePath::utf8(std::filesystem::path(argv[i])));
            } else {
                arp::core::pmr::String argument{&arp::core::defaultCpuMemoryResource()};
                for (const wchar_t* cursor = argv[i]; *cursor; ++cursor) {
                    if (*cursor < 0 || *cursor > 127) {
                        std::fputs("AOT-CLI-ARGUMENT-ENCODING\n", stderr);
                        return 2;
                    }
                    argument.push_back(static_cast<char>(*cursor));
                }
                arguments.push_back(std::move(argument));
            }
        }
        for (auto& argument : arguments)
            pointers.push_back(argument.data());
        return run(argc, pointers.data());
    }
#endif

    int run(int argc, char** argv) const
    {
        if (argc == 2 && arp::core::StringView(argv[1]) == "--version") {
            std::printf("arp_script_aotc 5\n");
            return 0;
        }
        if (argc < 3) {
            usage();
            return 2;
        }
        const arp::core::StringView command = argv[1];
        const auto manifestPath = std::filesystem::u8path(argv[2]);
        std::filesystem::path output = "aot-package";
        // AotCliSourceProfile/1: explicit tooling limits, independent of VM budgets.
        // Each limit can be reduced or raised by its command-line option.
        std::uint32_t maxManifestBytes = 1024u * 1024u;
        arp::scriptvm::ScriptSourceBudget budget{
            256, 16u * 1024u * 1024u, 64u * 1024u * 1024u, 64u * 1024u * 1024u};
        for (int index = 3; index < argc; ++index) {
            if (arp::core::StringView(argv[index]) == "--output" && index + 1 < argc) {
                output = std::filesystem::u8path(argv[++index]);
            } else {
                const arp::core::StringView option = argv[index];
                if (index + 1 >= argc) {
                    usage();
                    return 2;
                }
                arp::core::AsciiTokenCursor number(argv[++index]);
                std::uint32_t value;
                if (!number.decimalUint32(value) || !number.atEnd() || !value) {
                    usage();
                    return 2;
                }
                if (option == "--max-manifest-bytes")
                    maxManifestBytes = value;
                else if (option == "--max-modules")
                    budget.maxFiles = value;
                else if (option == "--max-file-bytes")
                    budget.maxFileBytes = value;
                else if (option == "--max-source-bytes")
                    budget.maxSourceBytes = value;
                else if (option == "--max-text-bytes")
                    budget.maxTextBytes = value;
                else {
                    usage();
                    return 2;
                }
            }
        }
        if (command != "validate" && command != "compile" && command != "package"
            && command != "report") {
            usage();
            return 2;
        }
        const auto prepared = arp::tools::script_aotc::AotCliSourcePreparation::load(
            manifestPath, maxManifestBytes, budget);
        if (!prepared->ok()) {
            printDiagnostic(prepared->loaded.error);
            return 1;
        }
        const auto& loaded = prepared->loaded;
        const auto& sources = *prepared->sources;
        if (command == "validate") {
            std::printf("valid: %s\n", loaded.package.packageId.c_str());
            return 0;
        }
        arp::scriptvm::ScriptAotPackageBuilder builder;
        const auto built = builder.build(loaded.package, sources, output);
        if (!built.ok()) {
            printDiagnostic(built.error);
            return 1;
        }
        if (command == "report") {
            std::fwrite(built.report.data(), 1, built.report.size(), stdout);
            if (built.report.empty() || built.report.back() != '\n')
                std::fputc('\n', stdout);
            std::printf("package-identity: %s\noutputs: %zu\n",
                built.packageIdentity.c_str(),
                built.outputs.size());
            return 0;
        }
        std::printf("package-identity: %s\noutputs: %zu\n",
            built.packageIdentity.c_str(),
            built.outputs.size());
        return 0;
    }
};

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try {
        return AotCompilerApplication{}.run(argc, argv);
    } catch (const std::bad_alloc&) {
        std::fputs("AOT-CLI-SOURCE-ALLOCATION\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "AOT-CLI-FAILURE: %s\n", error.what());
    }
    return 1;
}
