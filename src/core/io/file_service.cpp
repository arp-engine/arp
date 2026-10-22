#include "core/io/file_service.h"

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <chrono>

namespace arp::core::io {

namespace {

core::pmr::String diagnostic(const char* text)
{
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

} // namespace

FileBackendCapability NullFileBackend::capability() const noexcept
{
    return {
        FileBackendKind::NullFileBackend,
        FileBackendStatus::ContractComplete,
        false,
        true,
        true,
    };
}

FileReadResult NullFileBackend::validateRead(const FileReadRequest& request) const
{
    FileReadResult result;
    result.backendKind = FileBackendKind::NullFileBackend;
    if (request.pathScope == FilePathScope::Unspecified) {
        result.status = FileIoStatus::InvalidPathScope;
        result.diagnostics = diagnostic("file read requires explicit path scope");
        return result;
    }
    if (request.path.empty()) {
        result.status = FileIoStatus::InvalidRequest;
        result.diagnostics = diagnostic("file read requires non-empty path");
        return result;
    }
    if (request.range.mode == FileReadRangeMode::ExactRange && request.range.size == 0) {
        result.status = FileIoStatus::InvalidRange;
        result.diagnostics = diagnostic("exact range file read requires size greater than zero");
        return result;
    }
    result.status = FileIoStatus::BackendUnavailable;
    result.diagnostics = diagnostic("NullFileBackend validates requests but does not read files");
    return result;
}

FileIoStatus NullFileBackend::readAsync(const FileReadRequest& request,
    FileReadCompletion completion,
    runtime::CompletionQueue& completions) noexcept
{
    struct CompletionState {
        FileReadCompletion completion;
        FileReadResult result;
    };

    auto deliver = [](void* userData) noexcept {
        core::UniquePtr<CompletionState> state{
            static_cast<CompletionState*>(userData),
            core::CpuAllocatedDeleter<CompletionState>{
                userData,
                &core::destroyCpuAllocatedUnique<CompletionState>,
            },
        };
        state->completion.complete(state->completion.userData, state->result);
    };

    if (!completion.valid()) {
        return FileIoStatus::InvalidRequest;
    }
    auto state = core::makeUnique<CompletionState>();
    state->completion = completion;
    state->result = validateRead(request);
    auto* raw = state.get();
    const auto post = completions.post(runtime::CompletionCallback{deliver, raw});
    if (post.accepted()) {
        state.release();
        return FileIoStatus::Accepted;
    }
    return post.status == runtime::CompletionStatus::QueueFull ? FileIoStatus::QueueFull
                                                               : FileIoStatus::ServiceClosed;
}

FileService::FileService(runtime::CompletionQueue& completions, FileBackend& backend) noexcept
    : completions_(completions), backend_(backend)
{}

FileBackendCapability FileService::capability() const noexcept
{
    return backend_.capability();
}

FileIoStatus FileService::read(
    const FileReadRequest& request, FileReadCompletion completion) noexcept
{
    const auto submitStart = std::chrono::steady_clock::now();
    auto recordSubmitLatency = [this, submitStart]() noexcept {
        const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - submitStart);
        const auto latencyNs = static_cast<std::uint64_t>(latency.count());
        telemetry_.totalSubmitLatencyNs += latencyNs;
        telemetry_.maxSubmitLatencyNs = std::max(telemetry_.maxSubmitLatencyNs, latencyNs);
    };

    if (closed_) {
        ++telemetry_.rejected;
        recordSubmitLatency();
        return FileIoStatus::ServiceClosed;
    }
    if (!completion.valid()) {
        ++telemetry_.rejected;
        recordSubmitLatency();
        return FileIoStatus::InvalidRequest;
    }
    if (request.cancellation.cancelled()) {
        struct CompletionState {
            FileReadCompletion completion;
            FileReadResult result;
        };

        auto deliver = [](void* userData) noexcept {
            core::UniquePtr<CompletionState> state{
                static_cast<CompletionState*>(userData),
                core::CpuAllocatedDeleter<CompletionState>{
                    userData,
                    &core::destroyCpuAllocatedUnique<CompletionState>,
                },
            };
            state->completion.complete(state->completion.userData, state->result);
        };

        auto state = core::makeUnique<CompletionState>();
        state->completion = completion;
        state->result.status = FileIoStatus::Cancelled;
        state->result.backendKind = backend_.capability().kind;
        state->result.diagnostics = diagnostic("file read cancelled before scheduling");
        auto* raw = state.get();
        const auto post = completions_.post(runtime::CompletionCallback{deliver, raw});
        if (post.accepted()) {
            ++telemetry_.submitted;
            ++telemetry_.cancelled;
            telemetry_.maxCompletionQueueDepth =
                std::max<std::uint64_t>(telemetry_.maxCompletionQueueDepth,
                    static_cast<std::uint64_t>(completions_.pending()));
            recordSubmitLatency();
            state.release();
            return FileIoStatus::Accepted;
        }
        ++telemetry_.rejected;
        recordSubmitLatency();
        return post.status == runtime::CompletionStatus::QueueFull ? FileIoStatus::QueueFull
                                                                   : FileIoStatus::ServiceClosed;
    }
    const auto status = backend_.readAsync(request, completion, completions_);
    if (status == FileIoStatus::Accepted) {
        ++telemetry_.submitted;
        telemetry_.maxCompletionQueueDepth = std::max<std::uint64_t>(
            telemetry_.maxCompletionQueueDepth, static_cast<std::uint64_t>(completions_.pending()));
    } else if (status == FileIoStatus::Cancelled) {
        ++telemetry_.cancelled;
    } else {
        ++telemetry_.rejected;
    }
    recordSubmitLatency();
    return status;
}

FileServiceTelemetry FileService::telemetry() const noexcept
{
    return telemetry_;
}

void FileService::shutdown() noexcept
{
    closed_ = true;
}

} // namespace arp::core::io
