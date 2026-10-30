#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "core/runtime/cancellation_token.h"

#include <atomic>
#include <string_view>

namespace arp::network::http {

enum class ByteReadStatus {
    Accepted,
    Success,
    Unsupported,
    Cancelled,
    Timeout,
    TransportError,
    BudgetExceeded,
    InvalidRequest,
    ProtocolError,
};

struct ByteReadDesc {
    std::size_t maxHeaderBytes = 0;
    std::size_t ioChunkBytes = 0;
    std::uint32_t timeoutMs = 0;
    std::uint32_t cancellationPollMs = 0;
    // Zero explicitly disables engine-side DNS. Fetch owns browser resolution.
    std::uint32_t maxDnsAddresses = 0;
};

struct ByteReadRequest {
    core::pmr::String url{&core::defaultCpuMemoryResource()};
    core::pmr::String range{&core::defaultCpuMemoryResource()};
    core::runtime::CancellationToken cancellation;
    std::uint64_t maxBodyBytes = 0;
};

// Transport has no file location, mount, version or resource semantics.
// The observer consumes views synchronously; false aborts the transfer. It
// owns the application body budget. Transport storage is bounded independently.
class ByteReadObserver {
public:
    virtual ~ByteReadObserver() = default;
    virtual bool onHeaders(unsigned status, std::string_view block) noexcept = 0;
    virtual bool onBody(const std::uint8_t* bytes, std::size_t count) noexcept = 0;
    virtual void onEnd() noexcept = 0;
    virtual void onFailure(ByteReadStatus status, const char* diagnostic) noexcept = 0;
};

// Read-only HTTP/1 transport with explicit no-redirect/no-credentials/identity
// encoding policy. Native read reuses Client/Session/parser/socket/reactor on
// an IO worker; Fetch is the browser adapter. No OS HTTP client is substituted.
class ARP_API ByteReadTransport final {
public:
    static bool supported() noexcept;
    static ByteReadStatus validate(
        const ByteReadDesc& desc, const ByteReadRequest& request) noexcept;
    static std::uint64_t resolverBudget(const ByteReadDesc& desc, std::size_t maxUrlBytes) noexcept;
    static void read(const ByteReadDesc& desc,
        const ByteReadRequest& request,
        ByteReadObserver& observer,
        const std::atomic_bool& closed,
        std::atomic_uint64_t& openNativeObjects);
};

} // namespace arp::network::http
