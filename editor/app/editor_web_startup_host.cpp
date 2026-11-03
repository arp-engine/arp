#include "editor/app/editor_web_startup_host.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/io/http_file_backend.h"

namespace arp::editor {
namespace {
std::array<core::SharedPtr<core::io::ManagedFileBackend>, 3> httpBackends(
    const std::array<core::pmr::String, 3>& urls)
{
    std::array<core::SharedPtr<core::io::ManagedFileBackend>, 3> result;
    for (std::size_t i = 0; i < urls.size(); ++i) {
        core::io::HttpFileBackendDesc desc;
        desc.baseUrl = urls[i];
        desc.maxInFlight = 1;
        desc.maxBytesInFlight = 2 * EditorWebStartupLimits::maxFileBytes;
        desc.maxPathBytes = 512;
        desc.maxHeaderBytes = 8192;
        desc.ioChunkBytes = 16384;
        desc.timeoutMs = 5000;
        desc.cancellationPollMs = 10;
        desc.maxDnsAddresses = 16;
        result[i] = core::makeShared<core::io::HttpFileBackend>(std::move(desc));
    }
    return result;
}
} // namespace

EditorWebStartupHost::EditorWebStartupHost(const std::array<core::pmr::String, 3>& urls)
    : EditorWebStartupHost(httpBackends(urls))
{}

EditorWebStartupHost::EditorWebStartupHost(
    const std::array<core::SharedPtr<core::io::ManagedFileBackend>, 3>& backends)
{
    using Limits = EditorWebStartupLimits;
    using namespace core::io;
    auto router = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{3, 1, Limits::maxFileBytes, 32, 512});
    EditorWebStartupMounts mounts{router->mount("ui", backends[0]),
        router->mount("fonts", backends[1]),
        router->mount("shaders", backends[2])};
    if (!mounts.ui || !mounts.fonts || !mounts.shaders)
        throw core::DiagnosticError("EDITOR-WEB-STARTUP-MOUNT");
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.completionBudget = desc.completionQueueCapacity = 2;
    desc.fileBackend = std::move(router);
    runtime_ = core::makeUnique<core::runtime::AsyncRuntime>(std::move(desc));
    if (!runtime_->valid())
        throw core::DiagnosticError("EDITOR-WEB-STARTUP-RUNTIME");
    resource::ResourceGraphExecutor executor(
        manager_, runtime_->fileService(), runtime_->completionQueue());
    if (executor.submit(EditorWebStartupSources::request(mounts, cancellation_.token()),
            {[](void* context, resource::ResourceGraphExecutionResult result) noexcept {
                 auto& self = *static_cast<EditorWebStartupHost*>(context);
                 self.result_ = std::move(result);
                 self.done_ = true;
             },
                this})
        != resource::ResourceGraphSubmitStatus::Accepted)
        throw core::DiagnosticError("EDITOR-WEB-STARTUP-NOT-ACCEPTED");
}

EditorWebStartupHost::~EditorWebStartupHost()
{
    cancel();
    if (runtime_)
        runtime_->shutdown();
}

void EditorWebStartupHost::cancel() noexcept
{
    cancellation_.cancel();
}

void EditorWebStartupHost::poll()
{
    if (done_)
        return;
    if (!runtime_->dispatch().success())
        throw core::DiagnosticError("EDITOR-WEB-STARTUP-DISPATCH");
    if (!done_)
        return;
    runtime_->shutdown();
    diagnostic_ = result_.resource.diagnostics;
    if (!result_.resource.success && diagnostic_.empty())
        diagnostic_ = result_.diagnosticId ? result_.diagnosticId : "EDITOR-WEB-STARTUP-FAILED";
}

core::SharedPtr<const EditorWebStartupSources> EditorWebStartupHost::sources() const
{
    return ready() ? EditorWebStartupSources::capture(manager_) : nullptr;
}
} // namespace arp::editor
