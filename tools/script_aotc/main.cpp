#include "core/text/ascii_token_cursor.h"
#include "scriptvm/aot/aot_package.h"

#include <cstdio>
#include <filesystem>
#include <string>

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

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::printf("arp_script_aotc 5\n");
        return 0;
    }
    if (argc < 3) {
        usage();
        return 2;
    }
    const std::string command = argv[1];
    const std::filesystem::path manifestPath = argv[2];
    std::filesystem::path output = "aot-package";
    // AotCliSourceProfile/1: explicit tooling limits, independent of VM budgets.
    // Each limit can be reduced or raised by its command-line option.
    std::uint32_t maxManifestBytes = 1024u * 1024u;
    arp::scriptvm::ScriptSourceBudget budget{
        256, 16u * 1024u * 1024u, 64u * 1024u * 1024u, 64u * 1024u * 1024u};
    for (int index = 3; index < argc; ++index) {
        if (std::string(argv[index]) == "--output" && index + 1 < argc) {
            output = argv[++index];
        } else {
            const std::string option = argv[index];
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
    const auto loaded =
        arp::scriptvm::ScriptAotPackageBuilder::load(manifestPath, maxManifestBytes);
    if (!loaded.ok()) {
        printDiagnostic(loaded.error);
        return 1;
    }
    arp::scriptvm::ScriptSourceSnapshot sources(budget);
    const auto captured =
        arp::scriptvm::ScriptAotPackageBuilder::captureHostSources(loaded.package, sources);
    if (!captured.ok()) {
        printDiagnostic(captured.diagnostic());
        return 1;
    }
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
