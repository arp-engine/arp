#pragma once

#include "core/runtime/completion_queue.h"
#include "core/runtime/work_service.h"
#include "network/http/byte_read_transport.h"

namespace arp::network::http {

struct ByteTransferExecutorDesc {
    ByteReadDesc transport;
    std::uint32_t maxInFlight = 0;
};

struct ByteTransferExecutorTelemetry {
    std::uint32_t inFlight = 0;
    std::uint64_t activeNativeTransfers = 0;
};

// Owner-thread bounded transport scheduler. The request and observer remain
// alive until complete; the queue outlives shutdown and all accepted callbacks.
// A completed callback means the producer is quiescent, not that HTTP succeeded.
class ARP_API ByteTransferExecutor final {
public:
    using Complete = void (*)(void*);
    explicit ByteTransferExecutor(ByteTransferExecutorDesc);
    ~ByteTransferExecutor();
    ByteTransferExecutor(const ByteTransferExecutor&) = delete;
    ByteTransferExecutor& operator=(const ByteTransferExecutor&) = delete;
    static bool supported() noexcept;
    static bool browserSupported() noexcept;
    ByteReadStatus submit(const ByteReadRequest&,
        ByteReadObserver&,
        Complete,
        void*,
        core::runtime::CompletionQueue&,
        core::SharedPtr<core::CpuMemoryQuota> = {}) noexcept;
    void shutdown() noexcept;
    ByteTransferExecutorTelemetry telemetry() const noexcept;

private:
    struct State;
    struct Cookie;
    static core::runtime::WorkStatus execute(void*);
    static void workerComplete(void*, core::runtime::WorkResult);
    static void browserComplete(void*);
    static void deliver(void*);
    core::SharedPtr<State> state_;
};
} // namespace arp::network::http
