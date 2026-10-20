#include "core/io/thread_pool_file_backend.h"

namespace arp::core::io {

ThreadPoolFallbackFileBackend::ThreadPoolFallbackFileBackend(
    runtime::WorkService& workService) noexcept
    : workService_(workService)
{}

FileBackendCapability ThreadPoolFallbackFileBackend::capability() const noexcept
{
    return {
        FileBackendKind::ThreadPoolFallbackFileBackend,
        FileBackendStatus::PrototypeComplete,
        false,
        true,
        true,
    };
}

FileReadResult ThreadPoolFallbackFileBackend::validateRead(const FileReadRequest& request) const
{
    BlockingFileReader reader;
    return reader.read(request);
}

FileIoStatus ThreadPoolFallbackFileBackend::readAsync(const FileReadRequest& request,
    FileReadCompletion completion,
    runtime::CompletionQueue&) noexcept
{
    if (!completion.valid()) {
        return FileIoStatus::InvalidRequest;
    }
    auto state = core::makeUnique<WorkState>();
    state->request = request;
    state->completion = completion;
    auto* raw = state.get();
    const auto submit = workService_.submit(runtime::WorkItem{
        &execute,
        raw,
        &complete,
        raw,
        request.cancellation,
    });
    if (submit.accepted()) {
        state.release();
        return FileIoStatus::Accepted;
    }
    if (submit.status == runtime::WorkServiceStatus::QueueFull) {
        return FileIoStatus::QueueFull;
    }
    if (submit.status == runtime::WorkServiceStatus::Cancelled) {
        return FileIoStatus::Cancelled;
    }
    return FileIoStatus::ServiceClosed;
}

runtime::WorkStatus ThreadPoolFallbackFileBackend::execute(void* userData)
{
    auto* state = static_cast<WorkState*>(userData);
    if (state->request.cancellation.cancelled()) {
        state->result.status = FileIoStatus::Cancelled;
        state->result.backendKind = FileBackendKind::ThreadPoolFallbackFileBackend;
        state->result.diagnostics = core::pmr::String(
            "file read cancelled before blocking read", &core::defaultCpuMemoryResource());
        return runtime::WorkStatus::Cancelled;
    }
    state->result = state->reader.read(state->request);
    if (state->request.cancellation.cancelled() && state->result.status == FileIoStatus::Success) {
        state->result.status = FileIoStatus::Cancelled;
        state->result.bytes.clear();
        state->result.diagnostics =
            core::pmr::String("file read completed after cancellation and was discarded",
                &core::defaultCpuMemoryResource());
        return runtime::WorkStatus::Cancelled;
    }
    return state->result.status == FileIoStatus::Success ? runtime::WorkStatus::Completed
                                                         : runtime::WorkStatus::Failed;
}

void ThreadPoolFallbackFileBackend::complete(void* userData, runtime::WorkResult result)
{
    core::UniquePtr<WorkState> state{
        static_cast<WorkState*>(userData),
        core::CpuAllocatedDeleter<WorkState>{
            userData,
            &core::destroyCpuAllocatedUnique<WorkState>,
        },
    };
    if (result.status == runtime::WorkStatus::Cancelled) {
        state->result.status = FileIoStatus::Cancelled;
        state->result.backendKind = FileBackendKind::ThreadPoolFallbackFileBackend;
        state->result.diagnostics = core::pmr::String(
            "file read cancelled before completion", &core::defaultCpuMemoryResource());
    }
    state->completion.complete(state->completion.userData, state->result);
}

} // namespace arp::core::io
