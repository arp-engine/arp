#pragma once

#include "network/error.h"
#include "network/http/parser.h"
#include "network/http/serializer.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace arp::network::http {

struct SessionConfig {
    std::size_t read_buffer_size = 4096;
    ParserLimits parser_limits{};
};

struct SessionStatus {
    error_code transport_error{};
    DiagnosticCode diagnostic = DiagnosticCode::none;
    std::size_t bytes = 0;

    bool success() const noexcept;
};

namespace detail {

template <typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
struct ResponseStreamHandlerState {
    ResponseStreamHandlerState(
        HeadersHandler&& headers, BodyHandler&& body, CompleteHandler&& complete)
        : on_headers(std::move(headers)), on_body(std::move(body)), on_complete(std::move(complete))
    {}

    HeadersHandler on_headers;
    BodyHandler on_body;
    CompleteHandler on_complete;
    bool proceed = true;
};

template <typename HandlerState> class ResponseStreamSinkAdapter final : public ResponseStreamSink {
public:
    explicit ResponseStreamSinkAdapter(HandlerState state) : state_(std::move(state)) {}

    void on_headers(const Response& response) override
    {
        if constexpr (std::is_same_v<decltype(state_->on_headers(response)), bool>)
            state_->proceed = state_->on_headers(response);
        else
            state_->on_headers(response);
    }

    void on_body(StringView chunk) override
    {
        if constexpr (std::is_same_v<decltype(state_->on_body(chunk)), bool>)
            state_->proceed = state_->on_body(chunk);
        else
            state_->on_body(chunk);
    }

    bool shouldContinue() const noexcept override { return state_->proceed; }

private:
    HandlerState state_;
};

} // namespace detail

class ARP_API Session {
public:
    Session();
    explicit Session(SessionConfig config);

    const SessionConfig& config() const noexcept;

    template <typename Stream, typename Handler>
    void async_write_request(Stream& stream, const Request& request, Handler&& handler);

    template <typename Stream, typename Handler>
    void async_write_response(Stream& stream, const Response& response, Handler&& handler);

    template <typename Stream, typename Handler>
    void async_read_request(Stream& stream, Handler&& handler);

    template <typename Stream, typename Handler>
    void async_read_response(Stream& stream, Handler&& handler);

    template <typename Stream,
        typename HeadersHandler,
        typename BodyHandler,
        typename CompleteHandler>
    void async_read_response_stream(Stream& stream,
        HeadersHandler&& on_headers,
        BodyHandler&& on_body,
        CompleteHandler&& on_complete);

private:
    template <typename Stream, typename HandlerPtr>
    void async_read_request_step(Stream& stream, HandlerPtr handler);

    template <typename Stream, typename HandlerPtr>
    void async_read_response_step(Stream& stream, HandlerPtr handler);

    template <typename Stream, typename HandlerPtr>
    void async_read_response_stream_step(Stream& stream, HandlerPtr handler);

    SessionConfig config_{};
    RequestParser request_parser_;
    ResponseParser response_parser_;
    ResponseStreamParser response_stream_parser_;
    byte_buffer read_buffer_;
};

template <typename Stream, typename Handler>
void Session::async_write_request(Stream& stream, const Request& request, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    const auto serialized = Serializer::serializeRequest(request);
    if (!serialized.success()) {
        SessionStatus status;
        status.diagnostic = serialized.diagnostic;
        (*handlerState)(status);
        return;
    }
    stream.async_send(serialized.text.data(),
        serialized.text.size(),
        [handlerState](const error_code& ec, std::size_t bytes) {
            SessionStatus status;
            status.transport_error = ec;
            status.bytes = bytes;
            (*handlerState)(status);
        });
}

template <typename Stream, typename Handler>
void Session::async_write_response(Stream& stream, const Response& response, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    const auto serialized = Serializer::serializeResponse(response);
    if (!serialized.success()) {
        SessionStatus status;
        status.diagnostic = serialized.diagnostic;
        (*handlerState)(status);
        return;
    }
    stream.async_send(serialized.text.data(),
        serialized.text.size(),
        [handlerState](const error_code& ec, std::size_t bytes) {
            SessionStatus status;
            status.transport_error = ec;
            status.bytes = bytes;
            (*handlerState)(status);
        });
}

template <typename Stream, typename Handler>
void Session::async_read_request(Stream& stream, Handler&& handler)
{
    request_parser_.reset();
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    async_read_request_step(stream, handlerState);
}

