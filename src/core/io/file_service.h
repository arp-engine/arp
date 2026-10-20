#pragma once

#include "core/api.h"
#include "core/io/file_backend.h"
#include "core/runtime/completion_queue.h"

#include <cstdint>

namespace arp::core::io {

struct FileServiceTelemetry {
    std::uint64_t submitted = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t rejected = 0;
    std::uint64_t maxCompletionQueueDepth = 0;
    std::uint64_t totalSubmitLatencyNs = 0;
    std::uint64_t maxSubmitLatencyNs = 0;
};

class ARP_API FileService final {
public:
    FileService(runtime::CompletionQueue& completions, FileBackend& backend) noexcept;
    FileService(const FileService&) = delete;
    FileService& operator=(const FileService&) = delete;
    FileService(FileService&&) = delete;
    FileService& operator=(FileService&&) = delete;
    ~FileService() = default;

    FileBackendCapability capability() const noexcept;
    FileIoStatus read(const FileReadRequest& request, FileReadCompletion completion) noexcept;
    FileServiceTelemetry telemetry() const noexcept;
    void shutdown() noexcept;

private:
    runtime::CompletionQueue& completions_;
    FileBackend& backend_;
    FileServiceTelemetry telemetry_{};
    bool closed_ = false;
};

} // namespace arp::core::io
