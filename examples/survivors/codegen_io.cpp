#include "codegen_io.h"

#include "../common/native_startup_wait.h"
#include "core/diagnostics/diagnostic_error.h"
#include "tools/io/cold_tool_files.h"
#include "tools/io/native_tool_outputs.h"

namespace arp::survivors {
namespace {
constexpr std::array<const char*, 3> outputNames{
    "script_aot_v5.gen.h", "survivors.gen.cpp", "survivors.manifest.yaml"};
constexpr examples::NativeStartupWaitDiagnostics waitDiagnostics{"SURVIVORS-CODEGEN-SOURCE-TIMEOUT",
    "SURVIVORS-CODEGEN-SOURCE-DISPATCH",
    "SURVIVORS-CODEGEN-SOURCE-NOT-RETIRED"};

core::UniquePtr<examples::NativeStartupFiles> openSource(const std::filesystem::path& directory,
    examples::NativeStartupSource source,
    core::StringView baseUrl)
{
    examples::NativeStartupFileDesc desc;
    desc.source = source;
    if (source == examples::NativeStartupSource::Directory)
        desc.directory = directory;
    else
        desc.baseUrl = baseUrl;
    desc.mountName = "survivors-codegen";
    desc.maxFileBytes = ContentSourceTask::maxSourceFileBytes;
    return core::makeUnique<examples::NativeStartupFiles>(std::move(desc),
        examples::NativeStartupFileDiagnostics{"SURVIVORS-CODEGEN-SOURCE-CONFIG",
            "SURVIVORS-CODEGEN-SOURCE-MOUNT",
            "SURVIVORS-CODEGEN-SOURCE-RUNTIME"});
}
} // namespace

PreparedCodegenInputs CodegenIo::prepareInputs(const std::filesystem::path& directory,
    examples::NativeStartupSource source,
    core::StringView baseUrl,
    core::pmr::String& diagnostic) const
try {
    diagnostic.clear();
    PreparedCodegenInputs result;
    {
        auto files = openSource(directory, source, baseUrl);
        GameplaySourceTask task(
            files->runtime().fileService(), files->runtime().completionQueue(), files->mount());
        if (!examples::waitForNativeStartup(
                *files, task, std::chrono::seconds(20), waitDiagnostics, diagnostic))
            return {};
        result.gameplay = task.preparation();
        if (!result.gameplay)
            throw core::DiagnosticError("SURVIVORS-CODEGEN-GAMEPLAY-OWNER");
    }
    {
        auto files = openSource(directory, source, baseUrl);
        ContentSourceTask task(files->runtime().fileService(),
            files->runtime().completionQueue(),
            files->mount(),
            result.gameplay->config);
        if (!examples::waitForNativeStartup(
                *files, task, std::chrono::seconds(20), waitDiagnostics, diagnostic))
            return {};
        result.content = task.preparation();
        if (!result.content)
            throw core::DiagnosticError("SURVIVORS-CODEGEN-CONTENT-OWNER");
    }
    return result;
} catch (const std::bad_alloc&) {
    diagnostic = "SURVIVORS-CODEGEN-SOURCE-ALLOCATION";
    return {};
} catch (const std::exception& error) {
    diagnostic = error.what();
    return {};
}

bool CodegenIo::saveOutputs(const std::filesystem::path& directory,
    const std::array<core::StringView, 3>& contents,
    std::uint64_t maxOutputBytes,
    core::pmr::String& diagnostic) const
try {
    using namespace tools::io;
    diagnostic.clear();
    if (directory.empty() || !ColdToolFiles::validBudget(maxOutputBytes)
        || maxOutputBytes > CodegenIoLimits::maxOutputBytes) {
        diagnostic = "SURVIVORS-CODEGEN-OUTPUT-OPTIONS";
        return false;
    }
    const std::array<NativeToolOutputFile, 3> files{{
        {directory / outputNames[0], contents[0]},
        {directory / outputNames[1], contents[1]},
        {directory / outputNames[2], contents[2]},
    }};
    const auto result = NativeToolOutputWriter{}.save(
        {files, 3, maxOutputBytes, NativeToolOutputEncoding::LegacyNativeText});
    if (result.success())
        return true;
    switch (result.failure) {
    case NativeToolOutputFailure::Options:
        diagnostic = "SURVIVORS-CODEGEN-OUTPUT-OPTIONS";
        break;
    case NativeToolOutputFailure::Budget:
        diagnostic = "SURVIVORS-CODEGEN-OUTPUT-BUDGET";
        break;
    case NativeToolOutputFailure::Directory:
        diagnostic = "SURVIVORS-CODEGEN-OUTPUT-DIRECTORY";
        break;
    case NativeToolOutputFailure::Allocation:
        diagnostic = "SURVIVORS-CODEGEN-OUTPUT-ALLOCATION";
        break;
    default:
        diagnostic = result.diagnosticId;
        break;
    }
    return false;
} catch (const std::bad_alloc&) {
    diagnostic = "SURVIVORS-CODEGEN-OUTPUT-ALLOCATION";
    return false;
} catch (const std::exception& error) {
    diagnostic = error.what();
    return false;
}
} // namespace arp::survivors
