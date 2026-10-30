#pragma once

#include "core/io/file_backend.h"

#include <string_view>

namespace arp::core::io {

struct MemoryFileBackendDesc {
    // Historical field name; bounds all explicit file/directory registrations.
    std::size_t maxFiles = 0;
    std::uint64_t maxResidentBytes = 0;
    std::size_t maxInFlight = 0;
    std::uint64_t maxQueuedBytes = 0;
    std::size_t maxPathBytes = 0;
};

struct MemoryFileBackendTelemetry {
    std::size_t files = 0;
    std::uint64_t residentBytes = 0;
    std::size_t inFlight = 0;
    std::uint64_t queuedBytes = 0;
    std::size_t directories = 0;
};

// Owner-thread backend. Files are explicitly registered immutable byte arrays;
// it never probes a host path or fabricates a missing file.
class ARP_API MemoryFileBackend final : public ManagedFileBackend {
public:
    explicit MemoryFileBackend(MemoryFileBackendDesc desc);
    ~MemoryFileBackend() override;
    MemoryFileBackend(const MemoryFileBackend&) = delete;
    MemoryFileBackend& operator=(const MemoryFileBackend&) = delete;

    FileIoStatus registerFile(std::string_view path, const std::uint8_t* bytes, std::size_t size);
    FileIoStatus registerDirectory(std::string_view path);
    FileBackendCapability capability() const noexcept override;
    FileReadResult validateRead(const FileReadRequest& request) const override;
    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
    FileIoStatus queryAsync(const FileQueryRequest& request,
        FileQueryCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
    void shutdown() noexcept override;
    MemoryFileBackendTelemetry telemetry() const noexcept;

    const char* diagnostic() const noexcept { return diagnostic_; }

private:
    struct State;
    struct Pending;
    struct PendingQuery;

    struct File {
        core::pmr::String path{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
        FileMetadata metadata;
    };

    static void deliver(void* context) noexcept;
    static void deliverQuery(void* context) noexcept;
    FileIoStatus registerEntry(
        std::string_view path, FileEntryKind kind, const std::uint8_t* bytes, std::size_t size);
    MemoryFileBackendDesc desc_;
    core::SharedPtr<State> state_;
    core::pmr::Vector<File> files_{&core::defaultCpuMemoryResource()};
    std::uint64_t residentBytes_ = 0;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::core::io
