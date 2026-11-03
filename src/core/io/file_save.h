#pragma once

#include "core/io/file_request.h"
#include "core/io/file_result.h"

namespace arp::core::io {

enum class FileSavePrecondition : std::uint8_t { MustBeMissing, ContentSha256 };
enum class FileCommitState : std::uint8_t { NotCommitted, Committed, OutcomeUnknown };

struct FileSaveCapability {
    bool observedBaselineReplace = false;
    bool strictConditionalReplace = false;
    bool operationReceipts = false;
};

// Opaque file-domain identity. Scope is never reused by its owning backend;
// session/request digests bind a remote journal result to this exact operation.
struct FileSaveReceipt {
    std::uint64_t scope = 0, operationId = 0;
    FileVersionToken sessionVersion, requestVersion;

    bool valid() const noexcept
    {
        return scope && operationId && sessionVersion.kind == FileVersionKind::ContentSha256
            && requestVersion.kind == FileVersionKind::ContentSha256 && sessionVersion.valid()
            && requestVersion.valid();
    }

    bool operator==(const FileSaveReceipt& other) const noexcept
    {
        return scope == other.scope && operationId == other.operationId
            && sessionVersion == other.sessionVersion && requestVersion == other.requestVersion;
    }
};

struct FileSaveRequest {
    std::uint32_t contractVersion = 1;
    FilePathScope pathScope = FilePathScope::Unspecified;
    core::PoolHandle mount;
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
    FileSavePrecondition precondition = FileSavePrecondition::MustBeMissing;
    FileVersionToken expectedVersion;
    std::uint64_t maxWriteBytes = 0;
    std::uint64_t maxBaselineBytes = 0;
    runtime::CancellationToken cancellation;
};

struct FileSaveResult {
    FileIoStatus status = FileIoStatus::BackendUnavailable;
    // Freshness/cancellation of delivery, independent from the actual commit.
    FileIoStatus deliveryStatus = FileIoStatus::Success;
    FileCommitState commitState = FileCommitState::NotCommitted;
    FileBackendKind backendKind = FileBackendKind::NullFileBackend;
    const char* diagnosticId = nullptr;
    FileVersionToken committedVersion;
    FileSaveReceipt receipt;

    void fail(FileIoStatus failure, const char* diagnostic) noexcept
    {
        status = failure;
        diagnosticId = diagnostic;
    }
};

struct FileSaveCompletion {
    using Complete = void (*)(void*, FileSaveResult);
    Complete complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

} // namespace arp::core::io
