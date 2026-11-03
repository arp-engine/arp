#include "tools/script_aotc/aot_cli_sources.h"

#include "core/io/filesystem_path.h"
#include "tools/io/cold_tool_files.h"

#include <algorithm>

namespace arp::tools::script_aotc {
namespace {
void fail(
    AotCliPreparedSources& result, core::StringView message, const std::filesystem::path& file = {})
{
    result.sources.reset();
    result.loaded.error.code = scriptvm::ScriptDiagnosticCode::AotArtifactInvalid;
    result.loaded.error.message = message;
    result.loaded.error.file = core::FilePath::utf8(file);
}
} // namespace

core::UniquePtr<AotCliPreparedSources> AotCliSourcePreparation::load(
    const std::filesystem::path& manifest,
    std::uint64_t manifestBytes,
    scriptvm::ScriptSourceBudget budget)
{
    using namespace core::io;
    using scriptvm::ScriptSourceSnapshot;
    auto result = core::makeUnique<AotCliPreparedSources>();
    if (!ScriptSourceSnapshot::validBudget(budget)
        || !ScriptSourceSnapshot::validBudget({1, manifestBytes, manifestBytes, manifestBytes})
        || !io::ColdToolFiles::validBudget(std::max(manifestBytes, budget.maxFileBytes))) {
        fail(*result, "ARP-SCRIPT-SOURCE-BUDGET");
        return result;
    }
    if (manifest.empty()) {
        fail(*result, "AOT-CLI-MANIFEST-LOCATION");
        return result;
    }
    try {
        io::ColdToolFiles files(std::max(manifestBytes, budget.maxFileBytes),
            0,
            io::ColdToolFileScope::NativeHostFilesystem);
        result->backend = files.service().capability();
        FileReadRequest request;
        request.contractVersion = 1;
        request.pathScope = FilePathScope::HostFilesystem;
        request.path = core::FilePath::utf8(std::filesystem::absolute(manifest).lexically_normal());
        request.maxBytes = manifestBytes;
        result->loaded =
            scriptvm::ScriptAotPackageBuilder::load(request, files.read(request), manifestBytes);
        if (result->loaded.ok()) {
            if (result->loaded.package.modules.size() > budget.maxFiles)
                fail(*result, "ARP-SCRIPT-SOURCE-BUDGET");
            else {
                result->sources = core::makeUnique<ScriptSourceSnapshot>(budget);
                const char* diagnostic = nullptr;
                for (const auto& module : result->loaded.package.modules) {
                    FileReadRequest source;
                    source.contractVersion = 1;
                    source.pathScope = FilePathScope::HostFilesystem;
                    source.path = core::FilePath::utf8(module.source);
                    source.maxBytes = std::min(budget.maxFileBytes,
                        budget.maxSourceBytes - result->sources->sourceBytes());
                    if (!result->sources->add(
                            module.moduleId, source, files.read(source), diagnostic)) {
                        fail(*result, diagnostic, module.source);
                        break;
                    }
                }
                if (result->sources && !result->sources->seal(diagnostic))
                    fail(*result, diagnostic);
            }
        }
        files.retire();
        result->telemetry = files.service().telemetry();
    } catch (const std::bad_alloc&) {
        fail(*result, "AOT-CLI-SOURCE-ALLOCATION");
    } catch (const std::exception& error) {
        fail(*result, error.what(), manifest);
    }
    return result;
}
} // namespace arp::tools::script_aotc
