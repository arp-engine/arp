#include "editor/app/editor_startup_resource_host.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/filesystem_path.h"
#include "core/io/http_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/native_directory_file_backend.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace arp::editor {

EditorStartupResourceHost::EditorStartupResourceHost(
    core::ArrayView<const EditorStartupMountDesc> inputs)
{
#if defined(__EMSCRIPTEN__)
    throw core::DiagnosticError("EDITOR-STARTUP-NATIVE-HOST-REQUIRED");
#else
    using namespace core::io;
    if (inputs.empty() || inputs.size() > 3)
        throw core::DiagnosticError("EDITOR-STARTUP-MOUNT-COUNT");
    std::size_t maximum = 0;
    for (const auto& input : inputs) {
        if (input.alias.empty() || !input.maxFileBytes || input.maxFileBytes > SIZE_MAX / 2
            || (input.baseUrl && input.baseUrl->empty()))
            throw core::DiagnosticError("EDITOR-STARTUP-MOUNT-CONFIG");
        maximum = std::max(maximum, input.maxFileBytes);
    }
    auto router = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{static_cast<std::uint32_t>(inputs.size()), 1, maximum, 32, 512});
    mounts_.reserve(inputs.size());
    for (const auto& input : inputs) {
        core::SharedPtr<ManagedFileBackend> backend;
        if (input.baseUrl) {
            HttpFileBackendDesc desc;
            desc.baseUrl = *input.baseUrl;
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 2 * input.maxFileBytes;
            desc.maxPathBytes = 512;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 16384;
            desc.timeoutMs = 5000;
            desc.cancellationPollMs = 10;
            desc.maxDnsAddresses = 16;
            backend = core::makeShared<HttpFileBackend>(std::move(desc));
        } else {
            NativeDirectoryFileBackendDesc desc;
            desc.rootPath = core::FilePath::utf8(input.directory);
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = input.maxFileBytes;
            desc.maxPathBytes = 512;
            desc.ioChunkBytes = 16384;
            desc.directoryBufferBytes = 4096;
            desc.maxScanEntries = 16;
            desc.maxScanNameBytes = 4096;
            backend = core::makeShared<NativeDirectoryFileBackend>(std::move(desc));
        }
        const auto handle = router->mount(input.alias, std::move(backend));
        if (!handle)
            throw core::DiagnosticError("EDITOR-STARTUP-MOUNT");
        mounts_.push_back(handle);
    }
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.completionBudget = desc.completionQueueCapacity = 2;
    desc.fileBackend = std::move(router);
    runtime_ = core::makeUnique<core::runtime::AsyncRuntime>(std::move(desc));
    if (!runtime_->valid())
        throw core::DiagnosticError("EDITOR-STARTUP-RUNTIME");
#endif
}

bool EditorStartupResourceHost::execute(resource::ResourceManager& manager,
    resource::ResourceGraphExecutionRequest request,
    resource::ResourceGraphExecutionResult& output,
    core::pmr::String& diagnostic)
{
    if (!runtime_) {
        diagnostic = "EDITOR-STARTUP-NATIVE-HOST-REQUIRED";
        return false;
    }

    struct Completion {
        resource::ResourceGraphExecutionResult result;
        bool done = false;
    } completed;

    const auto external = request.cancellation;
    core::runtime::CancellationSource cancellation;
    request.cancellation = cancellation.token();
    const auto fail = [&](const char* issue) {
        cancellation.cancel();
        runtime_->shutdown();
        diagnostic = issue;
        return false;
    };
    try {
        if (external.cancelled())
            return fail("EDITOR-STARTUP-CANCELLED");
        resource::ResourceGraphExecutor executor(
            manager, runtime_->fileService(), runtime_->completionQueue());
        const auto accepted = executor.submit(std::move(request),
            {[](void* context, resource::ResourceGraphExecutionResult result) noexcept {
                 auto& state = *static_cast<Completion*>(context);
                 state.result = std::move(result);
                 state.done = true;
             },
                &completed});
        if (accepted != resource::ResourceGraphSubmitStatus::Accepted)
            return fail("EDITOR-STARTUP-GRAPH-NOT-ACCEPTED");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!completed.done && std::chrono::steady_clock::now() < deadline) {
            if (external.cancelled())
                return fail("EDITOR-STARTUP-CANCELLED");
            if (!runtime_->dispatch().success())
                return fail("EDITOR-STARTUP-DISPATCH");
            std::this_thread::yield();
        }
        if (!completed.done)
            return fail("EDITOR-STARTUP-TIMEOUT");
        runtime_->shutdown();
        output = std::move(completed.result);
        if (!output.resource.success) {
            diagnostic = output.resource.diagnostics;
            if (diagnostic.empty())
                diagnostic =
                    output.diagnosticId ? output.diagnosticId : "EDITOR-STARTUP-PREPARATION";
            return false;
        }
        diagnostic.clear();
        return true;
    } catch (...) {
        return fail("EDITOR-STARTUP-ALLOCATION");
    }
}
} // namespace arp::editor
