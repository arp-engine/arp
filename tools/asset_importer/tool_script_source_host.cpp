#include "tools/asset_importer/tool_script_source_host.h"

#include "core/io/filesystem_path.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "core/runtime/async_runtime.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace arp::tools::asset_importer {
ToolScriptSourcePreparation ToolScriptSourceHost::prepareNative(
    const std::filesystem::path& entryPath,
    const std::filesystem::path& moduleRoot,
    scriptvm::ScriptSourceBudget budget)
{
    ToolScriptSourcePreparation output;
#if defined(__EMSCRIPTEN__)
    output.diagnostics = "TOOL-SOURCE-NATIVE-WAIT-UNAVAILABLE";
#else
    using namespace core::io;
    try {
        if (entryPath.empty() || moduleRoot.empty()) {
            output.diagnostics = "TOOL-SOURCE-NATIVE-DESCRIPTOR";
            return output;
        }
        const auto entry = std::filesystem::absolute(entryPath).lexically_normal();
        const auto modules = std::filesystem::absolute(moduleRoot).lexically_normal();
        if (!scriptvm::ScriptSourceSnapshot::validBudget(budget)
            || !core::FilePath::isSafeRelativePath(
                core::FilePath::utf8(entry.filename()), ToolScriptSourceLimits::maxPathBytes)) {
            output.diagnostics = "TOOL-SOURCE-NATIVE-DESCRIPTOR";
            return output;
        }
        auto router = core::makeShared<MountedFileBackend>(MountedFileBackendDesc{
            2, 1, budget.maxFileBytes, 32, ToolScriptSourceLimits::maxPathBytes});
        auto mount = [&](const char* alias, const std::filesystem::path& root) {
            NativeDirectoryFileBackendDesc desc;
            desc.rootPath = core::FilePath::utf8(root);
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = budget.maxFileBytes;
            desc.maxPathBytes = ToolScriptSourceLimits::maxPathBytes;
            desc.ioChunkBytes = 16384;
            desc.directoryBufferBytes = 4096;
            desc.maxScanEntries = ToolScriptSourceLimits::maxNames;
            desc.maxScanNameBytes = 4096;
            return router->mount(
                alias, core::makeShared<NativeDirectoryFileBackend>(std::move(desc)));
        };
        const auto entryMount = mount("entry", entry.parent_path());
        const auto moduleMount = mount("modules", modules);
        if (!entryMount || !moduleMount) {
            output.diagnostics = "TOOL-SOURCE-NATIVE-MOUNT";
            return output;
        }
        core::runtime::AsyncRuntimeDesc desc;
        desc.enableWork = desc.enableThreadPoolFileBackend = false;
        desc.completionBudget = desc.completionQueueCapacity = 2;
        desc.fileBackend = router;
        core::runtime::AsyncRuntime runtime(std::move(desc));
        if (!runtime.valid()) {
            output.diagnostics = "TOOL-SOURCE-NATIVE-RUNTIME";
            return output;
        }
        ToolScriptSourceDesc source;
        source.entry.contractVersion = 1;
        source.entry.path = core::FilePath::utf8(entry.filename());
        source.entry.pathScope = FilePathScope::MountedResource;
        source.entry.mount = entryMount;
        source.entry.maxBytes = budget.maxFileBytes;
        source.entryDisplayName = core::FilePath::utf8(entryPath);
        source.moduleMount = moduleMount;
        source.moduleDisplayPrefix = core::FilePath::utf8(moduleRoot);
        source.budget = budget;
        source.maxNames = std::max(ToolScriptSourceLimits::maxNames, budget.maxFiles);
        ToolScriptSourceTask task(runtime.fileService(), std::move(source));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (task.phase() == ToolScriptSourcePhase::Pending) {
            if (std::chrono::steady_clock::now() >= deadline) {
                output.diagnostics = "TOOL-SOURCE-NATIVE-TIMEOUT";
                task.cancel();
                break;
            }
            if (!runtime.dispatch().success()) {
                output.diagnostics = "TOOL-SOURCE-NATIVE-DISPATCH";
                task.cancel();
                break;
            }
            task.advance();
            std::this_thread::yield();
        }
        runtime.shutdown();
        if (runtime.completionQueue().pending() || runtime.completionQueue().reserved()
            || router->telemetry().inFlight) {
            output.diagnostics = "TOOL-SOURCE-NATIVE-NOT-RETIRED";
            return output;
        }
        if (task.phase() == ToolScriptSourcePhase::Ready)
            output.sources = task.sources();
        else if (output.diagnostics.empty()) {
            if (task.diagnosticId()) {
                output.diagnostics = task.diagnosticId();
                output.diagnostics += ": ";
            }
            output.diagnostics += task.diagnostic();
        }
    } catch (const std::exception& error) {
        output.sources.reset();
        output.diagnostics = "TOOL-SOURCE-NATIVE-PREPARATION: ";
        output.diagnostics += error.what();
    }
#endif
    return output;
}
} // namespace arp::tools::asset_importer
