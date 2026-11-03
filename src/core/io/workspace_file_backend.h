#pragma once

#include "core/io/file_backend.h"

namespace arp::core::io {

struct WorkspaceFileBackendDesc {
    std::uint32_t contractVersion = 1;
    core::pmr::String endpoint, credentialOrigin, workspaceId, token;
    std::size_t maxPathBytes = 0;
    std::uint64_t maxFileBytes = 0;
    std::size_t maxRequestBytes = 0, maxResponseBytes = 0, preparationBytes = 0;
    std::uint64_t maxQueryBytes = 0;
    std::uint32_t maxDirectoryEntries = 0;
    std::uint64_t maxDirectoryNameBytes = 0;
    std::size_t maxHeaderBytes = 0, ioChunkBytes = 0;
    std::uint32_t timeoutMs = 0, cancellationPollMs = 0, maxDnsAddresses = 0;
};

struct WorkspaceFileConnectionResult {
    FileIoStatus status = FileIoStatus::BackendUnavailable;
    const char* diagnosticId = nullptr;
    FileBackendCapability capability;
    FileSaveCapability saveCapability;
};

struct WorkspaceFileConnectionCompletion {
    using Complete = void (*)(void*, WorkspaceFileConnectionResult);
    Complete complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

struct WorkspaceFileBackendTelemetry {
    std::uint32_t inFlight = 0;
    std::uint64_t activeNativeTransfers = 0;
    std::uint64_t receivedBytes = 0;
};

// Owner-thread, single in-flight request, one completion queue. Constructor has
// no IO; connectAsync must verify the actual remote profile before file use.
// File operations and receipt observations then use the ordinary FileService.
class ARP_API WorkspaceFileBackend final : public ManagedFileBackend {
public:
    explicit WorkspaceFileBackend(WorkspaceFileBackendDesc);
    ~WorkspaceFileBackend() override;
    WorkspaceFileBackend(const WorkspaceFileBackend&) = delete;
    WorkspaceFileBackend& operator=(const WorkspaceFileBackend&) = delete;
    FileIoStatus connectAsync(WorkspaceFileConnectionCompletion,
        runtime::CompletionQueue&,
        runtime::CancellationToken = {}) noexcept;
    bool connected() const noexcept;
    const char* diagnostic() const noexcept;
    FileBackendCapability capability() const noexcept override;
    FileSaveCapability saveCapability(core::PoolHandle = {}) const noexcept override;
    FileReadResult validateRead(const FileReadRequest&) const override;
    FileIoStatus readAsync(
        const FileReadRequest&, FileReadCompletion, runtime::CompletionQueue&) noexcept override;
    FileIoStatus queryAsync(
        const FileQueryRequest&, FileQueryCompletion, runtime::CompletionQueue&) noexcept override;
    FileIoStatus saveAsync(
        FileSaveRequest, FileSaveCompletion, runtime::CompletionQueue&) noexcept override;
    FileIoStatus receiptAsync(const FileSaveReceiptRequest&,
        FileSaveReceiptCompletion,
        runtime::CompletionQueue&) noexcept override;
    void shutdown() noexcept override;
    WorkspaceFileBackendTelemetry telemetry() const noexcept;

private:
    struct State;
    struct Pending;
    FileIoStatus admit(runtime::CompletionQueue&) const noexcept;
    FileIoStatus submit(core::UniquePtr<Pending>, runtime::CompletionQueue&) noexcept;
    static void deliver(void*);
    core::SharedPtr<State> state_;
};
} // namespace arp::core::io
