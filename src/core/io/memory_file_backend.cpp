#include "core/io/memory_file_backend.h"

#include "core/hash/sha256.h"
#include "core/io/file_query_contract.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/utf8.h"

#include <algorithm>
#include <new>
#include <utility>

namespace arp::core::io {

struct MemoryFileBackend::State {
    std::uint64_t scope = FileQueryContract::acquireBackendScope();
    bool closed = false;
    std::size_t inFlight = 0;
    std::uint64_t queuedBytes = 0;
};

struct MemoryFileBackend::PendingQuery {
    core::SharedPtr<State> owner;
    FileQueryCompletion completion;
    runtime::CancellationToken cancellation;
    FileQueryResult result;
    std::uint64_t budget = 0;
};

struct MemoryFileBackend::Pending {
    core::SharedPtr<State> owner;
    FileReadCompletion completion;
    runtime::CancellationToken cancellation;
    FileReadResult result;
    std::uint64_t bytes = 0;
};

MemoryFileBackend::MemoryFileBackend(MemoryFileBackendDesc desc)
    : desc_(desc), state_(core::makeShared<State>())
{
    if (!state_->scope || !desc.maxFiles || !desc.maxResidentBytes || !desc.maxInFlight
        || !desc.maxQueuedBytes || !desc.maxPathBytes) {
        diagnostic_ = "IO-MEMORY-INVALID-BUDGET";
        state_->closed = true;
        return;
    }
    files_.reserve(desc.maxFiles);
}

MemoryFileBackend::~MemoryFileBackend()
{
    shutdown();
}

FileIoStatus MemoryFileBackend::registerFile(
    std::string_view path, const std::uint8_t* bytes, std::size_t size)
{
    return registerEntry(path, FileEntryKind::RegularFile, bytes, size);
}

FileIoStatus MemoryFileBackend::registerDirectory(std::string_view path)
{
    return registerEntry(path, FileEntryKind::Directory, nullptr, 0);
}

FileIoStatus MemoryFileBackend::registerEntry(
    std::string_view path, FileEntryKind kind, const std::uint8_t* bytes, std::size_t size)
{
    diagnostic_ = nullptr;
    if (state_->closed) {
        diagnostic_ = "IO-MEMORY-CLOSED";
        return FileIoStatus::ServiceClosed;
    }
    if (!core::FilePath::isSafeRelativePath(path, desc_.maxPathBytes)
        || path.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(path)
        || (!bytes && size)) {
        diagnostic_ = "IO-MEMORY-INVALID-FILE";
        return FileIoStatus::InvalidRequest;
    }
    for (const auto& file : files_) {
        if (file.path == path) {
            diagnostic_ = "IO-MEMORY-DUPLICATE-FILE";
            return FileIoStatus::InvalidRequest;
        }
    }
    const auto slash = path.rfind('/');
    if (slash != std::string_view::npos) {
        const auto parent = path.substr(0, slash);
        const auto found = std::find_if(files_.begin(), files_.end(), [&](const File& entry) {
            return entry.path == parent && entry.metadata.kind == FileEntryKind::Directory;
        });
        if (found == files_.end()) {
            diagnostic_ = "IO-MEMORY-PARENT-NOT-REGISTERED";
            return FileIoStatus::InvalidRequest;
        }
    }
    if (files_.size() >= desc_.maxFiles || size > desc_.maxResidentBytes - residentBytes_) {
        diagnostic_ = "IO-MEMORY-RESIDENT-BUDGET";
        return FileIoStatus::BudgetExceeded;
    }
    File file;
    file.path.assign(path.data(), path.size());
    file.metadata.kind = kind;
    file.metadata.sizeBytes = size;
    if (size)
        file.bytes.assign(bytes, bytes + size);
    if (kind == FileEntryKind::RegularFile) {
        const auto hash = core::Sha256::digest(
            {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()});
        file.metadata.version.kind = FileVersionKind::ContentSha256;
        std::copy(hash.begin(), hash.end(), file.metadata.version.digest.begin());
    }
    files_.push_back(std::move(file));
    residentBytes_ += size;
    return FileIoStatus::Success;
}

FileBackendCapability MemoryFileBackend::capability() const noexcept
{
    return {FileBackendKind::MemoryFileBackend,
        FileBackendStatus::PrototypeComplete,
        false,
        true,
        true,
        true,
        true,
        true};
}

FileReadResult MemoryFileBackend::validateRead(const FileReadRequest& request) const
{
    FileReadResult result;
    result.backendKind = FileBackendKind::MemoryFileBackend;
    if (state_->closed) {
        result.status = FileIoStatus::ServiceClosed;
        result.diagnosticId = "IO-MEMORY-CLOSED";
        return result;
    }
    const auto validation = FileReadContract::validate(request);
    result.status = validation.status;
    if (validation.diagnostic) {
        result.diagnostics = validation.diagnostic;
        return result;
    }
    if (request.pathScope != FilePathScope::MountedResource) {
        result.status = FileIoStatus::InvalidPathScope;
        result.diagnostics = "IO-MEMORY-PATH-SCOPE";
    } else if (!core::FilePath::isSafeRelativePath(request.path, desc_.maxPathBytes)
        || core::Utf8Decoder::validate(request.path)) {
        result.status = FileIoStatus::InvalidRequest;
        result.diagnostics = "IO-MEMORY-PATH";
    }
    return result;
}

FileIoStatus MemoryFileBackend::readAsync(const FileReadRequest& request,
    FileReadCompletion completion,
    runtime::CompletionQueue& completions) noexcept
{
    if (!completion.valid())
        return FileIoStatus::InvalidRequest;
    if (state_->closed)
        return FileIoStatus::ServiceClosed;
    if (state_->inFlight >= desc_.maxInFlight)
        return FileIoStatus::QueueFull;
    try {
        auto validation = validateRead(request);
        if (validation.status != FileIoStatus::Success)
            return validation.status;
        auto reservation = completions.reserve();
        if (!reservation.valid())
            return reservation.status() == runtime::CompletionStatus::QueueFull
                ? FileIoStatus::QueueFull
                : FileIoStatus::ServiceClosed;
        auto pending = core::makeUnique<Pending>();
        pending->owner = state_;
        pending->completion = completion;
        pending->cancellation = request.cancellation;
        pending->result.backendKind = FileBackendKind::MemoryFileBackend;
        const auto found = std::find_if(files_.begin(), files_.end(), [&](const File& file) {
            return file.path == request.path;
        });
        const auto fail = [&](FileIoStatus status, const char* diagnostic) {
            pending->result.status = status;
            pending->result.diagnosticId = diagnostic;
            pending->result.diagnostics = diagnostic;
        };
        if (request.cancellation.cancelled()) {
            fail(FileIoStatus::Cancelled, "IO-MEMORY-CANCELLED");
        } else if (found == files_.end()) {
            fail(FileIoStatus::MissingFile, "IO-MEMORY-FILE-NOT-REGISTERED");
        } else if (found->metadata.kind != FileEntryKind::RegularFile) {
            fail(FileIoStatus::InvalidRequest, "IO-MEMORY-READ-DIRECTORY");
        } else if (request.expectedVersion.kind != FileVersionKind::None
            && found->bytes.size() > request.maxVersionBytes) {
            fail(FileIoStatus::BudgetExceeded, "IO-MEMORY-VERSION-BUDGET");
        } else if (request.expectedVersion.kind != FileVersionKind::None
            && found->metadata.version != request.expectedVersion) {
            fail(FileIoStatus::VersionMismatch, "IO-MEMORY-CONTENT-VERSION-MISMATCH");
        } else {
            const auto range = FileReadContract::resolveRange(request, found->bytes.size());
            if (range.diagnostic) {
                fail(range.status, range.diagnostic);
            } else {
                if (range.size > desc_.maxQueuedBytes - state_->queuedBytes) {
                    return FileIoStatus::BudgetExceeded;
                }
                pending->result.bytes.assign(found->bytes.begin() + range.offset,
                    found->bytes.begin() + range.offset + range.size);
                pending->bytes = range.size;
                pending->result.version = found->metadata.version;
            }
        }
        const auto post =
            completions.postReserved(std::move(reservation), {deliver, pending.get()});
        if (!post.accepted())
            return FileIoStatus::ServiceClosed;
        ++state_->inFlight;
        state_->queuedBytes += pending->bytes;
        pending.release();
        return FileIoStatus::Accepted;
    } catch (const std::bad_alloc&) {
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        return FileIoStatus::InvalidRequest;
    }
}

void MemoryFileBackend::deliver(void* context) noexcept
{
    core::UniquePtr<Pending> pending{static_cast<Pending*>(context),
        core::CpuAllocatedDeleter<Pending>{context, &core::destroyCpuAllocatedUnique<Pending>}};
    --pending->owner->inFlight;
    pending->owner->queuedBytes -= pending->bytes;
    if (pending->cancellation.cancelled() || pending->owner->closed) {
        pending->result.status = pending->cancellation.cancelled() ? FileIoStatus::Cancelled
                                                                   : FileIoStatus::ServiceClosed;
        pending->result.diagnosticId =
            pending->cancellation.cancelled() ? "IO-MEMORY-CANCELLED" : "IO-MEMORY-CLOSED";
        pending->result.bytes.clear();
        pending->result.version = {};
        // The status is sufficient; terminal dispatch must not allocate.
        pending->result.diagnostics.clear();
    }
    pending->completion.complete(pending->completion.userData, std::move(pending->result));
}

void MemoryFileBackend::shutdown() noexcept
{
    state_->closed = true;
}

MemoryFileBackendTelemetry MemoryFileBackend::telemetry() const noexcept
{
    const auto directories =
        static_cast<std::size_t>(std::count_if(files_.begin(), files_.end(), [](const File& file) {
            return file.metadata.kind == FileEntryKind::Directory;
        }));
    return {files_.size() - directories,
        residentBytes_,
        state_->inFlight,
        state_->queuedBytes,
        directories};
}

FileIoStatus MemoryFileBackend::queryAsync(const FileQueryRequest& request,
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
    if (state_->inFlight >= desc_.maxInFlight)
        return FileIoStatus::QueueFull;
    const auto budget = FileQueryContract::reservationBytes(request);
    if (budget > desc_.maxQueuedBytes - state_->queuedBytes)
        return FileIoStatus::BudgetExceeded;
    try {
        auto reservation = queue.reserve();
        if (!reservation.valid())
            return reservation.status() == runtime::CompletionStatus::QueueFull
                ? FileIoStatus::QueueFull
                : FileIoStatus::ServiceClosed;
        auto pending = core::makeUnique<PendingQuery>();
        pending->owner = state_;
        pending->completion = completion;
        pending->cancellation = request.cancellation;
        pending->budget = budget;
        const auto found = std::find_if(files_.begin(), files_.end(), [&](const File& file) {
            return file.path == request.path;
        });
        if (request.cancellation.cancelled()) {
            pending->result.fail(FileIoStatus::Cancelled, "IO-MEMORY-CANCELLED");
        } else if (!request.path.empty() && found == files_.end()) {
            pending->result.fail(FileIoStatus::MissingFile, "IO-MEMORY-FILE-NOT-REGISTERED");
        } else if (request.operation == FileQueryOperation::Stat) {
            pending->result.metadata = request.path.empty()
                ? FileMetadata{FileEntryKind::Directory, 0, {}}
                : found->metadata;
            if (request.versionPolicy == FileVersionPolicy::ContentSha256
                && pending->result.metadata.kind != FileEntryKind::RegularFile)
                pending->result.fail(
                    FileIoStatus::Unsupported, "IO-MEMORY-CONTENT-VERSION-REQUIRES-FILE");
            else if (request.versionPolicy == FileVersionPolicy::ContentSha256
                && pending->result.metadata.sizeBytes > request.maxVersionBytes)
                pending->result.fail(FileIoStatus::BudgetExceeded, "IO-MEMORY-VERSION-BUDGET");
            else if (FileQueryContract::resultBytes(pending->result) > request.maxResultBytes)
                pending->result.fail(FileIoStatus::BudgetExceeded, "IO-MEMORY-METADATA-BUDGET");
        } else if (!request.path.empty() && found->metadata.kind != FileEntryKind::Directory) {
            pending->result.fail(FileIoStatus::InvalidRequest, "IO-MEMORY-ENUMERATE-FILE");
        } else {
            core::pmr::String prefix(request.path, &core::defaultCpuMemoryResource());
            if (!prefix.empty())
                prefix.push_back('/');
            core::pmr::Vector<FileDirectoryEntry> entries{&core::defaultCpuMemoryResource()};
            entries.reserve(std::min<std::size_t>(files_.size(), request.maxScanEntries));
            std::uint64_t names = 0;
            for (const auto& entry : files_) {
                if (entry.path.size() <= prefix.size()
                    || entry.path.compare(0, prefix.size(), prefix) != 0)
                    continue;
                const std::string_view name(
                    entry.path.data() + prefix.size(), entry.path.size() - prefix.size());
                if (name.find('/') != std::string_view::npos)
                    continue;
                if (entries.size() >= request.maxScanEntries
                    || name.size() > request.maxScanNameBytes - names) {
                    pending->result.fail(FileIoStatus::BudgetExceeded, "IO-MEMORY-SCAN-BUDGET");
                    break;
                }
                FileDirectoryEntry value;
                value.name.assign(name.data(), name.size());
                value.metadata = entry.metadata;
                names += name.size();
                entries.push_back(std::move(value));
            }
            if (pending->result.status == FileIoStatus::Success)
                pending->result =
                    FileQueryContract::page(request, std::move(entries), state_->scope);
        }
        pending->result.backendKind = FileBackendKind::MemoryFileBackend;
        const auto post = queue.postReserved(std::move(reservation), {deliverQuery, pending.get()});
        if (!post.accepted())
            return FileIoStatus::ServiceClosed;
        ++state_->inFlight;
        state_->queuedBytes += budget;
        pending.release();
        return FileIoStatus::Accepted;
    } catch (const std::bad_alloc&) {
        return FileIoStatus::BudgetExceeded;
    } catch (...) {
        return FileIoStatus::InvalidRequest;
    }
}

void MemoryFileBackend::deliverQuery(void* context) noexcept
{
    core::UniquePtr<PendingQuery> pending{static_cast<PendingQuery*>(context),
        core::CpuAllocatedDeleter<PendingQuery>{
            context, &core::destroyCpuAllocatedUnique<PendingQuery>}};
    --pending->owner->inFlight;
    pending->owner->queuedBytes -= pending->budget;
    if (pending->cancellation.cancelled() || pending->owner->closed)
        pending->result.fail(pending->cancellation.cancelled() ? FileIoStatus::Cancelled
                                                               : FileIoStatus::ServiceClosed,
            "IO-MEMORY-QUERY-TERMINATED");
    pending->completion.complete(pending->completion.userData, std::move(pending->result));
}

} // namespace arp::core::io
