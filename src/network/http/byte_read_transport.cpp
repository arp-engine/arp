#include "network/http/byte_read_transport.h"

#include "network/http/uri.h"
#if !defined(__EMSCRIPTEN__)
#include "network/http/client.h"
#include "network/io_service.h"
#include "network/resolver.h"
#include "network/socket_runtime.h"
#endif

#include <algorithm>
#include <chrono>
#include <thread>

namespace arp::network::http {

std::uint64_t ByteReadTransport::resolverBudget(
    const ByteReadDesc& desc, std::size_t maxUrlBytes) noexcept
{
#if !defined(__EMSCRIPTEN__)
    if (!desc.maxDnsAddresses)
        return 0;
    const auto addresses = static_cast<std::uint64_t>(desc.maxDnsAddresses) * sizeof(endpoint);
    if (maxUrlBytes > (UINT64_MAX - addresses) / 2 - 1)
        return UINT64_MAX;
    return addresses + 2 * (static_cast<std::uint64_t>(maxUrlBytes) + 1);
#else
    (void)desc;
    (void)maxUrlBytes;
    return 0;
#endif
}

ByteReadStatus ByteReadTransport::validate(
    const ByteReadDesc& desc, const ByteReadRequest& request) noexcept
{
    if (!desc.maxHeaderBytes || desc.maxHeaderBytes > INT32_MAX || !desc.ioChunkBytes
        || desc.ioChunkBytes > INT32_MAX || !desc.timeoutMs || desc.timeoutMs > INT32_MAX
        || !desc.cancellationPollMs || desc.cancellationPollMs > desc.timeoutMs
        || request.url.empty() || request.url.size() > INT32_MAX || request.maxBodyBytes > SIZE_MAX
        || request.maxBodyBytes == UINT64_MAX)
        return ByteReadStatus::InvalidRequest;
    for (const unsigned char c : request.url)
        if (c < 33 || c > 126)
            return ByteReadStatus::InvalidRequest;
    if (!request.range.empty()) {
        constexpr std::string_view prefix = "bytes=";
        const std::string_view range = request.range;
        if (range.substr(0, prefix.size()) != prefix || range.size() > sizeof("bytes=-") + 40)
            return ByteReadStatus::InvalidRequest;
        unsigned dashes = 0;
        for (const char c : range.substr(prefix.size())) {
            if (c == '-')
                ++dashes;
            else if (c < '0' || c > '9')
                return ByteReadStatus::InvalidRequest;
        }
        if (dashes != 1 || range.back() == '-' || range[prefix.size()] == '-')
            return ByteReadStatus::InvalidRequest;
    }
    try {
        const auto parsed = Uri::parse(request.url);
        if (!parsed.success() || !parsed.uri.is_absolute()
            || (parsed.uri.scheme() != "http" && parsed.uri.scheme() != "https")
            || !parsed.uri.fragment().empty())
            return ByteReadStatus::InvalidRequest;
    } catch (...) {
        return ByteReadStatus::BudgetExceeded;
    }
    return ByteReadStatus::Success;
}

#if !defined(__EMSCRIPTEN__)
namespace {

// The engine reactor owns socket cancellation. Stop and join producers, then
// drain its completion queue before either observer or context leaves scope.
class ReactorLifetime final {
public:
    ReactorLifetime(io_context& context, std::atomic_bool& crashed)
        : context_(context), thread_([&context, &crashed] {
              try {
                  context.service()->run();
              } catch (...) {
                  crashed = true;
                  context.shutdown();
              }
          })
    {}

    ~ReactorLifetime() { stop(); }