template <typename Stream, typename Handler>
void Session::async_read_response(Stream& stream, Handler&& handler)
{
    response_parser_.reset();
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    async_read_response_step(stream, handlerState);
}

template <typename Stream, typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
void Session::async_read_response_stream(Stream& stream,
    HeadersHandler&& on_headers,
    BodyHandler&& on_body,
    CompleteHandler&& on_complete)
{
    response_stream_parser_.reset();
    using HandlerState = detail::ResponseStreamHandlerState<std::decay_t<HeadersHandler>,
        std::decay_t<BodyHandler>,
        std::decay_t<CompleteHandler>>;
    auto handlerState = core::makeShared<HandlerState>(std::forward<HeadersHandler>(on_headers),
        std::forward<BodyHandler>(on_body),
        std::forward<CompleteHandler>(on_complete));
    async_read_response_stream_step(stream, handlerState);
}

template <typename Stream, typename HandlerPtr>
void Session::async_read_request_step(Stream& stream, HandlerPtr handler)
{
    read_buffer_.assign(config_.read_buffer_size, 0);
    stream.async_recv(
        read_buffer_, [this, &stream, handler](const error_code& ec, std::size_t bytes) {
            SessionStatus status;
            status.transport_error = ec;
            status.bytes = bytes;
            if (ec) {
                (*handler)(status, request_parser_.request());
                return;
            }
            if (bytes == 0) {
                status.diagnostic = DiagnosticCode::connection_closed;
                (*handler)(status, request_parser_.request());
                return;
            }
            const auto result = request_parser_.feed(
                StringView(reinterpret_cast<const char*>(read_buffer_.data()), bytes));
            if (result.status == ParseStatus::failed) {
                status.diagnostic = result.diagnostic;
                (*handler)(status, request_parser_.request());
                return;
            }
            if (result.status == ParseStatus::done) {
                (*handler)(status, request_parser_.request());
                return;
            }
            async_read_request_step(stream, handler);
        });
}

template <typename Stream, typename HandlerPtr>
void Session::async_read_response_step(Stream& stream, HandlerPtr handler)
{
    read_buffer_.assign(config_.read_buffer_size, 0);
    stream.async_recv(
        read_buffer_, [this, &stream, handler](const error_code& ec, std::size_t bytes) {
            SessionStatus status;
            status.transport_error = ec;
            status.bytes = bytes;
            if (ec) {
                (*handler)(status, response_parser_.response());
                return;
            }
            if (bytes == 0) {
                status.diagnostic = DiagnosticCode::connection_closed;
                (*handler)(status, response_parser_.response());
                return;
            }
            const auto result = response_parser_.feed(
                StringView(reinterpret_cast<const char*>(read_buffer_.data()), bytes));
            if (result.status == ParseStatus::failed) {
                status.diagnostic = result.diagnostic;
                (*handler)(status, response_parser_.response());
                return;
            }
            if (result.status == ParseStatus::done) {
                (*handler)(status, response_parser_.response());
                return;
            }
            async_read_response_step(stream, handler);
        });
}

template <typename Stream, typename HandlerPtr>
void Session::async_read_response_stream_step(Stream& stream, HandlerPtr handler)
{
    read_buffer_.assign(config_.read_buffer_size, 0);
    stream.async_recv(
        read_buffer_, [this, &stream, handler](const error_code& ec, std::size_t bytes) {
            SessionStatus status;
            status.transport_error = ec;
            status.bytes = bytes;
            if (ec) {
                handler->on_complete(status);
                return;
            }
            if (bytes == 0) {
                detail::ResponseStreamSinkAdapter<HandlerPtr> sink(handler);
                status.diagnostic = response_stream_parser_.finish(sink).diagnostic;
                handler->on_complete(status);
                return;
            }

            detail::ResponseStreamSinkAdapter<HandlerPtr> sink(handler);
            const auto result = response_stream_parser_.feed(
                StringView(reinterpret_cast<const char*>(read_buffer_.data()), bytes), sink);
            if (result.status == ParseStatus::failed) {
                status.diagnostic = result.diagnostic;
                handler->on_complete(status);
                return;
            }
            if (result.status == ParseStatus::done) {
                handler->on_complete(status);
                return;
            }
            async_read_response_stream_step(stream, handler);
        });
}

} // namespace arp::network::http
