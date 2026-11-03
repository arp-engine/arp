#include "tools/cloud_asset/cloud_cook_application.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/filesystem_path.h"
#include "tools/cloud_asset/cloud_cook_session.h"
#include "tools/io/cold_tool_files.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <thread>

namespace arp::tools::cloud_asset {
namespace {
using namespace core::io;

} // namespace

int CloudCookApplication::run(int argc, char** argv) const
{
#if defined(__EMSCRIPTEN__)
    std::fputs("CLOUD-TOOL-NATIVE-WAIT-UNAVAILABLE\n", stderr);
    return 1;
#else
    if ((argc != 5 && argc != 7) || std::string_view(argv[1]) != "cook"
        || (argc == 7 && std::string_view(argv[5]) != "--source-http-base")) {
        std::fputs(
            "Usage: arp_cloud_asset_tool cook <recipe.yaml> <cooked.yaml> <max-document-bytes>"
            " [--source-http-base <url>]\n",
            stderr);
        return 2;
    }
    const std::string_view budgetText(argv[4]);
    std::uint64_t bytes = 0;
    const auto parsed =
        std::from_chars(budgetText.data(), budgetText.data() + budgetText.size(), bytes);
    if (parsed.ec != std::errc{} || parsed.ptr != budgetText.data() + budgetText.size() || !bytes
        || !io::ColdToolFiles::validBudget(bytes))
        return 2;
    try {
        if (!*argv[2] || !*argv[3] || (argc == 7 && !*argv[6]))
            throw core::DiagnosticError("CLOUD-TOOL-EMPTY-LOCATION");
        if (argc == 7 && std::string_view(argv[6]).size() > io::ColdToolFileProfile::pathBytes)
            throw core::DiagnosticError("CLOUD-TOOL-HTTP-BASE-BUDGET");
        core::installDefaultCpuAllocatorAsPmrDefault();
        const bool httpSource = argc == 7;
        const auto output =
            std::filesystem::absolute(std::filesystem::u8path(argv[3])).lexically_normal();
        io::ColdToolFiles files(bytes, 2);
        core::PoolHandle sourceMount;
        core::pmr::String sourcePath{&core::defaultCpuMemoryResource()};
        if (httpSource) {
            sourcePath = argv[2];
            sourceMount = files.mountHttp("recipe", argv[6]);
        } else {
            const auto input =
                std::filesystem::absolute(std::filesystem::u8path(argv[2])).lexically_normal();
            sourcePath = core::FilePath::utf8(input.filename());
            sourceMount = files.mountDirectory("recipe", input.parent_path());
        }
        const auto outputMount = files.mountDirectory("cooked", output.parent_path(), true);
        if (!sourceMount || !outputMount)
            throw core::DiagnosticError("CLOUD-TOOL-MOUNT");
        CloudCookSessionDesc desc;
        desc.source.contractVersion = 1;
        desc.source.pathScope = FilePathScope::MountedResource;
        desc.source.mount = sourceMount;
        desc.source.path = std::move(sourcePath);
        desc.source.maxBytes = bytes;
        desc.outputMount = outputMount;
        desc.outputPath = core::FilePath::utf8(output.filename());
        desc.maxDocumentBytes = bytes;
        CloudCookSession session(files.service(), desc);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!session.terminal()) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw core::DiagnosticError("CLOUD-TOOL-DEADLINE");
            if (!files.dispatch())
                throw core::DiagnosticError("CLOUD-TOOL-DISPATCH");
            session.advance();
            std::this_thread::yield();
        }
        files.retire();
        if (session.phase() != CloudCookPhase::Complete)
            throw core::DiagnosticError(session.diagnostic());
        std::printf("CloudPeriodicSimplex/%u cooked %u cells (%zu document bytes)\n",
            session.algorithmVersion(),
            session.cellCount(),
            session.documentBytes());
        std::printf("CloudCookToolIo/1 source=%s committed=true strictConditionalReplace=false\n",
            httpSource ? "http" : "native");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
#endif
}
} // namespace arp::tools::cloud_asset
