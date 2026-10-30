#include "network/http/client.h"

namespace arp::network::http {

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

bool needsPortInHostHeader(const Uri& uri) noexcept
{
    if (uri.has_explicit_port()) {
        return true;
    }
    if ((uri.scheme() == "http" || uri.scheme() == "ws") && uri.port() != 80) {
        return true;
    }
    if ((uri.scheme() == "https" || uri.scheme() == "wss") && uri.port() != 443) {
        return true;
    }
    return false;
}

bool hostNeedsBrackets(StringView host) noexcept
{
    return host.find(':') != StringView::npos;
}

void appendView(core::pmr::String& out, StringView value)
{
    out.append(value.data(), value.size());
}

} // namespace

bool ClientPrepareResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

bool SecureClientPrepareResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none && tls_diagnostic == tls::TlsDiagnosticCode::none;
}

bool ClientStatus::success() const noexcept
{
    return !transport_error && diagnostic == DiagnosticCode::none
        && tls_diagnostic == tls::TlsDiagnosticCode::none;
}

ClientRequest::ClientRequest() : host_header_(&core::defaultCpuMemoryResource())
{}

ClientRequest::ClientRequest(Method method, StringView uri) : ClientRequest()
{
    message_.set_method(method);
    diagnostic_ = set_uri(uri);
}

ClientRequest ClientRequest::get(StringView uri)
{
    return ClientRequest(Method::Get, uri);
}

ClientRequest ClientRequest::post(StringView uri)
{
    return ClientRequest(Method::Post, uri);
}

bool ClientRequest::success() const noexcept
{
    return diagnostic_ == DiagnosticCode::none;
}

DiagnosticCode ClientRequest::diagnostic() const noexcept
{
    return diagnostic_;
}

DiagnosticCode ClientRequest::set_uri(StringView uri)
{
    has_connection_endpoint_ = false;
    const auto parsed = Uri::parse(uri);
    if (!parsed.success()) {
        diagnostic_ = parsed.diagnostic;
        return diagnostic_;
    }
    if (!parsed.uri.is_absolute()) {
        diagnostic_ = DiagnosticCode::malformed_uri;
        return diagnostic_;
    }
    uri_ = parsed.uri;
    diagnostic_ = rebuild_from_uri();
    return diagnostic_;
}

DiagnosticCode ClientRequest::set_connection_endpoint(const endpoint& remote) noexcept
{
    if (!success() || !remote.is_valid() || remote.is_ipv6() || remote.port() != uri_.port()) {
        has_connection_endpoint_ = false;
        diagnostic_ = DiagnosticCode::malformed_uri;
        return diagnostic_;
    }
    connection_endpoint_ = remote;
    has_connection_endpoint_ = true;
    return DiagnosticCode::none;
}

const endpoint* ClientRequest::connection_endpoint() const noexcept
{
    return has_connection_endpoint_ ? &connection_endpoint_ : nullptr;
}

void ClientRequest::set_method(Method method) noexcept
{
    message_.set_method(method);
}

DiagnosticCode ClientRequest::add_header(StringView name, StringView value)
{
    const auto diagnostic = message_.add_header(name, value);
    if (diagnostic != DiagnosticCode::none) {
        diagnostic_ = diagnostic;
    }
    return diagnostic;
}

DiagnosticCode ClientRequest::set_content_type(ContentType type)
{
    return add_header("Content-Type", contentTypeText(type));
}

void ClientRequest::set_body_text(StringView text)
{
    message_.set_body_text(text);
}

tls::TlsConfig& ClientRequest::tls_config() noexcept
{
    return tls_config_;
}

const tls::TlsConfig& ClientRequest::tls_config() const noexcept
{
    return tls_config_;
}

const Uri& ClientRequest::uri() const noexcept
{
    return uri_;
}

const Request& ClientRequest::message() const noexcept
{
    return message_;
}

Request& ClientRequest::message() noexcept
{
    return message_;
}

StringView ClientRequest::host_header() const noexcept
{
    return StringView(host_header_.data(), host_header_.size());
}

DiagnosticCode ClientRequest::rebuild_from_uri()
{
    message_.set_target(uri_.request_target());

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
    return DiagnosticCode::none;
}

Client::Client(io_context* context) noexcept : context_(context)
{}

io_context* Client::context() const noexcept
{
    return context_;
}

ClientPrepareResult Client::prepareTransport(const Uri& uri) noexcept
{
    ClientPrepareResult result;
    if (!uri.is_absolute()) {
        result.diagnostic = DiagnosticCode::malformed_uri;
        return result;
    }
    if (uri.scheme() == "http") {
        result.transport = ClientTransportKind::http_cleartext;
        return result;
    }
    if (uri.scheme() == "ws") {
        result.transport = ClientTransportKind::websocket_cleartext;
        return result;
    }
    if (uri.scheme() == "https" || uri.scheme() == "wss") {
        result.transport = ClientTransportKind::tls_required;
        if (!tls::backendAvailable()) {
            result.diagnostic = DiagnosticCode::tls_unavailable;
        }
        return result;
    }
    result.diagnostic = DiagnosticCode::unsupported_scheme;
    return result;
}

SecureClientPrepareResult Client::prepareSecureTransport(const Uri& uri)
{
    SecureClientPrepareResult result;
    result.transport = ClientTransportKind::tls_required;
    result.tls_config.set_endpoint_role(tls::TlsEndpointRole::client);
    result.tls_config.set_certificate_verification(tls::CertificateVerification::system_trust);

    if (!uri.is_absolute()) {
        result.diagnostic = DiagnosticCode::malformed_uri;
        return result;
    }
    if (uri.scheme() != "https" && uri.scheme() != "wss") {
        result.diagnostic = DiagnosticCode::unsupported_scheme;
        return result;
    }

    result.tls_config.set_server_name(uri.host());
    const tls::TlsContext context(result.tls_config);
    if (!context.status().success()) {
        result.diagnostic = DiagnosticCode::tls_unavailable;
        result.tls_diagnostic = context.status().diagnostic;
        return result;
    }

    return result;
}

SecureClientPrepareResult Client::propagateTlsFailure(const tls::TlsStatus& status) noexcept
{
    SecureClientPrepareResult result;
    if (!status.success()) {
        result.diagnostic = DiagnosticCode::tls_unavailable;
        result.tls_diagnostic = status.diagnostic;
    }
    return result;
}

} // namespace arp::network::http
