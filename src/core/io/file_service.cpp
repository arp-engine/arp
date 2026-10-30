#include "core/io/file_service.h"

#include "core/io/file_query_contract.h"
#include "core/io/file_read_contract.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <chrono>
#include <new>
#include <utility>

namespace arp::core::io {

namespace {

core::pmr::String diagnostic(const char* text)
{
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

class SubmitLatency final {
public:
    explicit SubmitLatency(FileServiceTelemetry& telemetry) noexcept : telemetry_(telemetry) {}

    ~SubmitLatency()
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started_);
        const auto value = static_cast<std::uint64_t>(elapsed.count());
        telemetry_.totalSubmitLatencyNs += value;
        telemetry_.maxSubmitLatencyNs = std::max(telemetry_.maxSubmitLatencyNs, value);
    }

private:
    FileServiceTelemetry& telemetry_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
};

struct TextReadState {
    TextDecodeDesc desc;
    FileTextReadCompletion completion;
};

void completeTextRead(void* context, FileReadResult source)
{
    core::UniquePtr<TextReadState> state{static_cast<TextReadState*>(context),
        core::CpuAllocatedDeleter<TextReadState>{
            context, &core::destroyCpuAllocatedUnique<TextReadState>}};
    FileTextReadResult result;
    result.source = std::move(source);
    if (result.source.status == FileIoStatus::Success)
        result.decoded = TextDecoder::decode(
            {reinterpret_cast<const char*>(result.source.bytes.data()), result.source.bytes.size()},
            state->desc);
    state->completion.complete(state->completion.userData, std::move(result));
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
    const auto validation = FileReadContract::validate(request);
    if (validation.diagnostic) {
        result.status = validation.status;
        result.diagnostics = validation.diagnostic;
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
        state->completion.complete(state->completion.userData, std::move(state->result));
    };

    if (!completion.valid()) {
        return FileIoStatus::InvalidRequest;
    }
    try {
        auto reservation = completions.reserve();
        if (!reservation.valid())
            return reservation.status() == runtime::CompletionStatus::QueueFull
                ? FileIoStatus::QueueFull
                : FileIoStatus::ServiceClosed;
        auto state = core::makeUnique<CompletionState>();
        state->completion = completion;
        state->result = validateRead(request);
        auto* raw = state.get();
        const auto post = completions.postReserved(std::move(reservation), {deliver, raw});
        if (post.accepted()) {
            state.release();
            return FileIoStatus::Accepted;
        }
        return post.status == runtime::CompletionStatus::QueueFull ? FileIoStatus::QueueFull
                                                                   : FileIoStatus::ServiceClosed;
    } catch (const std::bad_alloc&) {
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        return FileIoStatus::InvalidRequest;
    }
}

FileService::FileService(runtime::CompletionQueue& completions, FileBackend& backend) noexcept
    : completions_(completions), backend_(backend)
{}

FileBackendCapability FileService::capability() const noexcept
{
    return backend_.capability();
}

FileIoStatus FileService::readText(
    const FileTextReadRequest& request, FileTextReadCompletion completion) noexcept
{
    if (closed_) {
        ++telemetry_.rejected;
        return FileIoStatus::ServiceClosed;
    }
    const auto text = TextDecoder::validate(request.text);
    if (!completion.valid() || request.file.contractVersion != 1
        || text != TextDecodeStatus::Success) {
        ++telemetry_.rejected;
        return text == TextDecodeStatus::UnsupportedEncoding ? FileIoStatus::Unsupported
                                                             : FileIoStatus::InvalidRequest;
    }
    try {
        auto state = core::makeUnique<TextReadState>();
        state->desc = request.text;
        state->completion = completion;
        const auto submitted = read(request.file, {completeTextRead, state.get()});
        if (submitted == FileIoStatus::Accepted)
            state.release();
        return submitted;
    } catch (...) {
        ++telemetry_.rejected;
        return FileIoStatus::BudgetExceeded;
    }
}

FileReadResult FileService::validateRead(const FileReadRequest& request) const
{
    FileReadResult result;
    if (closed_) {
        result.status = FileIoStatus::ServiceClosed;
        result.diagnosticId = "IO-FILE-SERVICE-CLOSED";
        return result;
    }
    const auto validation = FileReadContract::validate(request);
    if (validation.status != FileIoStatus::Success) {
        result.status = validation.status;
        result.diagnosticId = validation.diagnostic;
        return result;
    }
    return backend_.validateRead(request);
}

FileIoStatus FileService::read(
    const FileReadRequest& request, FileReadCompletion completion) noexcept
{
    const SubmitLatency latency(telemetry_);

    if (closed_) {
        ++telemetry_.rejected;
        return FileIoStatus::ServiceClosed;
    }
    if (!completion.valid()) {
        ++telemetry_.rejected;
        return FileIoStatus::InvalidRequest;
    }
    if (request.contractVersion) {
        const auto validation = FileReadContract::validate(request);
        if (validation.diagnostic) {
            ++telemetry_.rejected;
            return validation.status;
        }
    }
    if (request.expectedVersion.kind != FileVersionKind::None
        && !backend_.capability().supportsContentVersion) {
        ++telemetry_.rejected;
        return FileIoStatus::Unsupported;
    }
    if (request.cancellation.cancelled()) {
        try {
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
                state->completion.complete(state->completion.userData, std::move(state->result));
            };

            auto reservation = completions_.reserve();
            if (!reservation.valid()) {
                ++telemetry_.rejected;
                return reservation.status() == runtime::CompletionStatus::QueueFull
                    ? FileIoStatus::QueueFull
                    : FileIoStatus::ServiceClosed;
            }
            auto state = core::makeUnique<CompletionState>();
            state->completion = completion;
            state->result.status = FileIoStatus::Cancelled;
            state->result.backendKind = backend_.capability().kind;
            state->result.diagnostics = diagnostic("file read cancelled before scheduling");
            auto* raw = state.get();
            const auto post = completions_.postReserved(std::move(reservation), {deliver, raw});
            if (post.accepted()) {
                ++telemetry_.submitted;
                ++telemetry_.cancelled;
                telemetry_.maxCompletionQueueDepth =
                    std::max<std::uint64_t>(telemetry_.maxCompletionQueueDepth,
                        static_cast<std::uint64_t>(completions_.pending()));
                state.release();
                return FileIoStatus::Accepted;
            }
            ++telemetry_.rejected;
            return post.status == runtime::CompletionStatus::QueueFull
                ? FileIoStatus::QueueFull
                : FileIoStatus::ServiceClosed;
        } catch (const std::bad_alloc&) {
            ++telemetry_.rejected;
            return FileIoStatus::BudgetExceeded;
        } catch (...) {
            ++telemetry_.rejected;
            return FileIoStatus::InvalidRequest;
        }
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
    return status;
}

FileServiceTelemetry FileService::telemetry() const noexcept
{
    return telemetry_;
}

FileIoStatus FileService::query(
    const FileQueryRequest& request, FileQueryCompletion completion) noexcept
{
    const SubmitLatency latency(telemetry_);
    if (closed_) {
        ++telemetry_.rejected;
        return FileIoStatus::ServiceClosed;
    }
    const auto validation = FileQueryContract::validate(request);
    if (!completion.valid() || validation.diagnostic) {
        ++telemetry_.rejected;
        return completion.valid() ? validation.status : FileIoStatus::InvalidRequest;
    }
    const auto caps = backend_.capability();
    if ((request.operation == FileQueryOperation::Stat && !caps.supportsMetadata)
        || (request.operation == FileQueryOperation::Enumerate && !caps.supportsEnumeration)
        || (request.versionPolicy == FileVersionPolicy::ContentSha256
            && !caps.supportsContentVersion)) {
        ++telemetry_.rejected;
        return FileIoStatus::Unsupported;
    }
    const auto status = backend_.queryAsync(request, completion, completions_);
    if (status == FileIoStatus::Accepted) {
        ++telemetry_.submitted;
        telemetry_.maxCompletionQueueDepth =
            std::max<std::uint64_t>(telemetry_.maxCompletionQueueDepth, completions_.pending());
    } else if (status == FileIoStatus::Cancelled) {
        ++telemetry_.cancelled;
    } else {
        ++telemetry_.rejected;
    }
    return status;
}

void FileService::shutdown() noexcept
{
    closed_ = true;
}

} // namespace arp::core::io
