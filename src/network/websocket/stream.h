#pragma once

#include "network/error.h"
#include "network/http/client.h"
#include "network/http/session.h"
#include "network/websocket/frame.h"
#include "network/websocket/handshake.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace arp::network::websocket {

enum class StreamRole {
    client,
    server,
};

enum class StreamState {
    initial,
    upgrading,
    open,
    close_sent,
    close_received,
    failed,
};

struct StreamConfig {
    FrameLimits frame_limits{};
    std::size_t read_buffer_size = 4096;
    bool mask_client_frames = true;
    MaskingKey client_mask_key{0x12, 0x34, 0x56, 0x78};
};

struct StreamStatus {
    error_code transport_error{};
    DiagnosticCode diagnostic = DiagnosticCode::none;
    http::DiagnosticCode http_diagnostic = http::DiagnosticCode::none;
    std::size_t bytes = 0;

    bool success() const noexcept;
};

struct SecureStreamPrepareResult {
    DiagnosticCode diagnostic = DiagnosticCode::none;
    http::DiagnosticCode http_diagnostic = http::DiagnosticCode::none;
    tls::TlsDiagnosticCode tls_diagnostic = tls::TlsDiagnosticCode::none;
    tls::TlsConfig tls_config;
    bool plaintext_fallback_allowed = false;

    bool success() const noexcept;
};

class ARP_API Stream {
public:
    explicit Stream(StreamRole role, StreamConfig config = {});

    static SecureStreamPrepareResult prepareSecureClient(const http::Uri& uri);

    StreamRole role() const noexcept;
    StreamState state() const noexcept;
    const StreamConfig& config() const noexcept;

    template <typename Transport, typename Handler>
    void async_client_upgrade(Transport& transport,
        StringView host,
        StringView resource,
        StringView key,
        Handler&& handler);

    template <typename Transport, typename Handler>
    void async_server_accept(
        Transport& transport, StringView selectedSubprotocol, Handler&& handler);

    template <typename Transport, typename Handler>
    void async_write_frame(Transport& transport, const Frame& frame, Handler&& handler);

    template <typename Transport, typename Handler>
    void async_write_text(Transport& transport, StringView text, Handler&& handler);

    template <typename Transport, typename Handler>
    void async_write_binary(Transport& transport, const byte_buffer& payload, Handler&& handler);

    template <typename Transport, typename Handler>
    void async_write_ping(Transport& transport, StringView text, Handler&& handler);

    template <typename Transport, typename Handler>
    void async_write_pong(Transport& transport, StringView text, Handler&& handler);

    template <typename Transport, typename Handler>
    void async_write_close(
        Transport& transport, std::uint16_t code, StringView reason, Handler&& handler);

    template <typename Transport, typename Handler>
    void async_read_frame(Transport& transport, Handler&& handler);

private:
    template <typename Transport, typename HandlerPtr>
    void async_read_frame_step(Transport& transport, HandlerPtr handler);

    static StreamStatus fromHttpStatus(const http::SessionStatus& status) noexcept;
    static StreamStatus fromDiagnostic(DiagnosticCode diagnostic) noexcept;

    Frame prepareOutgoingFrame(const Frame& frame) const;
    void store_handshake_key(StringView key);
    void store_selected_subprotocol(StringView subprotocol);

    StreamRole role_;
    StreamConfig config_;
    StreamState state_ = StreamState::initial;
    http::Session http_session_;
    FrameParser frame_parser_;
    byte_buffer read_buffer_;
    byte_buffer pending_;
    core::pmr::String handshake_key_;
    core::pmr::String selected_subprotocol_;
};

template <typename Transport, typename Handler>
void Stream::async_client_upgrade(
    Transport& transport, StringView host, StringView resource, StringView key, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    store_handshake_key(key);
    state_ = StreamState::upgrading;

    HandshakeRequest request;
    request.set_host(host);
    request.set_resource(resource);
    request.set_key(key);
    http_session_.async_write_request(transport,
        request.toHttpRequest(),
        [this, &transport, handlerState](const http::SessionStatus& writeStatus) {
            if (!writeStatus.success()) {
                state_ = StreamState::failed;
                (*handlerState)(fromHttpStatus(writeStatus));
                return;
            }
            http_session_.async_read_response(transport,
                [this, handlerState](
                    const http::SessionStatus& readStatus, const http::Response& response) {
                    if (!readStatus.success()) {
                        state_ = StreamState::failed;
                        (*handlerState)(fromHttpStatus(readStatus));
                        return;
                    }
                    const auto validation = HandshakeResponse::validateServerResponse(response,
                        StringView(handshake_key_.data(), handshake_key_.size()),
                        StringView());
                    if (!validation.success()) {
                        state_ = StreamState::failed;
                        (*handlerState)(fromDiagnostic(validation.diagnostic));
                        return;
                    }
                    state_ = StreamState::open;
                    (*handlerState)(StreamStatus{});
                });
        });
}

