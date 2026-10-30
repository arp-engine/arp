#pragma once

#include "core/runtime/cancellation_token.h"
#include "network/endpoint.h"

#include <atomic>

namespace arp::network {

enum class ResolverStatus {
    Success,
    Unsupported,
    InvalidRequest,
    Cancelled,
    Timeout,
    NotFound,
    BudgetExceeded,
    BackendUnavailable,
};

struct ResolverDesc {
    std::size_t maxHostBytes = 0;
    std::uint32_t maxAddresses = 0;
    std::uint32_t timeoutMs = 0;
    std::uint32_t cancellationPollMs = 0;
};

struct ResolverResult {
    ResolverStatus status = ResolverStatus::Success;
    const char* diagnostic = nullptr;
    core::pmr::Vector<endpoint> addresses{&core::defaultCpuMemoryResource()};
};

struct ResolverTelemetry {
    std::uint64_t activeLookups = 0;
    std::uint64_t openNativeEvents = 0;
    std::uint64_t ownedNativeResults = 0;
    std::uint64_t cancellations = 0;
};

// Resolver/1 is an explicit worker-only operation. Windows uses cancellable
// GetAddrInfoExW; other hosts fail Unsupported rather than blocking getaddrinfo.
// IPv4 matches the existing Client socket family. OS order is preserved and
// the caller explicitly chooses one address; there is no retry/fallback loop.
class ARP_API Resolver final {
public:
    static bool supported() noexcept;
    static ResolverResult resolveOnWorker(const ResolverDesc& desc,
        StringView host,
        uint16 port,
        const core::runtime::CancellationToken& cancellation,
        const std::atomic_bool& closed);
    static ResolverTelemetry telemetry() noexcept;
};

} // namespace arp::network
