#include "network/resolver.h"

#include "network/socket_runtime.h"

#include <chrono>
#include <cstdio>
#include <cwchar>
#include <limits>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace arp::network {
namespace {

std::atomic_uint64_t activeLookups{0}, nativeEvents{0}, nativeResults{0}, cancellations{0};

ResolverResult failure(ResolverStatus status, const char* diagnostic)
{
    ResolverResult result;
    result.status = status;
    result.diagnostic = diagnostic;
    return result;
}

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

// These two Winsock entry points are absent from some MinGW headers. Bind
// their documented signatures dynamically, together with lookup/free, so
// supported() reflects actual availability rather than a Windows name check.
class ResolverApi final {
public:
    using Cancel = int(WSAAPI*)(HANDLE*);
    using Result = int(WSAAPI*)(OVERLAPPED*);

    ResolverApi()
    {
        const auto module = GetModuleHandleW(L"ws2_32.dll");
        if (!module)
            return;
        lookup = reinterpret_cast<decltype(lookup)>(GetProcAddress(module, "GetAddrInfoExW"));
        cancel = reinterpret_cast<Cancel>(GetProcAddress(module, "GetAddrInfoExCancel"));
        result = reinterpret_cast<Result>(GetProcAddress(module, "GetAddrInfoExOverlappedResult"));
        release = reinterpret_cast<decltype(release)>(GetProcAddress(module, "FreeAddrInfoExW"));
    }

    bool valid() const noexcept { return lookup && cancel && result && release; }

    decltype(&GetAddrInfoExW) lookup = nullptr;
    Cancel cancel = nullptr;
    Result result = nullptr;
    decltype(&FreeAddrInfoExW) release = nullptr;
};

class Lookup final {
public:
    explicit Lookup(const ResolverApi& api) : api_(api)
    {
        ++activeLookups;
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (operation.hEvent)
            ++nativeEvents;
    }

    ~Lookup()
    {
        if (pending)
            cancelAndJoin();
        trackResults();
        if (addresses) {
            api_.release(addresses);
            --nativeResults;
        }
        if (operation.hEvent && CloseHandle(operation.hEvent))
            --nativeEvents;
        --activeLookups;
    }

    void cancelAndJoin() noexcept
    {
        ++cancellations;
        api_.cancel(&queryHandle);
        // Native completion is the ownership boundary. Cancellation alone
        // does not permit freeing OVERLAPPED/result storage still in use.
        if (WaitForSingleObject(operation.hEvent, INFINITE) != WAIT_OBJECT_0)
            std::terminate(); // Native producer quiescence cannot be fabricated.
        pending = false;
    }

    void trackResults() noexcept
    {
        if (addresses && !counted_) {
            ++nativeResults;
            counted_ = true;
        }
    }

    OVERLAPPED operation{};
    HANDLE queryHandle = nullptr;
    PADDRINFOEXW addresses = nullptr;
    bool pending = false;

private:
    const ResolverApi& api_;
    bool counted_ = false;
};

#endif

} // namespace

bool Resolver::supported() noexcept
{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    return ResolverApi{}.valid();
#else
    return false;
#endif
}

ResolverTelemetry Resolver::telemetry() noexcept
{
    return {activeLookups.load(), nativeEvents.load(), nativeResults.load(), cancellations.load()};
}

