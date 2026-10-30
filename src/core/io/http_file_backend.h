#pragma once

#include "core/io/file_backend.h"
#include "core/io/http_file_contract.h"
#include "core/runtime/work_service.h"

namespace arp::core::io {

struct HttpFileBackendTelemetry {
    std::uint32_t inFlight = 0;
    std::uint64_t reservedBytes = 0;
    std::uint64_t activeNativeTransfers = 0;
    std::uint64_t receivedBytes = 0;
};

// Owner-thread submission/dispatch/shutdown, one bound CompletionQueue. Native
// IO runs on an owned worker; browser IO uses Fetch without blocking that thread.
// Public types contain no native handles, Fetch, URL parser or network module types.
class ARP_API HttpFileBackend final : public ManagedFileBackend {
public:
    explicit HttpFileBackend(HttpFileBackendDesc desc);
    ~HttpFileBackend() override;
    FileBackendCapability capability() const noexcept override;
    FileReadResult validateRead(const FileReadRequest& request) const override;
    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        runtime::CompletionQueue& completions) noexcept override;
    void shutdown() noexcept override;
    HttpFileBackendTelemetry telemetry() const noexcept;

    const char* diagnostic() const noexcept { return diagnostic_; }

private:
    struct State;
    struct Pending;
    static runtime::WorkStatus execute(void* context);
    static void complete(void* context, runtime::WorkResult result);
    static void deliver(void* context);
    static void browserComplete(void* context);
    FileIoStatus start(runtime::CompletionQueue& queue);
    core::SharedPtr<State> state_;
    core::UniquePtr<runtime::WorkService> work_;
    runtime::CompletionQueue* queue_ = nullptr;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::core::io