    void stop() noexcept
    {
        context_.shutdown();
        if (thread_.joinable())
            thread_.join();
        while (context_.pending_completions()) {
            try {
                context_.dispatch();
            } catch (...) {
                // The public observer is noexcept; release remaining reactor
                // records even if an internal callback allocation failed.
            }
        }
    }

private:
    io_context& context_;
    std::thread thread_;
};

ByteReadStatus fromClient(const ClientStatus& status) noexcept
{
    if (status.success())
        return ByteReadStatus::Success;
    if (status.diagnostic == DiagnosticCode::tls_unavailable
        || status.diagnostic == DiagnosticCode::unsupported_scheme)
        return ByteReadStatus::Unsupported;
    if (status.diagnostic == DiagnosticCode::body_too_large
        || status.diagnostic == DiagnosticCode::header_too_large
        || status.diagnostic == DiagnosticCode::too_many_headers
        || status.diagnostic == DiagnosticCode::buffer_too_large)
        return ByteReadStatus::BudgetExceeded;
    return status.transport_error ? ByteReadStatus::TransportError : ByteReadStatus::ProtocolError;
}

} // namespace
#endif

bool ByteReadTransport::supported() noexcept
{
#if defined(__EMSCRIPTEN__)
    return false;
#else
    return true;
#endif
}

void ByteReadTransport::read(const ByteReadDesc& desc,
    const ByteReadRequest& request,
    ByteReadObserver& observer,
    const std::atomic_bool& closed,
    std::atomic_uint64_t& activeTransfers)
{
    if (const auto status = validate(desc, request); status != ByteReadStatus::Success) {
        observer.onFailure(status, "NET-HTTP-BYTE-READ-REQUEST");
        return;
    }
#if !defined(__EMSCRIPTEN__)
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(desc.timeoutMs);
    SocketRuntime socketRuntime;
    if (!socketRuntime.valid()) {
        observer.onFailure(ByteReadStatus::TransportError, "NET-HTTP-SOCKET-RUNTIME");
        return;
    }
    auto wire = ClientRequest::get(request.url);
    const auto prepared = Client::prepareTransport(wire.uri());
    if (!wire.success() || !prepared.success()) {
        observer.onFailure(ByteReadStatus::Unsupported, "NET-HTTP-ENGINE-TRANSPORT-UNAVAILABLE");
        return;
    }
    if (!endpoint(wire.uri().host(), wire.uri().port()).is_valid()) {
        if (!desc.maxDnsAddresses) {
            observer.onFailure(ByteReadStatus::Unsupported, "NET-HTTP-DNS-NOT-ENABLED");
            return;
        }
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0) {
            observer.onFailure(ByteReadStatus::Timeout, "NET-HTTP-ENGINE-DEADLINE");
            return;
        }
        auto resolved = Resolver::resolveOnWorker(
            {request.url.size(),
                desc.maxDnsAddresses,
                static_cast<std::uint32_t>(remaining),
                std::min(desc.cancellationPollMs, static_cast<std::uint32_t>(remaining))},
            wire.uri().host(),
            wire.uri().port(),
            request.cancellation,
            closed);
        if (resolved.status != ResolverStatus::Success) {
            const auto status = resolved.status == ResolverStatus::Cancelled
                ? ByteReadStatus::Cancelled
                : resolved.status == ResolverStatus::Timeout        ? ByteReadStatus::Timeout
                : resolved.status == ResolverStatus::BudgetExceeded ? ByteReadStatus::BudgetExceeded
                : resolved.status == ResolverStatus::Unsupported    ? ByteReadStatus::Unsupported
                                                                 : ByteReadStatus::TransportError;
            observer.onFailure(status, resolved.diagnostic);
            return;
        }
        // Resolver/1 explicitly selects the first OS-ordered IPv4 address.
        // Host, URI and TLS identity retain the original DNS name. No retry.
        if (wire.set_connection_endpoint(resolved.addresses.front()) != DiagnosticCode::none) {
            observer.onFailure(ByteReadStatus::InvalidRequest, "NET-HTTP-RESOLVED-ENDPOINT");
            return;
        }
    }
    if (closed.load() || request.cancellation.cancelled()) {
        observer.onFailure(ByteReadStatus::Cancelled, "NET-HTTP-ENGINE-CANCELLED");
        return;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
        observer.onFailure(ByteReadStatus::Timeout, "NET-HTTP-ENGINE-DEADLINE");
        return;
    }
    if (wire.add_header("Accept-Encoding", "identity") != DiagnosticCode::none
        || wire.add_header("Connection", "close") != DiagnosticCode::none
        || (!request.range.empty()
            && wire.add_header("Range", request.range) != DiagnosticCode::none)) {
        observer.onFailure(ByteReadStatus::InvalidRequest, "NET-HTTP-REQUEST-HEADERS");
        return;
    }
    SessionConfig config;
    config.read_buffer_size = desc.ioChunkBytes;
    config.parser_limits.max_header_bytes = desc.maxHeaderBytes;
    // Minimum wire field is "a:\r\n"; byte budget also bounds field count.
    config.parser_limits.max_header_count = desc.maxHeaderBytes / (sizeof("a:\r\n") - 1);
    config.parser_limits.max_body_bytes = static_cast<std::size_t>(request.maxBodyBytes);
    config.parser_limits.max_buffer_bytes = desc.maxHeaderBytes + desc.ioChunkBytes;
    config.parser_limits.allow_close_delimited_body = true;
    config.parser_limits.prioritize_header_observer = true;
    bool done = false;
    ClientStatus outcome;
    std::atomic_bool crashed{false};
    io_context context;
    const auto headers = [&](const Response& response) {
        core::pmr::String block{&core::defaultCpuMemoryResource()};
        block.reserve(desc.maxHeaderBytes);
        for (std::size_t i = 0; i < response.headers().size(); ++i) {
            const auto& header = response.headers().at(i);
            if (header.name().size() + header.value().size() + 2
                > desc.maxHeaderBytes - block.size()) {
                observer.onFailure(ByteReadStatus::BudgetExceeded, "NET-HTTP-HEADER-BUDGET");
                return false;
            }
            block.append(header.name().data(), header.name().size());
            block += ':';
            block.append(header.value().data(), header.value().size());
            block += '\n';
        }
        return observer.onHeaders(response.status().value(), block);
    };
    const auto body = [&](StringView chunk) {
        return observer.onBody(reinterpret_cast<const std::uint8_t*>(chunk.data()), chunk.size());
    };
    const auto completed = [&](const ClientStatus& status) {
        outcome = status;
        done = true;
    };
    ReactorLifetime reactor(context, crashed);
    ++activeTransfers;

