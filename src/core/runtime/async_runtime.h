#pragma once

#include "core/api.h"
#include "core/io/file_service.h"
#include "core/io/thread_pool_file_backend.h"
#include "core/runtime/completion_queue.h"
#include "core/runtime/work_service.h"

#include <cstddef>
#include <cstdint>

namespace arp::core::runtime {

enum class AsyncRuntimeStatus : std::uint8_t {
    Running,
    InvalidConfiguration,
    Shutdown,
};

enum class AsyncRuntimeDiagnostic : std::uint8_t {
    None,
    InvalidCompletionQueueCapacity,
    InvalidCompletionBudget,
    MissingWorkBackend,
    MissingFileBackend,
    MissingNetworkValidationSurface,
};

enum class AsyncRuntimeSubmitStatus : std::uint8_t {
    Accepted,
    QueueFull,
    ServiceClosed,
    InvalidCallback,
};

enum class AsyncRuntimeShutdownPolicy : std::uint8_t {
    Drain,
    CancelPending,
};

struct AsyncRuntimeDesc {
    std::size_t completionQueueCapacity = 0;
    std::size_t workerCount = 0;
    std::size_t workQueueCapacity = 0;
    std::size_t completionBudget = 0;
    bool enableWork = true;
    bool enableThreadPoolFileBackend = true;
    bool enableExternalNetworkValidation = true;
};

struct AsyncRuntimeTelemetry {
    CompletionQueueTelemetry completion;
    WorkServiceTelemetry work;
    io::FileBackendCapability file;
    io::FileServiceTelemetry fileService;
    std::size_t completionBudget = 0;
    std::uint64_t dispatchCalls = 0;
    bool externalNetworkValidationEnabled = false;
};

class ARP_API AsyncRuntime final {
public:
    explicit AsyncRuntime(AsyncRuntimeDesc desc);
    AsyncRuntime(const AsyncRuntime&) = delete;
    AsyncRuntime& operator=(const AsyncRuntime&) = delete;
    AsyncRuntime(AsyncRuntime&&) = delete;
    AsyncRuntime& operator=(AsyncRuntime&&) = delete;
    ~AsyncRuntime();

    bool valid() const noexcept;
    AsyncRuntimeStatus status() const noexcept;
    AsyncRuntimeDiagnostic startupDiagnostics() const noexcept;

    CompletionQueue& completionQueue() noexcept;
    WorkService& workService() noexcept;
    io::FileService& fileService() noexcept;

    CompletionDispatchResult dispatch() noexcept;
    CompletionDispatchResult dispatch(std::size_t maxCallbacks) noexcept;
    void shutdown(
        AsyncRuntimeShutdownPolicy policy = AsyncRuntimeShutdownPolicy::CancelPending) noexcept;

    AsyncRuntimeTelemetry telemetry() const noexcept;

private:
    AsyncRuntimeDiagnostic validateDesc(const AsyncRuntimeDesc& desc) const noexcept;

    AsyncRuntimeDesc desc_;
    CompletionQueue completions_;
    WorkService work_;
    io::ThreadPoolFallbackFileBackend fileBackend_;
    io::FileService fileService_;
    std::uint64_t dispatchCalls_ = 0;
    AsyncRuntimeStatus status_ = AsyncRuntimeStatus::Running;
    AsyncRuntimeDiagnostic diagnostic_ = AsyncRuntimeDiagnostic::None;
};

} // namespace arp::core::runtime
