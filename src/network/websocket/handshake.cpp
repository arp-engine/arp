#include "network/websocket/handshake.h"

#include "core/base64.h"
#include "core/sha1.h"
#include "core/string_utils.h"

namespace arp::network::websocket {

namespace {

constexpr StringView kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

HandshakeValidationResult fail(DiagnosticCode diagnostic) noexcept
{
    HandshakeValidationResult result;
    result.diagnostic = diagnostic;
    return result;
}

bool validClientKey(StringView key)
{
    byte_buffer decoded{&core::defaultCpuMemoryResource()};
    return core::Base64::decode(key, decoded) && decoded.size() == 16U;
}

} // namespace

bool HandshakeValidationResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

SubprotocolSelection::SubprotocolSelection() : value_(&core::defaultCpuMemoryResource())
{}

bool SubprotocolSelection::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

StringView SubprotocolSelection::value() const noexcept
{
    return StringView(value_.data(), value_.size());
}

void SubprotocolSelection::set_value(StringView value)
{
    value_.assign(value.data(), value.size());
}

HandshakeRequest::HandshakeRequest()
    : host_(&core::defaultCpuMemoryResource()), resource_(&core::defaultCpuMemoryResource()),
      key_(&core::defaultCpuMemoryResource()), subprotocols_(&core::defaultCpuMemoryResource())
{}

void HandshakeRequest::set_host(StringView host)
{
    host_.assign(host.data(), host.size());
}

void HandshakeRequest::set_resource(StringView resource)
{
    resource_.assign(resource.data(), resource.size());
}

void HandshakeRequest::set_key(StringView key)
{
    key_.assign(key.data(), key.size());
}

void HandshakeRequest::add_subprotocol(StringView subprotocol)
{
    core::pmr::String value{&core::defaultCpuMemoryResource()};
    value.assign(subprotocol.data(), subprotocol.size());
    subprotocols_.push_back(value);
}

http::Request HandshakeRequest::toHttpRequest() const
{
    http::Request request;
    request.set_method(http::Method::Get);
    request.set_target(
        resource_.empty() ? StringView("/") : StringView(resource_.data(), resource_.size()));
    request.headers().add("Host", StringView(host_.data(), host_.size()));
    request.headers().add("Upgrade", "websocket");
    request.headers().add("Connection", "Upgrade");
    request.headers().add("Sec-WebSocket-Version", "13");
    request.headers().add("Sec-WebSocket-Key", StringView(key_.data(), key_.size()));
    if (!subprotocols_.empty()) {
        core::pmr::String protocols{&core::defaultCpuMemoryResource()};
        for (std::size_t i = 0; i < subprotocols_.size(); ++i) {
            if (i > 0) {
                protocols += ", ";
            }
            protocols += subprotocols_[i];
        }
        request.headers().add(
            "Sec-WebSocket-Protocol", StringView(protocols.data(), protocols.size()));
    }
    return request;
}

core::pmr::String HandshakeResponse::acceptKey(StringView clientKey)
{
    core::pmr::String source{&core::defaultCpuMemoryResource()};
    source.append(clientKey.data(), clientKey.size());
    source.append(kGuid.data(), kGuid.size());
    return core::Base64::encode(core::Sha1::digest(StringView(source.data(), source.size())));
}

HandshakeValidationResult HandshakeResponse::validateClientRequest(const http::Request& request)
{
    if (request.method() != http::Method::Get) {
        return fail(DiagnosticCode::invalid_upgrade);
    }
    if (!core::asciiEqualsIgnoreCase(request.headers().firstValue("Upgrade"), "websocket")) {
        return fail(DiagnosticCode::invalid_upgrade);
    }
    if (!core::containsCommaSeparatedAsciiToken(
            request.headers().firstValue("Connection"), "Upgrade")) {
        return fail(DiagnosticCode::invalid_connection);
    }
    const auto version = request.headers().firstValue("Sec-WebSocket-Version");
    if (version.empty()) {
        return fail(DiagnosticCode::missing_header);
    }
    if (version != "13") {
        return fail(DiagnosticCode::unsupported_version);
    }
    const auto key = request.headers().firstValue("Sec-WebSocket-Key");
    if (key.empty()) {
        return fail(DiagnosticCode::missing_key);
    }
    if (!validClientKey(key)) {
        return fail(DiagnosticCode::invalid_key);
    }
    return {};
}

SubprotocolSelection HandshakeResponse::selectSubprotocol(
    const http::Request& request, StringView supported)
{
    SubprotocolSelection selection;
    if (supported.empty()) {
        return selection;
    }
    StringView requested = request.headers().firstValue("Sec-WebSocket-Protocol");
    while (!requested.empty()) {
        const auto comma = requested.find(',');
        const StringView part = core::trimAsciiOws(requested.substr(0, comma));
        if (part == supported) {
            selection.set_value(part);
            return selection;
        }
        if (comma == StringView::npos) {
            break;
        }
        requested.remove_prefix(comma + 1);
    }
    selection.diagnostic = DiagnosticCode::unsupported_subprotocol;
    return selection;
}

http::Response HandshakeResponse::buildServerResponse(
    const http::Request& request, StringView selectedSubprotocol)
{
    http::Response response;
    response.set_status(http::StatusCode(101));
    response.set_reason("Switching Protocols");
    response.headers().add("Upgrade", "websocket");
    response.headers().add("Connection", "Upgrade");
    const auto accept = acceptKey(request.headers().firstValue("Sec-WebSocket-Key"));
    response.headers().add("Sec-WebSocket-Accept", StringView(accept.data(), accept.size()));
    if (!selectedSubprotocol.empty()) {
        response.headers().add("Sec-WebSocket-Protocol", selectedSubprotocol);
    }
    return response;
}

HandshakeValidationResult HandshakeResponse::validateServerResponse(
    const http::Response& response, StringView clientKey, StringView expectedSubprotocol)
{
    if (response.status().value() != 101U) {
        return fail(DiagnosticCode::invalid_status);
    }
    if (!core::asciiEqualsIgnoreCase(response.headers().firstValue("Upgrade"), "websocket")) {
        return fail(DiagnosticCode::invalid_upgrade);
    }
    if (!core::containsCommaSeparatedAsciiToken(
            response.headers().firstValue("Connection"), "Upgrade")) {
        return fail(DiagnosticCode::invalid_connection);
    }
    if (response.headers().firstValue("Sec-WebSocket-Accept") != acceptKey(clientKey)) {
        return fail(DiagnosticCode::accept_mismatch);
    }
    if (!expectedSubprotocol.empty()
        && response.headers().firstValue("Sec-WebSocket-Protocol") != expectedSubprotocol) {
        return fail(DiagnosticCode::unsupported_subprotocol);
    }
    return {};
}

} // namespace arp::network::websocket
