#pragma once

#include "network/http/message.h"

#include <cstddef>

namespace arp::network::http {

struct ParserLimits {
    std::size_t max_header_bytes = 8192;
    std::size_t max_header_count = 64;
    std::size_t max_body_bytes = 1048576;
    std::size_t max_buffer_bytes = 2097152;
    bool server_request_validation = false;
    bool head_response = false;
};

enum class ParseStatus {
    need_more,
    done,
    failed,
};

struct ParseResult {
    ParseStatus status = ParseStatus::need_more;
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

class ARP_API RequestParser {
public:
    explicit RequestParser(ParserLimits limits = {});

    ParseResult feed(StringView data);
    void reset();
    const Request& request() const noexcept;

private:
    ParseResult parse_buffer();
    ParseResult fail(DiagnosticCode diagnostic) noexcept;

    ParserLimits limits_{};
    core::pmr::String buffer_;
    Request request_;
    ParseStatus status_ = ParseStatus::need_more;
    DiagnosticCode diagnostic_ = DiagnosticCode::none;
};

class ARP_API ResponseParser {
public:
    explicit ResponseParser(ParserLimits limits = {});

    ParseResult feed(StringView data);
    void reset();
    const Response& response() const noexcept;

private:
    ParseResult parse_buffer();
    ParseResult fail(DiagnosticCode diagnostic) noexcept;

    ParserLimits limits_{};
    core::pmr::String buffer_;
    Response response_;
    ParseStatus status_ = ParseStatus::need_more;
    DiagnosticCode diagnostic_ = DiagnosticCode::none;
};

class ARP_API ResponseStreamSink {
public:
    virtual ~ResponseStreamSink();
    virtual void on_headers(const Response& response) = 0;
    virtual void on_body(StringView chunk) = 0;
};

enum class ResponseStreamBodyMode {
    none,
    fixed,
    chunked,
};

class ARP_API ResponseStreamParser {
public:
    explicit ResponseStreamParser(ParserLimits limits = {});

    ParseResult feed(StringView data, ResponseStreamSink& sink);
    void reset();
    const Response& response() const noexcept;

private:
    ParseResult parse_buffer(ResponseStreamSink& sink);
    ParseResult parse_headers(ResponseStreamSink& sink);
    ParseResult parse_fixed_body(ResponseStreamSink& sink);
    ParseResult parse_chunked_body(ResponseStreamSink& sink);
    ParseResult complete() noexcept;
    ParseResult fail(DiagnosticCode diagnostic) noexcept;

    ParserLimits limits_{};
    core::pmr::String buffer_;
    Response response_;
    ParseStatus status_ = ParseStatus::need_more;
    DiagnosticCode diagnostic_ = DiagnosticCode::none;
    ResponseStreamBodyMode body_mode_ = ResponseStreamBodyMode::none;
    bool headers_parsed_ = false;
    std::size_t content_length_ = 0;
    std::size_t body_received_ = 0;
    std::size_t chunk_remaining_ = 0;
    bool expect_chunk_data_crlf_ = false;
};

} // namespace arp::network::http
