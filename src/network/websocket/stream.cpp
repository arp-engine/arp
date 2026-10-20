#include "network/websocket/stream.h"

namespace arp::network::websocket {

bool StreamStatus::success() const noexcept
{
    return !transport_error && diagnostic == DiagnosticCode::none
        && http_diagnostic == http::DiagnosticCode::none;
}

bool SecureStreamPrepareResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none && http_diagnostic == http::DiagnosticCode::none
        && tls_diagnostic == tls::TlsDiagnosticCode::none;
}

Stream::Stream(StreamRole role, StreamConfig config)
    : role_(role), config_(config), frame_parser_(config.frame_limits),
      read_buffer_(&core::defaultCpuMemoryResource()), pending_(&core::defaultCpuMemoryResource()),
      handshake_key_(&core::defaultCpuMemoryResource()),
      selected_subprotocol_(&core::defaultCpuMemoryResource())
{}

SecureStreamPrepareResult Stream::prepareSecureClient(const http::Uri& uri)
{
    SecureStreamPrepareResult result;
    result.plaintext_fallback_allowed = false;
    if (!uri.is_absolute()) {
        result.http_diagnostic = http::DiagnosticCode::malformed_uri;
        return result;
    }
    if (uri.scheme() != "wss") {
        result.http_diagnostic = http::DiagnosticCode::unsupported_scheme;
        return result;
    }
    const auto secure = http::Client::prepareSecureTransport(uri);
    result.http_diagnostic = secure.diagnostic;
    result.tls_diagnostic = secure.tls_diagnostic;
    result.tls_config = secure.tls_config;
    return result;
}

StreamRole Stream::role() const noexcept
{
    return role_;
}

StreamState Stream::state() const noexcept
{
    return state_;
}

const StreamConfig& Stream::config() const noexcept
{
    return config_;
}

StreamStatus Stream::fromHttpStatus(const http::SessionStatus& status) noexcept
{
    StreamStatus result;
    result.transport_error = status.transport_error;
    result.http_diagnostic = status.diagnostic;
    result.bytes = status.bytes;
    return result;
}

StreamStatus Stream::fromDiagnostic(DiagnosticCode diagnostic) noexcept
{
    StreamStatus status;
    status.diagnostic = diagnostic;
    return status;
}

Frame Stream::prepareOutgoingFrame(const Frame& frame) const
{
    Frame outgoing = frame;
    if (role_ == StreamRole::client && config_.mask_client_frames && !outgoing.masked()) {
        outgoing.set_mask(config_.client_mask_key);
    }
    return outgoing;
}

void Stream::store_handshake_key(StringView key)
{
    handshake_key_.assign(key.data(), key.size());
}

void Stream::store_selected_subprotocol(StringView subprotocol)
{
    selected_subprotocol_.assign(subprotocol.data(), subprotocol.size());
}

} // namespace arp::network::websocket
