#include "startup_resource_task.h"

#include "core/diagnostics/diagnostic_error.h"

namespace arp::examples {

struct StartupResourceTask::State {
    resource::ResourceManager manager;
    core::io::FileService* files = nullptr;
    core::runtime::CompletionQueue* queue = nullptr;
    resource::ResourceGraphExecutionRequest request;
    core::runtime::CancellationSource cancellation;
    core::runtime::CancellationToken externalCancellation;
    StartupResourcePhase phase = StartupResourcePhase::Pending;
    core::pmr::String message;
    const char* diagnosticId = nullptr;
    core::io::FileIoStatus fileStatus = core::io::FileIoStatus::Success;
    StartupResourceDiagnostics diagnostics;
    std::uint64_t reads = 0, bytes = 0;
    bool submitted = false;
};

StartupResourceTask::StartupResourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    resource::ResourceGraphExecutionRequest request,
    StartupResourceDiagnostics diagnostics,
    core::runtime::CancellationToken cancellation)
    : state_(core::makeShared<State>())
{
    if (!diagnostics.cancelled || !*diagnostics.cancelled || !diagnostics.notAccepted
        || !*diagnostics.notAccepted || !diagnostics.preparationFailed
        || !*diagnostics.preparationFailed)
        throw core::DiagnosticError("EXAMPLE-STARTUP-DIAGNOSTICS-INVALID");
    state_->diagnostics = diagnostics;
    state_->files = &files;
    state_->queue = &queue;
    state_->request = std::move(request);
    state_->externalCancellation = std::move(cancellation);
}

StartupResourceTask::~StartupResourceTask()
{
    cancel();
}

void StartupResourceTask::cancel() noexcept
{
    if (state_->phase == StartupResourcePhase::Pending) {
        state_->cancellation.cancel();
        state_->phase = StartupResourcePhase::Cancelled;
        state_->diagnosticId = state_->diagnostics.cancelled;
    }
}

StartupResourcePhase StartupResourceTask::phase() const noexcept
{
    return state_->phase;
}

std::string_view StartupResourceTask::diagnostic() const noexcept
{
    return !state_->message.empty() ? std::string_view(state_->message)
        : state_->diagnosticId      ? std::string_view(state_->diagnosticId)
                                    : std::string_view();
}

const char* StartupResourceTask::diagnosticId() const noexcept
{
    return state_->diagnosticId;
}

core::io::FileIoStatus StartupResourceTask::fileStatus() const noexcept
{
    return state_->fileStatus;
}

const resource::ResourceManager& StartupResourceTask::resources() const noexcept
{
    return state_->manager;
}

std::uint64_t StartupResourceTask::readCount() const noexcept
{
    return state_->reads;
}

std::uint64_t StartupResourceTask::bytesRead() const noexcept
{
    return state_->bytes;
}

void StartupResourceTask::advance() noexcept
{
    auto& s = *state_;
    if (s.phase != StartupResourcePhase::Pending)
        return;
    if (s.externalCancellation.cancelled()) {
        cancel();
        return;
    }
    if (s.submitted)
        return;
    try {
        s.request.cancellation = s.cancellation.token();
        auto retained = core::makeUnique<core::SharedPtr<State>>(state_);
        resource::ResourceGraphExecutor executor(s.manager, *s.files, *s.queue);
        const auto status = executor.submit(std::move(s.request),
            {[](void* context, resource::ResourceGraphExecutionResult result) noexcept {
                 using Retained = core::SharedPtr<State>;
                 core::UniquePtr<Retained> owner{static_cast<Retained*>(context),
                     core::CpuAllocatedDeleter<Retained>{
                         context, &core::destroyCpuAllocatedUnique<Retained>}};
                 auto& state = **owner;
                 if (state.phase != StartupResourcePhase::Pending)
                     return;
                 state.reads = result.telemetry.submittedFileReads;
                 state.bytes = result.telemetry.bytesRead;
                 state.message = std::move(result.resource.diagnostics);
                 state.diagnosticId = result.diagnosticId;
                 state.fileStatus = result.fileStatus;
                 state.phase = result.resource.success ? StartupResourcePhase::Ready
                                                       : StartupResourcePhase::Failed;
             },
                retained.get()});
        if (status != resource::ResourceGraphSubmitStatus::Accepted) {
            s.phase = StartupResourcePhase::Failed;
            s.diagnosticId = s.diagnostics.notAccepted;
            return;
        }
        retained.release();
        s.submitted = true;
    } catch (...) {
        s.phase = StartupResourcePhase::Failed;
        s.diagnosticId = s.diagnostics.preparationFailed;
    }
}
} // namespace arp::examples
