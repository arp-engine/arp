#include "core/io/native_directory_file_backend.h"

#include "core/io/detail/native_directory_access.h"
#include "core/io/file_query_contract.h"

#include <algorithm>
#include <atomic>
#include <new>
#include <utility>

namespace arp::core::io {

struct NativeDirectoryFileBackend::State {
    explicit State(NativeDirectoryFileBackendDesc config) : desc(std::move(config)), access(desc) {}

    NativeDirectoryFileBackendDesc desc;
    detail::NativeDirectoryAccess access;
    std::atomic_bool closed{false};
    std::uint32_t inFlight = 0;
    std::uint64_t reservedBytes = 0;
};

struct NativeDirectoryFileBackend::Pending {
    core::SharedPtr<State> owner;
    bool query = false;
    FileReadRequest readRequest;
    FileQueryRequest queryRequest;
    FileReadCompletion readCompletion;
    FileQueryCompletion queryCompletion;
    FileReadResult readResult;
    FileQueryResult queryResult;
    std::uint64_t budget = 0;
};

NativeDirectoryFileBackend::NativeDirectoryFileBackend(NativeDirectoryFileBackendDesc desc)
    : state_(core::makeShared<State>(std::move(desc)))
{
    const auto& config = state_->desc;
    if (!config.maxInFlight || !config.maxBytesInFlight || !config.maxPathBytes
        || !config.ioChunkBytes || !config.directoryBufferBytes || !config.maxScanEntries
        || !config.maxScanNameBytes || config.rootPath.empty()
        || config.rootPath.size() > config.maxPathBytes
        || config.rootPath.find('\0') != core::pmr::String::npos
        || core::Utf8Decoder::validate(config.rootPath)
        || !core::FilePath::isAbsoluteHostPath(config.rootPath)) {
        diagnostic_ = "IO-LOCAL-INVALID-DESCRIPTOR";
        state_->closed = true;
    }
}

NativeDirectoryFileBackend::~NativeDirectoryFileBackend()
{
    shutdown();
}

FileBackendCapability NativeDirectoryFileBackend::capability() const noexcept
{
    const bool supported = state_->access.supported();
    return {FileBackendKind::NativeDirectoryFileBackend,
        FileBackendStatus::PrototypeComplete,
        false,
        true,
        true,
        supported,
        supported,
        supported};
}

FileReadResult NativeDirectoryFileBackend::validateRead(const FileReadRequest& request) const
{
    FileReadResult result;
    result.backendKind = FileBackendKind::NativeDirectoryFileBackend;
    if (state_->closed) {
        result.status = FileIoStatus::ServiceClosed;
        result.diagnosticId = "IO-LOCAL-CLOSED";
        return result;
    }
    auto validation = FileReadContract::validate(request);
    if (!validation.diagnostic
        && (request.contractVersion != 1 || request.pathScope != FilePathScope::MountedResource
            || !core::FilePath::isSafeRelativePath(request.path, state_->desc.maxPathBytes)))
        validation = {FileIoStatus::InvalidRequest, "IO-LOCAL-BOUNDED-RELATIVE-READ-REQUIRED"};
    result.status = validation.status;
    result.diagnosticId = validation.diagnostic;
    return result;
}

FileIoStatus NativeDirectoryFileBackend::start(runtime::CompletionQueue& queue)
{
    if (queue_ && queue_ != &queue)
        return FileIoStatus::InvalidRequest;
    if (!work_) {
        auto work = core::makeUnique<runtime::WorkService>(
            runtime::WorkServiceDesc{&queue, 1, state_->desc.maxInFlight, false, true});
        if (!work->valid())
            return FileIoStatus::BackendUnavailable;
        work_ = std::move(work);
        queue_ = &queue;
    }
    return FileIoStatus::Success;
}

FileIoStatus NativeDirectoryFileBackend::submit(
    core::UniquePtr<Pending> pending, runtime::CompletionQueue& queue)
{
    if (state_->inFlight >= state_->desc.maxInFlight)
        return FileIoStatus::QueueFull;
    if (pending->budget > state_->desc.maxBytesInFlight - state_->reservedBytes)
        return FileIoStatus::BudgetExceeded;
    const auto started = start(queue);
    if (started != FileIoStatus::Success)
        return started;
    pending->owner = state_;
    const auto cancellation =
        pending->query ? pending->queryRequest.cancellation : pending->readRequest.cancellation;
    auto submitted = work_->submit({execute, pending.get(), complete, pending.get(), cancellation});
    if (!submitted.accepted())
        return submitted.status == runtime::WorkServiceStatus::QueueFull ? FileIoStatus::QueueFull
            : submitted.status == runtime::WorkServiceStatus::Cancelled
            ? FileIoStatus::Cancelled
            : FileIoStatus::ServiceClosed;
    ++state_->inFlight;
    state_->reservedBytes += pending->budget;
    pending.release();
    return FileIoStatus::Accepted;
}

FileIoStatus NativeDirectoryFileBackend::readAsync(const FileReadRequest& request,
    FileReadCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    if (state_->closed)
        return FileIoStatus::ServiceClosed;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (!state_->access.supported())
        return FileIoStatus::Unsupported;
    try {
        const auto validation = validateRead(request);
        if (validation.status != FileIoStatus::Success)
            return validation.status;
        const auto scratch =
            request.expectedVersion.kind != FileVersionKind::None ? state_->desc.ioChunkBytes : 0;
        if (request.maxBytes > UINT64_MAX - scratch)
            return FileIoStatus::BudgetExceeded;
        auto pending = core::makeUnique<Pending>();
        pending->readRequest = request;
        pending->readCompletion = completion;
        pending->budget = request.maxBytes + scratch;
        return submit(std::move(pending), queue);
    } catch (const std::bad_alloc&) {
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        return FileIoStatus::BackendUnavailable;
    }
}

FileIoStatus NativeDirectoryFileBackend::queryAsync(const FileQueryRequest& request,
    FileQueryCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    if (state_->closed)
        return FileIoStatus::ServiceClosed;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (!state_->access.supported())
        return FileIoStatus::Unsupported;
    const auto validation = FileQueryContract::validate(request);
    if (validation.diagnostic)
        return validation.status;
    if (request.path.size() > state_->desc.maxPathBytes
        || request.maxScanEntries > state_->desc.maxScanEntries
        || request.maxScanNameBytes > state_->desc.maxScanNameBytes)
        return FileIoStatus::BudgetExceeded;
    const auto reserved = FileQueryContract::reservationBytes(request);
    const auto scratch = std::max(state_->desc.ioChunkBytes, state_->desc.directoryBufferBytes);
    if (reserved > UINT64_MAX - scratch)
        return FileIoStatus::BudgetExceeded;
    try {
        auto pending = core::makeUnique<Pending>();
        pending->query = true;
        pending->queryRequest = request;
        pending->queryCompletion = completion;
        pending->budget = reserved + scratch;
        return submit(std::move(pending), queue);
    } catch (const std::bad_alloc&) {
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        return FileIoStatus::BackendUnavailable;
    }
}

runtime::WorkStatus NativeDirectoryFileBackend::execute(void* context)
{
    auto& pending = *static_cast<Pending*>(context);
    try {
        if (pending.query)
            pending.queryResult = pending.owner->access.query(pending.queryRequest);
        else
            pending.readResult = pending.owner->access.read(pending.readRequest);
    } catch (const std::bad_alloc&) {
        pending.queryResult.fail(FileIoStatus::BudgetExceeded, "IO-LOCAL-ALLOCATION-FAILED");
        pending.readResult.status = FileIoStatus::BudgetExceeded;
        pending.readResult.diagnosticId = "IO-LOCAL-ALLOCATION-FAILED";
    }
    return (pending.query ? pending.queryResult.status : pending.readResult.status)
            == FileIoStatus::Success
        ? runtime::WorkStatus::Completed
        : runtime::WorkStatus::Failed;
}

void NativeDirectoryFileBackend::complete(void* context, runtime::WorkResult work)
{
    core::UniquePtr<Pending> pending{static_cast<Pending*>(context),
        core::CpuAllocatedDeleter<Pending>{context, &core::destroyCpuAllocatedUnique<Pending>}};
    auto& state = *pending->owner;
    --state.inFlight;
    state.reservedBytes -= pending->budget;
    const auto token =
        pending->query ? pending->queryRequest.cancellation : pending->readRequest.cancellation;
    const auto operationStatus =
        pending->query ? pending->queryResult.status : pending->readResult.status;
    const auto status = token.cancelled()               ? FileIoStatus::Cancelled
        : state.closed                                  ? FileIoStatus::ServiceClosed
        : work.status == runtime::WorkStatus::Cancelled ? FileIoStatus::Cancelled
        : work.status == runtime::WorkStatus::Failed && operationStatus == FileIoStatus::Success
        ? FileIoStatus::BackendUnavailable
        : FileIoStatus::Success;
    if (pending->query) {
        if (status != FileIoStatus::Success)
            pending->queryResult.fail(status, "IO-LOCAL-TERMINATED");
        pending->queryResult.backendKind = FileBackendKind::NativeDirectoryFileBackend;
        pending->queryCompletion.complete(
            pending->queryCompletion.userData, std::move(pending->queryResult));
    } else {
        if (status != FileIoStatus::Success) {
            pending->readResult.status = status;
            pending->readResult.diagnosticId = "IO-LOCAL-TERMINATED";
            pending->readResult.bytes.clear();
            pending->readResult.diagnostics.clear();
            pending->readResult.version = {};
        }
        pending->readResult.backendKind = FileBackendKind::NativeDirectoryFileBackend;
        pending->readCompletion.complete(
            pending->readCompletion.userData, std::move(pending->readResult));
    }
}

void NativeDirectoryFileBackend::shutdown() noexcept
{
    if (state_->closed.exchange(true))
        return;
    if (work_)
        work_->shutdown(runtime::WorkShutdownPolicy::CancelPending);
    state_->access.shutdown();
}

NativeDirectoryFileBackendTelemetry NativeDirectoryFileBackend::telemetry() const noexcept
{
    return {state_->inFlight, state_->reservedBytes, state_->access.openObjectCount()};
}

} // namespace arp::core::io
