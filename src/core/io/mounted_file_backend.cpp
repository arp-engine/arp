#include "core/io/mounted_file_backend.h"

#include "core/io/file_query_contract.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/utf8.h"

#include <new>
#include <utility>

namespace arp::core::io {

struct MountedFileBackend::State {
    struct Mount {
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        core::SharedPtr<ManagedFileBackend> backend;
    };

    struct Pending {
        core::PoolHandle mount;
        core::SharedPtr<ManagedFileBackend> backend;
        FileReadCompletion completion;
        runtime::CancellationToken cancellation;
        std::uint64_t budget = 0;
        FileQueryCompletion queryCompletion;
    };

    State(std::uint32_t mountsCount, std::uint32_t requestsCount)
        : mounts(mountsCount), pending(requestsCount)
    {}

    core::GenerationPool<Mount> mounts;
    core::GenerationPool<Pending> pending;
    std::uint64_t reservedReadBytes = 0;
    bool closed = false;
};

struct MountedFileBackend::Completion {
    core::SharedPtr<State> owner;
    core::PoolHandle pending;
};

MountedFileBackend::MountedFileBackend(MountedFileBackendDesc desc)
    : desc_(desc), state_(core::makeShared<State>(desc.maxMounts, desc.maxInFlight))
{
    if (!desc.maxMounts || !desc.maxInFlight || !desc.maxReadBytesInFlight || !desc.maxNameBytes
        || !desc.maxPathBytes) {
        diagnostic_ = "IO-MOUNT-INVALID-BUDGET";
        state_->closed = true;
    }
}

MountedFileBackend::~MountedFileBackend()
{
    shutdown();
}

core::PoolHandle MountedFileBackend::mount(
    std::string_view name, core::SharedPtr<ManagedFileBackend> backend)
{
    diagnostic_ = nullptr;
    if (state_->closed) {
        diagnostic_ = "IO-MOUNT-CLOSED";
        return {};
    }
    if (name.empty() || name.size() > desc_.maxNameBytes
        || name.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
            != std::string_view::npos
        || !backend || backend.get() == this
        || !backend->capability().completesThroughCompletionQueue
        || backend->capability().kind == FileBackendKind::MountedFileBackend) {
        diagnostic_ = "IO-MOUNT-INVALID-DESCRIPTOR";
        return {};
    }
    if (findMount(name)) {
        diagnostic_ = "IO-MOUNT-DUPLICATE-NAME";
        return {};
    }
    State::Mount entry;
    entry.name.assign(name.data(), name.size());
    entry.backend = std::move(backend);
    const auto handle = state_->mounts.emplace(std::move(entry));
    if (!handle)
        diagnostic_ = state_->mounts.diagnostic();
    return handle;
}

bool MountedFileBackend::unmount(core::PoolHandle handle) noexcept
{
    return !state_->closed && state_->mounts.erase(handle);
}

core::PoolHandle MountedFileBackend::findMount(std::string_view name) const noexcept
{
    core::PoolHandle found;
    if (!state_->closed)
        state_->mounts.forEach([&](core::PoolHandle handle, const State::Mount& mount) {
            if (mount.name == name)
                found = handle;
        });
    return found;
}

FileBackendCapability MountedFileBackend::capability() const noexcept
{
    return {FileBackendKind::MountedFileBackend,
        FileBackendStatus::PrototypeComplete,
        false,
        true,
        true,
        true,
        true,
        true};
}

FileReadResult MountedFileBackend::validateRead(const FileReadRequest& request) const
{
    FileReadResult result;
    result.backendKind = FileBackendKind::MountedFileBackend;
    if (state_->closed) {
        result.status = FileIoStatus::ServiceClosed;
        result.diagnosticId = "IO-MOUNT-CLOSED";
        return result;
    }
    const auto validation = FileReadContract::validate(request);
    result.status = validation.status;
    if (validation.diagnostic) {
        result.diagnostics = validation.diagnostic;
    } else if (request.contractVersion != 1
        || request.pathScope != FilePathScope::MountedResource) {
        result.status = FileIoStatus::InvalidRequest;
        result.diagnostics = "IO-MOUNT-BOUNDED-REQUEST-REQUIRED";
    } else if (!core::FilePath::isSafeRelativePath(request.path, desc_.maxPathBytes)
        || core::Utf8Decoder::validate(request.path)) {
        result.status = FileIoStatus::InvalidRequest;
        result.diagnostics = "IO-MOUNT-RELATIVE-PATH";
    } else if (!state_->mounts.get(request.mount)) {
        result.status = FileIoStatus::StaleLocation;
        result.diagnostics = "IO-MOUNT-STALE-LOCATION";
    }
    if (result.status == FileIoStatus::Success) {
        auto backend = state_->mounts.get(request.mount)->backend->validateRead(request);
        backend.backendKind = result.backendKind;
        return backend;
    }
    return result;
}

FileIoStatus MountedFileBackend::readAsync(const FileReadRequest& request,
    FileReadCompletion completion,
    runtime::CompletionQueue& completions) noexcept
{
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (state_->closed)
        return FileIoStatus::ServiceClosed;
    core::PoolHandle ticket;
    try {
        const auto validation = validateRead(request);
        if (validation.status != FileIoStatus::Success)
            return validation.status;
        if (state_->pending.liveCount() >= desc_.maxInFlight)
            return FileIoStatus::QueueFull;
        if (request.maxBytes > desc_.maxReadBytesInFlight - state_->reservedReadBytes)
            return FileIoStatus::BudgetExceeded;
        const auto backend = state_->mounts.get(request.mount)->backend;
        if (request.expectedVersion.kind != FileVersionKind::None
            && !backend->capability().supportsContentVersion)
            return FileIoStatus::Unsupported;
        auto callback = core::makeUnique<Completion>();
        callback->owner = state_;
        ticket = state_->pending.emplace(State::Pending{
            request.mount, backend, completion, request.cancellation, request.maxBytes});
        if (!ticket)
            return FileIoStatus::QueueFull;
        callback->pending = ticket;
        state_->reservedReadBytes += request.maxBytes;
        const auto status = backend->readAsync(request, {complete, callback.get()}, completions);
        if (status == FileIoStatus::Accepted) {
            callback.release();
            return status;
        }
        state_->pending.erase(ticket);
        state_->reservedReadBytes -= request.maxBytes;
        return status;
    } catch (const std::bad_alloc&) {
        if (ticket) {
            state_->pending.erase(ticket);
            state_->reservedReadBytes -= request.maxBytes;
        }
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        if (ticket) {
            state_->pending.erase(ticket);
            state_->reservedReadBytes -= request.maxBytes;
        }
        return FileIoStatus::InvalidRequest;
    }
}

void MountedFileBackend::complete(void* context, FileReadResult result)
{
    core::UniquePtr<Completion> callback{static_cast<Completion*>(context),
        core::CpuAllocatedDeleter<Completion>{
            context, &core::destroyCpuAllocatedUnique<Completion>}};
    auto& state = *callback->owner;
    const auto* pending = state.pending.get(callback->pending);
    if (!pending)
        return;
    const auto completion = pending->completion;
    if (pending->cancellation.cancelled() || state.closed || !state.mounts.get(pending->mount)) {
        result.status = pending->cancellation.cancelled() ? FileIoStatus::Cancelled
            : state.closed                                ? FileIoStatus::ServiceClosed
                                                          : FileIoStatus::StaleLocation;
        result.diagnosticId = pending->cancellation.cancelled() ? "IO-MOUNT-CANCELLED"
            : state.closed                                      ? "IO-MOUNT-CLOSED"
                                                                : "IO-MOUNT-STALE-LOCATION";
        result.bytes.clear();
        result.version = {};
        result.diagnostics.clear();
    }
    state.reservedReadBytes -= pending->budget;
    state.pending.erase(callback->pending);
    completion.complete(completion.userData, std::move(result));
}

void MountedFileBackend::shutdown() noexcept
{
    if (state_->closed)
        return;
    state_->closed = true;
    state_->mounts.forEach([](core::PoolHandle, State::Mount& mount) {
        mount.backend->shutdown();
    });
    // Removed mounts may still have accepted transports. Quiesce those too.
    state_->pending.forEach([](core::PoolHandle, State::Pending& pending) {
        pending.backend->shutdown();
    });
}

MountedFileBackendTelemetry MountedFileBackend::telemetry() const noexcept
{
    return {state_->mounts.liveCount(), state_->pending.liveCount(), state_->reservedReadBytes};
}

FileIoStatus MountedFileBackend::queryAsync(const FileQueryRequest& request,
    FileQueryCompletion completion,
    runtime::CompletionQueue& queue) noexcept
{
    if (state_->closed)
        return FileIoStatus::ServiceClosed;
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    const auto validation = FileQueryContract::validate(request);
    if (validation.diagnostic)
        return validation.status;
    if (request.path.size() > desc_.maxPathBytes)
        return FileIoStatus::BudgetExceeded;
    const auto* mount = state_->mounts.get(request.mount);
    if (!mount)
        return FileIoStatus::StaleLocation;
    const auto capability = mount->backend->capability();
    if ((request.operation == FileQueryOperation::Stat && !capability.supportsMetadata)
        || (request.operation == FileQueryOperation::Enumerate && !capability.supportsEnumeration)
        || (request.versionPolicy == FileVersionPolicy::ContentSha256
            && !capability.supportsContentVersion))
        return FileIoStatus::Unsupported;
    if (state_->pending.liveCount() >= desc_.maxInFlight)
        return FileIoStatus::QueueFull;
    const auto budget = FileQueryContract::reservationBytes(request);
    if (budget > desc_.maxReadBytesInFlight - state_->reservedReadBytes)
        return FileIoStatus::BudgetExceeded;
    core::PoolHandle ticket;
    try {
        auto callback = core::makeUnique<Completion>();
        callback->owner = state_;
        State::Pending pending;
        pending.mount = request.mount;
        pending.backend = mount->backend;
        pending.queryCompletion = completion;
        pending.cancellation = request.cancellation;
        pending.budget = budget;
        ticket = state_->pending.emplace(std::move(pending));
        if (!ticket)
            return FileIoStatus::QueueFull;
        callback->pending = ticket;
        state_->reservedReadBytes += budget;
        const auto status =
            mount->backend->queryAsync(request, {completeQuery, callback.get()}, queue);
        if (status == FileIoStatus::Accepted) {
            callback.release();
            return status;
        }
        state_->pending.erase(ticket);
        state_->reservedReadBytes -= budget;
        return status;
    } catch (const std::bad_alloc&) {
        if (ticket) {
            state_->pending.erase(ticket);
            state_->reservedReadBytes -= budget;
        }
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        if (ticket) {
            state_->pending.erase(ticket);
            state_->reservedReadBytes -= budget;
        }
        return FileIoStatus::InvalidRequest;
    }
}

void MountedFileBackend::completeQuery(void* context, FileQueryResult result)
{
    core::UniquePtr<Completion> callback{static_cast<Completion*>(context),
        core::CpuAllocatedDeleter<Completion>{
            context, &core::destroyCpuAllocatedUnique<Completion>}};
    auto& state = *callback->owner;
    const auto* pending = state.pending.get(callback->pending);
    if (!pending)
        return;
    const auto completion = pending->queryCompletion;
    if (pending->cancellation.cancelled() || state.closed || !state.mounts.get(pending->mount))
        result.fail(pending->cancellation.cancelled() ? FileIoStatus::Cancelled
                : state.closed                        ? FileIoStatus::ServiceClosed
                                                      : FileIoStatus::StaleLocation,
            "IO-MOUNT-QUERY-TERMINATED");
    state.reservedReadBytes -= pending->budget;
    state.pending.erase(callback->pending);
    completion.complete(completion.userData, std::move(result));
}

} // namespace arp::core::io