template <typename Transport, typename Handler>
void Stream::async_server_accept(
    Transport& transport, StringView selectedSubprotocol, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    store_selected_subprotocol(selectedSubprotocol);
    state_ = StreamState::upgrading;

    http_session_.async_read_request(transport,
        [this, &transport, handlerState](
            const http::SessionStatus& readStatus, const http::Request& request) {
            if (!readStatus.success()) {
                state_ = StreamState::failed;
                (*handlerState)(fromHttpStatus(readStatus));
                return;
            }
            const auto validation = HandshakeResponse::validateClientRequest(request);
            if (!validation.success()) {
                state_ = StreamState::failed;
                (*handlerState)(fromDiagnostic(validation.diagnostic));
                return;
            }
            const auto response = HandshakeResponse::buildServerResponse(
                request, StringView(selected_subprotocol_.data(), selected_subprotocol_.size()));
            http_session_.async_write_response(
                transport, response, [this, handlerState](const http::SessionStatus& writeStatus) {
                    if (!writeStatus.success()) {
                        state_ = StreamState::failed;
                        (*handlerState)(fromHttpStatus(writeStatus));
                        return;
                    }
                    state_ = StreamState::open;
                    (*handlerState)(StreamStatus{});
                });
        });
}

template <typename Transport, typename Handler>
void Stream::async_write_frame(Transport& transport, const Frame& frame, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    auto data =
        core::makeShared<byte_buffer>(FrameSerializer::serialize(prepareOutgoingFrame(frame)));
    transport.async_send(data->data(),
        data->size(),
        [this, handlerState, data, opcode = frame.opcode()](
            const error_code& ec, std::size_t bytes) {
            StreamStatus status;
            status.transport_error = ec;
            status.bytes = bytes;
            if (ec) {
                state_ = StreamState::failed;
            } else if (opcode == Opcode::close) {
                state_ = StreamState::close_sent;
            }
            (*handlerState)(status);
        });
}

template <typename Transport, typename Handler>
void Stream::async_write_text(Transport& transport, StringView text, Handler&& handler)
{
    async_write_frame(transport, Frame::text(text), std::forward<Handler>(handler));
}

template <typename Transport, typename Handler>
void Stream::async_write_binary(Transport& transport, const byte_buffer& payload, Handler&& handler)
{
    async_write_frame(transport, Frame::binary(payload), std::forward<Handler>(handler));
}

template <typename Transport, typename Handler>
void Stream::async_write_ping(Transport& transport, StringView text, Handler&& handler)
{
    async_write_frame(transport, Frame::ping(text), std::forward<Handler>(handler));
}

template <typename Transport, typename Handler>
void Stream::async_write_pong(Transport& transport, StringView text, Handler&& handler)
{
    async_write_frame(transport, Frame::pong(text), std::forward<Handler>(handler));
}

template <typename Transport, typename Handler>
void Stream::async_write_close(
    Transport& transport, std::uint16_t code, StringView reason, Handler&& handler)
{
    async_write_frame(transport, Frame::close(code, reason), std::forward<Handler>(handler));
}

template <typename Transport, typename Handler>
void Stream::async_read_frame(Transport& transport, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    async_read_frame_step(transport, handlerState);
}

template <typename Transport, typename HandlerPtr>
void Stream::async_read_frame_step(Transport& transport, HandlerPtr handler)
{
    if (!pending_.empty()) {
        const auto pendingResult = frame_parser_.parse(pending_);
        if (pendingResult.success()) {
            pending_.erase(pending_.begin(),
                pending_.begin() + static_cast<std::ptrdiff_t>(pendingResult.bytes_consumed));
            if (pendingResult.frame.opcode() == Opcode::close) {
                state_ = StreamState::close_received;
            }
            StreamStatus status;
            status.bytes = pendingResult.bytes_consumed;
            (*handler)(status, pendingResult.frame);
            return;
        }
        if (pendingResult.diagnostic != DiagnosticCode::incomplete_frame) {
            state_ = StreamState::failed;
            (*handler)(fromDiagnostic(pendingResult.diagnostic), pendingResult.frame);
            return;
        }
    }

    read_buffer_.assign(config_.read_buffer_size, 0);
    transport.async_recv(
        read_buffer_, [this, &transport, handler](const error_code& ec, std::size_t bytes) {
            if (ec) {
                StreamStatus status;
                status.transport_error = ec;
                status.bytes = bytes;
                state_ = StreamState::failed;
                (*handler)(status, Frame{});
                return;
            }
            if (bytes == 0U) {
                state_ = StreamState::failed;
                (*handler)(fromDiagnostic(DiagnosticCode::incomplete_frame), Frame{});
                return;
            }
            pending_.insert(pending_.end(),
                read_buffer_.begin(),
                read_buffer_.begin() + static_cast<std::ptrdiff_t>(bytes));
            const auto result = frame_parser_.parse(pending_);
            if (result.diagnostic == DiagnosticCode::incomplete_frame) {
                async_read_frame_step(transport, handler);
                return;
            }
            if (!result.success()) {
                state_ = StreamState::failed;
                (*handler)(fromDiagnostic(result.diagnostic), result.frame);
                return;
            }
            pending_.erase(pending_.begin(),
                pending_.begin() + static_cast<std::ptrdiff_t>(result.bytes_consumed));
            if (result.frame.opcode() == Opcode::close) {
                state_ = StreamState::close_received;
            }
            StreamStatus status;
            status.bytes = result.bytes_consumed;
            (*handler)(status, result.frame);
        });
}

} // namespace arp::network::websocket