    struct ActiveScope {
        std::atomic_uint64_t& value;

        ~ActiveScope() { --value; }
    } active{activeTransfers};

    Client client(&context);
    client.async_stream(wire, config, headers, body, completed);
    while (!done && !crashed.load()) {
        context.dispatch();
        if (done)
            break;
        if (closed.load() || request.cancellation.cancelled()) {
            observer.onFailure(ByteReadStatus::Cancelled, "NET-HTTP-ENGINE-CANCELLED");
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            observer.onFailure(ByteReadStatus::Timeout, "NET-HTTP-ENGINE-DEADLINE");
            break;
        }
        context.wait_for_completion(std::chrono::milliseconds(desc.cancellationPollMs));
    }
    reactor.stop();
    if (crashed.load())
        observer.onFailure(ByteReadStatus::TransportError, "NET-HTTP-REACTOR-FAILED");
    else if (!done)
        observer.onFailure(ByteReadStatus::TransportError, "NET-HTTP-NO-TERMINAL");
    else if (outcome.success())
        observer.onEnd();
    else
        observer.onFailure(fromClient(outcome), "NET-HTTP-ENGINE-READ-FAILED");
#else
    (void)closed;
    (void)activeTransfers;
    observer.onFailure(ByteReadStatus::Unsupported, "NET-HTTP-NATIVE-PLATFORM");
#endif
}

} // namespace arp::network::http
