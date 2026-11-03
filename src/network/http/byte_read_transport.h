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
    QueueFull,
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

    // V1 remains an anonymous GET. V2 is an explicit, origin-bound JSON POST;
    // reading response bytes does not imply that the request is side-effect free.
    enum class Operation { AnonymousGet, BearerJsonPost };
    std::uint32_t contractVersion = 1;
    Operation operation = Operation::AnonymousGet;
    core::pmr::String credentialOrigin{&core::defaultCpuMemoryResource()};
    core::pmr::String bearerToken{&core::defaultCpuMemoryResource()};
    core::pmr::String requestBody{&core::defaultCpuMemoryResource()};
    std::uint64_t maxRequestBytes = 0;
    // Required for v2: the caller's application protocol must tolerate an
    // identical wire replay. Fetch does not expose control over UA retries.
    bool replayProtected = false;
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

// Response-byte HTTP/1 transport. V1 is anonymous GET; V2 explicitly opts into
// an origin-bound, replay-protected Bearer JSON POST. The engine never retries;
// a browser network stack can replay the identical request. No redirects.
// Native IO reuses Client/Session/parser/socket/reactor on a worker; Fetch is
// the browser adapter. No OS HTTP client is substituted.
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
