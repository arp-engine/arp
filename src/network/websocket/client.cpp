#include "network/websocket/client.h"

namespace arp::network::websocket {

namespace {

void appendUnsigned(core::pmr::String& out, std::size_t value)
{
    char digits[32]{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value > 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
}

void appendView(core::pmr::String& out, StringView value)
{
    out.append(value.data(), value.size());
}

bool hostNeedsBrackets(StringView host) noexcept
{
    return host.find(':') != StringView::npos;
}

bool needsPortInHostHeader(const http::Uri& uri) noexcept
{
    if (uri.has_explicit_port()) {
        return true;
    }
    if (uri.scheme() == "ws" && uri.port() != 80) {
        return true;
    }
    if (uri.scheme() == "wss" && uri.port() != 443) {
        return true;
    }
    return false;
}

} // namespace

bool ClientStatus::success() const noexcept
{
    return !transport_error && diagnostic == DiagnosticCode::none
        && http_diagnostic == http::DiagnosticCode::none
        && tls_diagnostic == tls::TlsDiagnosticCode::none;
}

ClientRequest::ClientRequest()
    : host_header_(&core::defaultCpuMemoryResource()), resource_(&core::defaultCpuMemoryResource()),
      key_("dGhlIHNhbXBsZSBub25jZQ==", &core::defaultCpuMemoryResource())
{}

ClientRequest ClientRequest::connect(StringView uri)
{
    ClientRequest request;
    request.set_uri(uri);
    return request;
}

bool ClientRequest::success() const noexcept
{
    return diagnostic_ == http::DiagnosticCode::none;
}

http::DiagnosticCode ClientRequest::diagnostic() const noexcept
{
    return diagnostic_;
}

http::DiagnosticCode ClientRequest::set_uri(StringView uri)
{
    const auto parsed = http::Uri::parse(uri);
    if (!parsed.success()) {
        diagnostic_ = parsed.diagnostic;
        return diagnostic_;
    }
    if (!parsed.uri.is_absolute()) {
        diagnostic_ = http::DiagnosticCode::malformed_uri;
        return diagnostic_;
    }
    if (parsed.uri.scheme() != "ws" && parsed.uri.scheme() != "wss") {
        diagnostic_ = http::DiagnosticCode::unsupported_scheme;
        return diagnostic_;
    }
    uri_ = parsed.uri;
    diagnostic_ = rebuild_from_uri();
    return diagnostic_;
}

tls::TlsConfig& ClientRequest::tls_config() noexcept
{
    return tls_config_;
}

const tls::TlsConfig& ClientRequest::tls_config() const noexcept
{
    return tls_config_;
}

const http::Uri& ClientRequest::uri() const noexcept
{
    return uri_;
}

StringView ClientRequest::host_header() const noexcept
{
    return StringView(host_header_.data(), host_header_.size());
}

StringView ClientRequest::resource() const noexcept
{
    return StringView(resource_.data(), resource_.size());
}

StringView ClientRequest::key() const noexcept
{
    return StringView(key_.data(), key_.size());
}

http::DiagnosticCode ClientRequest::rebuild_from_uri()
{
    resource_.clear();
    appendView(resource_, uri_.path().empty() ? StringView("/") : uri_.path());
    if (!uri_.query().empty()) {
        resource_.push_back('?');
        appendView(resource_, uri_.query());
    }

    host_header_.clear();
    if (hostNeedsBrackets(uri_.host())) {
        host_header_.push_back('[');
        appendView(host_header_, uri_.host());
        host_header_.push_back(']');
    } else {
        appendView(host_header_, uri_.host());
    }
    if (needsPortInHostHeader(uri_)) {
        host_header_.push_back(':');
        appendUnsigned(host_header_, uri_.port());
    }

    tls_config_.set_endpoint_role(tls::TlsEndpointRole::client);
    tls_config_.set_server_name(uri_.host());
    return http::DiagnosticCode::none;
}

Client::Client(io_context* context)
    : context_(context), socket_(context),
      stream_(core::makeUnique<Stream>(StreamRole::client, stream_config_))
{}

Client::Client(io_context* context, const ClientRequest& request) : Client(context, request, {})
{}

Client::Client(io_context* context, const ClientRequest& request, StreamConfig config)
    : context_(context), request_(request), has_request_(true), stream_config_(config),
      socket_(context), stream_(core::makeUnique<Stream>(StreamRole::client, stream_config_))
{}

io_context* Client::context() const noexcept
{
    return context_;
}

const ClientRequest& Client::request() const noexcept
{
    return request_;
}

void Client::set_request(const ClientRequest& request)
{
    request_ = request;
    has_request_ = true;
}

StreamState Client::state() const noexcept
{
    return stream_ ? stream_->state() : StreamState::initial;
}

void Client::reset_transport()
{
    error_code ignored;
    socket_.close(ignored);
    active_transport_ = ActiveTransport::none;
    tls_transport_.reset();
    tls_stream_.reset();
    tls_context_.reset();
    stream_ = core::makeUnique<Stream>(StreamRole::client, stream_config_);
}

ClientStatus Client::open_socket()
{
    error_code ec;
    if (socket_.open(ec) != 0) {
        return fromTransportError(ec);
    }
    if (socket_.bind(ec, endpoint::anyaddr<ipv4>(0)) != 0) {
        const error_code bindEc = ec;
        error_code ignored;
        socket_.close(ignored);
        return fromTransportError(bindEc);
    }
    return {};
}

ClientStatus Client::fromStreamStatus(const StreamStatus& status) noexcept
{
    ClientStatus result;
    result.transport_error = status.transport_error;
    result.diagnostic = status.diagnostic;
    result.http_diagnostic = status.http_diagnostic;
    result.bytes = status.bytes;
    return result;
}

ClientStatus Client::fromStreamAndTlsStatus(
    const StreamStatus& streamStatus, const tls::TlsStatus& tlsStatus) noexcept
{
    if (!tlsStatus.success()) {
        auto status = fromTlsStatus(tlsStatus);
        status.transport_error = streamStatus.transport_error;
        status.diagnostic = streamStatus.diagnostic;
        status.bytes = streamStatus.bytes;
        return status;
    }
    return fromStreamStatus(streamStatus);
}

ClientStatus Client::fromHttpDiagnostic(http::DiagnosticCode diagnostic) noexcept
{
    ClientStatus status;
    status.http_diagnostic = diagnostic;
    return status;
}

ClientStatus Client::fromTransportError(const error_code& ec) noexcept
{
    ClientStatus status;
    status.transport_error = ec;
    return status;
}

ClientStatus Client::fromTlsStatus(const tls::TlsStatus& tlsStatus) noexcept
{
    ClientStatus status;
    if (!tlsStatus.success()) {
        status.http_diagnostic = http::DiagnosticCode::tls_unavailable;
        status.tls_diagnostic = tlsStatus.diagnostic;
    }
    return status;
}

ClientStatus Client::fromSecurePrepare(const SecureStreamPrepareResult& prepare) noexcept
{
    ClientStatus status;
    status.diagnostic = prepare.diagnostic;
    status.http_diagnostic = prepare.http_diagnostic;
    status.tls_diagnostic = prepare.tls_diagnostic;
    return status;
}

} // namespace arp::network::websocket
