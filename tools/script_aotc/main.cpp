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
        "<manifest.yaml> [--output <directory>]\n");
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
    for (int index = 3; index < argc; ++index) {
        if (std::string(argv[index]) == "--output" && index + 1 < argc) {
            output = argv[++index];
        } else {
            usage();
            return 2;
        }
    }
    if (command != "validate" && command != "compile" && command != "package"
        && command != "report") {
        usage();
        return 2;
    }
    const auto loaded = arp::scriptvm::ScriptAotPackageBuilder::load(manifestPath);
    if (!loaded.ok()) {
        printDiagnostic(loaded.error);
        return 1;
    }
    if (command == "validate") {
        std::printf("valid: %s\n", loaded.package.packageId.c_str());
        return 0;
    }
    arp::scriptvm::ScriptAotPackageBuilder builder;
    const auto built = builder.build(loaded.package, output);
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