ResolverResult Resolver::resolveOnWorker(const ResolverDesc& desc,
    StringView host,
    uint16 port,
    const core::runtime::CancellationToken& cancellation,
    const std::atomic_bool& closed)
{
    if (!desc.maxHostBytes || desc.maxHostBytes > INT32_MAX || !desc.maxAddresses || !desc.timeoutMs
        || desc.timeoutMs > INT32_MAX || !desc.cancellationPollMs
        || desc.cancellationPollMs > desc.timeoutMs || host.empty()
        || host.size() > desc.maxHostBytes || !port
        || desc.maxAddresses > SIZE_MAX / sizeof(endpoint))
        return failure(ResolverStatus::InvalidRequest, "NET-DNS-DESCRIPTOR-OR-NAME");
    for (const char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'
                || c == '-'))
            return failure(ResolverStatus::InvalidRequest, "NET-DNS-ASCII-NAME-REQUIRED");
    if (cancellation.cancelled() || closed.load())
        return failure(ResolverStatus::Cancelled, "NET-DNS-CANCELLED");
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    SocketRuntime runtime;
    const ResolverApi api;
    if (!runtime.valid() || !api.valid())
        return failure(ResolverStatus::Unsupported, "NET-DNS-ASYNC-UNAVAILABLE");
    Lookup query(api);
    if (!query.operation.hEvent)
        return failure(ResolverStatus::BackendUnavailable, "NET-DNS-EVENT");
    core::pmr::Vector<wchar_t> name{&core::defaultCpuMemoryResource()};
    name.reserve(host.size() + 1);
    for (const auto c : host)
        name.push_back(static_cast<unsigned char>(c));
    name.push_back(0);
    wchar_t service[std::numeric_limits<uint16>::digits10 + 2]{};
    std::swprintf(
        service, sizeof(service) / sizeof(service[0]), L"%u", static_cast<unsigned>(port));
    ADDRINFOEXW hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(desc.timeoutMs);
    auto code = api.lookup(name.data(),
        service,
        NS_DNS,
        nullptr,
        &hints,
        &query.addresses,
        nullptr,
        &query.operation,
        nullptr,
        &query.queryHandle);
    query.pending = code == WSA_IO_PENDING;
    while (query.pending) {
        if (cancellation.cancelled() || closed.load()) {
            query.cancelAndJoin();
            return failure(ResolverStatus::Cancelled, "NET-DNS-CANCELLED");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            query.cancelAndJoin();
            return failure(ResolverStatus::Timeout, "NET-DNS-DEADLINE");
        }
        const auto wait = WaitForSingleObject(query.operation.hEvent, desc.cancellationPollMs);
        if (wait == WAIT_OBJECT_0) {
            query.pending = false;
            code = api.result(&query.operation);
        } else if (wait != WAIT_TIMEOUT) {
            query.cancelAndJoin();
            return failure(ResolverStatus::BackendUnavailable, "NET-DNS-WAIT");
        }
    }
    query.trackResults();
    if (code != 0)
        return failure(code == WSAHOST_NOT_FOUND || code == WSANO_DATA
                ? ResolverStatus::NotFound
                : ResolverStatus::BackendUnavailable,
            "NET-DNS-LOOKUP-FAILED");
    if (cancellation.cancelled() || closed.load())
        return failure(ResolverStatus::Cancelled, "NET-DNS-CANCELLED");
    ResolverResult result;
    result.addresses.reserve(desc.maxAddresses);
    for (auto* entry = query.addresses; entry; entry = entry->ai_next) {
        if (result.addresses.size() == desc.maxAddresses)
            return failure(ResolverStatus::BudgetExceeded, "NET-DNS-ADDRESS-BUDGET");
        if (entry->ai_family != AF_INET || entry->ai_addrlen != sizeof(sockaddr_in)
            || !entry->ai_addr || entry->ai_socktype != SOCK_STREAM
            || entry->ai_protocol != IPPROTO_TCP)
            return failure(ResolverStatus::BackendUnavailable, "NET-DNS-RESULT-CONTRACT");
        result.addresses.emplace_back(entry->ai_addr);
        if (!result.addresses.back().is_valid() || result.addresses.back().port() != port)
            return failure(ResolverStatus::BackendUnavailable, "NET-DNS-RESULT-ENDPOINT");
    }
    if (result.addresses.empty())
        return failure(ResolverStatus::NotFound, "NET-DNS-NO-ADDRESS");
    return result;
#else
    return failure(ResolverStatus::Unsupported, "NET-DNS-ASYNC-UNAVAILABLE");
#endif
}

} // namespace arp::network
