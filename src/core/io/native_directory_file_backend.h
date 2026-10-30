#pragma once

#include "core/io/file_backend.h"
#include "core/runtime/work_service.h"

namespace arp::core::io {

struct NativeDirectoryFileBackendDesc {
    core::pmr::String rootPath{&core::defaultCpuMemoryResource()};
    std::uint32_t maxInFlight = 0;
    std::uint64_t maxBytesInFlight = 0;
    std::size_t maxPathBytes = 0;
    std::size_t ioChunkBytes = 0;
    std::size_t directoryBufferBytes = 0;
    std::uint32_t maxScanEntries = 0;
    std::uint64_t maxScanNameBytes = 0;
};

struct NativeDirectoryFileBackendTelemetry {
    std::uint32_t inFlight = 0;
    std::uint64_t reservedBytes = 0;
    std::uint64_t openNativeObjects = 0;
};

// One owned IO worker, initialized on first submission. The native root and all
// child handles remain private; user callbacks run only on the bound queue.
class ARP_API NativeDirectoryFileBackend final : public ManagedFileBackend {
public:
    explicit NativeDirectoryFileBackend(NativeDirectoryFileBackendDesc desc);
    ~NativeDirectoryFileBackend() override;
    FileBackendCapability capability() const noexcept override;
    FileReadResult validateRead(const FileReadRequest& request) const override;
    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
    FileIoStatus queryAsync(const FileQueryRequest& request,
        FileQueryCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
    void shutdown() noexcept override;
    NativeDirectoryFileBackendTelemetry telemetry() const noexcept;

    const char* diagnostic() const noexcept { return diagnostic_; }

private:
    struct State;
    struct Pending;
    static runtime::WorkStatus execute(void* context);
    static void complete(void* context, runtime::WorkResult result);
    FileIoStatus start(runtime::CompletionQueue& queue);
    FileIoStatus submit(core::UniquePtr<Pending> pending, runtime::CompletionQueue& queue);
    core::SharedPtr<State> state_;
    core::UniquePtr<runtime::WorkService> work_;
    runtime::CompletionQueue* queue_ = nullptr;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::core::io
