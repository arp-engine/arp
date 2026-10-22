#include "network/http/parser.h"

#include "core/text/path_pattern.h"
#include "core/text/string_utils.h"

#include <cstdint>
#include <limits>

namespace arp::network::http {

namespace {

constexpr std::size_t npos = StringView::npos;

ParseResult needMore() noexcept
{
    return {};
}

ParseResult done() noexcept
{
    ParseResult result;
    result.status = ParseStatus::done;
    return result;
}

ParseResult failed(DiagnosticCode diagnostic) noexcept
{
    ParseResult result;
    result.status = ParseStatus::failed;
    result.diagnostic = diagnostic;
    return result;
}

StringView viewOf(const core::pmr::String& text) noexcept
{
    return StringView(text.data(), text.size());
}

bool hasBareLf(StringView text) noexcept
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            return true;
        }
    }
    return false;
}

bool parseUnsigned(StringView text, std::size_t& out) noexcept
{
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const auto digit = static_cast<std::size_t>(c - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

bool parseHex(StringView text, std::size_t& out) noexcept
{
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    for (char c : text) {
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(c - 'A' + 10);
        } else {
            return false;
        }
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 16)
            return false;
        value = value * 16 + digit;
    }
    out = value;
    return true;
}

bool parseVersion(StringView text, Version& version) noexcept
{
    // RFC 9112 HTTP-version has exactly one digit on each side of the dot.
    if (text.size() != 8 || text.substr(0, 5) != "HTTP/" || text[6] != '.') {
        return false;
    }
    const auto dot = text.find('.', 5);
    if (dot == npos) {
        return false;
    }
    std::size_t major = 0;
    std::size_t minor = 0;
    if (!parseUnsigned(text.substr(5, dot - 5), major)
        || !parseUnsigned(text.substr(dot + 1), minor)) {
        return false;
    }
    if (major > 65535 || minor > 65535) {
        return false;
    }
    version.major = static_cast<std::uint16_t>(major);
    version.minor = static_cast<std::uint16_t>(minor);
    return true;
}

// Chunk extensions are ignored semantically, but their syntax must be valid.
// Use the same HTTP token validator as header names for both token positions.
bool parseChunkSizeLine(StringView line, std::size_t& size) noexcept
{
    if (line.empty() || line.front() == ' ' || line.front() == '\t')
        return false;
    const auto extension = line.find(';');
    const auto digits = extension == npos ? line : core::trimAsciiOws(line.substr(0, extension));
    if (!parseHex(digits, size))
        return false;
    if (extension == npos)
        return true;
    auto suffix = line.substr(extension);
    while (!suffix.empty()) {
        if (suffix.front() != ';')
            return false;
        suffix = core::trimAsciiOws(suffix.substr(1));
        const auto name = suffix.substr(0, suffix.find_first_of("=; \t"));
        if (validateHeaderName(name) != DiagnosticCode::none)
            return false;
        suffix.remove_prefix(name.size());
        suffix = core::trimAsciiOws(suffix);
        if (suffix.empty() || suffix.front() == ';')
            continue;
        if (suffix.front() != '=')
            return false;
        suffix = core::trimAsciiOws(suffix.substr(1));
        if (suffix.empty())
            return false;
        if (suffix.front() == '"') {
            suffix.remove_prefix(1);
            bool closed = false;
            while (!suffix.empty()) {
                auto ch = static_cast<unsigned char>(suffix.front());
                suffix.remove_prefix(1);
                if (ch == '"') {
                    closed = true;
                    break;
                }
                if (ch == '\\') {
                    if (suffix.empty())
                        return false;
                    ch = static_cast<unsigned char>(suffix.front());
                    suffix.remove_prefix(1);
                }
                if ((ch < 32 && ch != '\t') || ch == 127)
                    return false;
            }
            if (!closed)
                return false;
        } else {
            const auto value = suffix.substr(0, suffix.find_first_of("; \t"));
            if (validateHeaderName(value) != DiagnosticCode::none)
                return false;
            suffix.remove_prefix(value.size());
        }
        suffix = core::trimAsciiOws(suffix);
    }
    return true;
}

DiagnosticCode parseRequestStartLine(StringView line, Request& request)
{
    const auto firstSpace = line.find(' ');
    if (firstSpace == npos) {
        return DiagnosticCode::malformed_start_line;
    }
    const auto secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == npos || secondSpace == firstSpace + 1) {
        return DiagnosticCode::malformed_start_line;
    }
    const auto method = parseMethod(line.substr(0, firstSpace));
    if (!method.success()) {
        return method.diagnostic;
    }
    Version version;
    if (!parseVersion(line.substr(secondSpace + 1), version)) {
        return DiagnosticCode::malformed_start_line;
    }
    request.set_method(method.method);
    request.set_target(line.substr(firstSpace + 1, secondSpace - firstSpace - 1));
    request.set_version(version);
    return DiagnosticCode::none;
}

DiagnosticCode parseResponseStartLine(StringView line, Response& response)
{
    const auto firstSpace = line.find(' ');
    if (firstSpace == npos) {
        return DiagnosticCode::malformed_start_line;
    }
    Version version;
    if (!parseVersion(line.substr(0, firstSpace), version)) {
        return DiagnosticCode::malformed_start_line;
    }
    const auto secondSpace = line.find(' ', firstSpace + 1);
    const auto statusText = secondSpace == npos
        ? line.substr(firstSpace + 1)
        : line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::size_t statusValue = 0;
    if (!parseUnsigned(statusText, statusValue) || statusValue > 65535) {
        return DiagnosticCode::invalid_status_code;
    }
    const auto status = StatusCode::parse(static_cast<std::uint16_t>(statusValue));
    if (!status.success()) {
        return status.diagnostic;
    }
    response.set_version(version);
    response.set_status(status.status);
    response.set_reason(secondSpace == npos ? StringView() : line.substr(secondSpace + 1));
    return DiagnosticCode::none;
}

struct HeaderInfo {
    bool has_content_length = false;
    std::size_t content_length = 0;
    bool chunked = false;
};

DiagnosticCode parseContentLength(StringView value, HeaderInfo& info) noexcept
{
    std::size_t parsed = 0;
    if (!parseUnsigned(value, parsed)) {
        return DiagnosticCode::invalid_content_length;
    }
    if (info.has_content_length && info.content_length != parsed) {
        return DiagnosticCode::conflicting_content_length;
    }
    info.has_content_length = true;
    info.content_length = parsed;
    return DiagnosticCode::none;
}

DiagnosticCode parseHeaderLines(StringView block,
    std::size_t start,
    const ParserLimits& limits,
    HeaderList& headers,
    HeaderInfo& info)
{
    std::size_t count = 0;
    std::size_t cursor = start;
    while (cursor < block.size()) {
        const auto lineEnd = block.find("\r\n", cursor);
        const auto line =
            lineEnd == npos ? block.substr(cursor) : block.substr(cursor, lineEnd - cursor);
        if (!line.empty()) {
            ++count;
            if (count > limits.max_header_count) {
                return DiagnosticCode::too_many_headers;
            }
            const auto colon = line.find(':');
            if (colon == npos) {
                return DiagnosticCode::malformed_header;
            }
            const auto name = line.substr(0, colon);
            const auto value = core::trimAsciiOws(line.substr(colon + 1));
            const auto diagnostic = headers.add(name, value);
            if (diagnostic != DiagnosticCode::none) {
                return diagnostic;
            }
            if (core::asciiEqualsIgnoreCase(name, "Content-Length")) {
                const auto lengthDiagnostic = parseContentLength(value, info);
                if (lengthDiagnostic != DiagnosticCode::none) {
                    return lengthDiagnostic;
                }
            } else if (core::asciiEqualsIgnoreCase(name, "Transfer-Encoding")) {
                if (info.chunked)
                    return DiagnosticCode::ambiguous_framing;
                if (core::asciiEqualsIgnoreCase(value, "chunked")) {
                    info.chunked = true;
                } else {
                    return DiagnosticCode::unsupported_transfer_encoding;
                }
            }
        }
        if (lineEnd == npos) {
            break;
        }
        cursor = lineEnd + 2;
    }
    if (info.chunked && info.has_content_length)
        return DiagnosticCode::ambiguous_framing;
    return DiagnosticCode::none;
}

ParseResult parseFixedBody(StringView buffer,
    std::size_t bodyStart,
    const ParserLimits& limits,
    std::size_t length,
    byte_buffer& body)
{
    if (length > limits.max_body_bytes) {
        return failed(DiagnosticCode::body_too_large);
    }
    if (buffer.size() - bodyStart < length) {
        return needMore();
    }
    body.clear();
    body.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        body.push_back(static_cast<std::uint8_t>(buffer[bodyStart + i]));
    }
    return done();
}

ParseResult parseChunkedBody(StringView buffer,
    std::size_t bodyStart,
    const ParserLimits& limits,
    byte_buffer& body,
    std::size_t* consumed = nullptr)
{
    body.clear();
    std::size_t cursor = bodyStart;
    for (;;) {
        const auto lineEnd = buffer.find("\r\n", cursor);
        if (lineEnd == npos) {
            return needMore();
        }
        const auto sizeText = buffer.substr(cursor, lineEnd - cursor);
        std::size_t chunkSize = 0;
        if (!parseChunkSizeLine(sizeText, chunkSize)) {
            return failed(DiagnosticCode::invalid_chunk_size);
        }
        cursor = lineEnd + 2;
        if (chunkSize == 0) {
            if (buffer.size() < cursor + 2) {
                return needMore();
            }
            if (buffer[cursor] != '\r' || buffer[cursor + 1] != '\n') {
                return failed(DiagnosticCode::invalid_chunk_size);
            }
            if (consumed)
                *consumed = cursor + 2;
            return done();
        }
        if (chunkSize > limits.max_body_bytes - body.size()) {
            return failed(DiagnosticCode::body_too_large);
        }
        if (buffer.size() - cursor < 2 || chunkSize > buffer.size() - cursor - 2) {
            return needMore();
        }
        if (buffer[cursor + chunkSize] != '\r' || buffer[cursor + chunkSize + 1] != '\n') {
            return failed(DiagnosticCode::invalid_chunk_size);
        }
        for (std::size_t i = 0; i < chunkSize; ++i) {
            body.push_back(static_cast<std::uint8_t>(buffer[cursor + i]));
        }
        cursor += chunkSize + 2;
    }
}

} // namespace

bool ParseResult::success() const noexcept
{
    return status == ParseStatus::done && diagnostic == DiagnosticCode::none;
}

RequestParser::RequestParser(ParserLimits limits)
    : limits_(limits), buffer_(&core::defaultCpuMemoryResource())
{}

ParseResult RequestParser::feed(StringView data)
{
    if (status_ == ParseStatus::done || status_ == ParseStatus::failed) {
        ParseResult result;
        result.status = status_;
        result.diagnostic = diagnostic_;
        return result;
    }
    if (buffer_.size() > limits_.max_buffer_bytes
        || data.size() > limits_.max_buffer_bytes - buffer_.size())
        return fail(DiagnosticCode::buffer_too_large);
    buffer_.append(data.data(), data.size());
    return parse_buffer();
}

void RequestParser::reset()
{
    buffer_.clear();
    request_ = Request();
    status_ = ParseStatus::need_more;
    diagnostic_ = DiagnosticCode::none;
}

const Request& RequestParser::request() const noexcept
{
    return request_;
}

ParseResult RequestParser::fail(DiagnosticCode diagnostic) noexcept
{
    status_ = ParseStatus::failed;
    diagnostic_ = diagnostic;
    return failed(diagnostic);
}

ParseResult RequestParser::parse_buffer()
{
    const auto buffer = viewOf(buffer_);
    const auto headerEnd = buffer.find("\r\n\r\n");
    if (hasBareLf(buffer.substr(0, headerEnd)))
        return fail(DiagnosticCode::malformed_start_line);
    if (headerEnd == npos) {
        if (buffer.size() > limits_.max_header_bytes) {
            return fail(DiagnosticCode::header_too_large);
        }
        return needMore();
    }
    if (headerEnd + 4 > limits_.max_header_bytes) {
        return fail(DiagnosticCode::header_too_large);
    }

    Request candidate;
    const auto headerBlock = buffer.substr(0, headerEnd);
    const auto startEnd = headerBlock.find("\r\n");
    const auto startLine = startEnd == npos ? headerBlock : headerBlock.substr(0, startEnd);
    auto diagnostic = parseRequestStartLine(startLine, candidate);
    if (diagnostic != DiagnosticCode::none) {
        return fail(diagnostic);
    }

    HeaderInfo info;
    const auto headerStart = startEnd == npos ? headerBlock.size() : startEnd + 2;
    diagnostic = parseHeaderLines(headerBlock, headerStart, limits_, candidate.headers(), info);
    if (diagnostic != DiagnosticCode::none) {
        return fail(diagnostic);
    }

    if (limits_.server_request_validation) {
        if (candidate.version().major != 1 || candidate.version().minor != 1)
            return fail(DiagnosticCode::unsupported_http_version);
        if (candidate.headers().count("Host") != 1 || candidate.header("Host").empty())
            return fail(DiagnosticCode::invalid_host_header);
        if (!candidate.header("Expect").empty())
            return fail(DiagnosticCode::expectation_not_supported);
        if (!core::PathPattern::validPath(
                candidate.target().substr(0, candidate.target().find('?')))
            || !core::PathPattern::validEncodedText(candidate.target()))
            return fail(DiagnosticCode::invalid_request_target);
    }

    const auto bodyStart = headerEnd + 4;
    ParseResult bodyResult;
    std::size_t consumed = bodyStart;
    if (info.chunked) {
        byte_buffer body{&core::defaultCpuMemoryResource()};
        bodyResult = parseChunkedBody(buffer, bodyStart, limits_, body, &consumed);
        if (bodyResult.status == ParseStatus::done) {
            candidate.set_body(body);
        }
    } else if (info.has_content_length) {
        byte_buffer body{&core::defaultCpuMemoryResource()};
        consumed = bodyStart + info.content_length;
        bodyResult = parseFixedBody(buffer, bodyStart, limits_, info.content_length, body);
        if (bodyResult.status == ParseStatus::done) {
            candidate.set_body(body);
        }
    } else {
        bodyResult = done();
    }

    if (bodyResult.status == ParseStatus::failed) {
        return fail(bodyResult.diagnostic);
    }
    if (bodyResult.status == ParseStatus::need_more) {
        return needMore();
    }
    if (limits_.server_request_validation && buffer.size() != consumed)
        return fail(DiagnosticCode::ambiguous_framing);
    request_ = candidate;
    status_ = ParseStatus::done;
    diagnostic_ = DiagnosticCode::none;
    return done();
}

ResponseParser::ResponseParser(ParserLimits limits)
    : limits_(limits), buffer_(&core::defaultCpuMemoryResource())
{}

ParseResult ResponseParser::feed(StringView data)
{
    if (status_ == ParseStatus::done || status_ == ParseStatus::failed) {
        ParseResult result;
        result.status = status_;
        result.diagnostic = diagnostic_;
        return result;
    }
    if (buffer_.size() > limits_.max_buffer_bytes
        || data.size() > limits_.max_buffer_bytes - buffer_.size())
        return fail(DiagnosticCode::buffer_too_large);
    buffer_.append(data.data(), data.size());
    return parse_buffer();
}

void ResponseParser::reset()
{
    buffer_.clear();
    response_ = Response();
    status_ = ParseStatus::need_more;
    diagnostic_ = DiagnosticCode::none;
}

const Response& ResponseParser::response() const noexcept
{
    return response_;
}

ParseResult ResponseParser::fail(DiagnosticCode diagnostic) noexcept
{
    status_ = ParseStatus::failed;
    diagnostic_ = diagnostic;
    return failed(diagnostic);
}

ParseResult ResponseParser::parse_buffer()
{
    const auto buffer = viewOf(buffer_);
    const auto headerEnd = buffer.find("\r\n\r\n");
    if (hasBareLf(buffer.substr(0, headerEnd)))
        return fail(DiagnosticCode::malformed_start_line);
    if (headerEnd == npos) {
        if (buffer.size() > limits_.max_header_bytes) {
            return fail(DiagnosticCode::header_too_large);
        }
        return needMore();
    }
    if (headerEnd + 4 > limits_.max_header_bytes) {
        return fail(DiagnosticCode::header_too_large);
    }

    Response candidate;
    const auto headerBlock = buffer.substr(0, headerEnd);
    const auto startEnd = headerBlock.find("\r\n");
    const auto startLine = startEnd == npos ? headerBlock : headerBlock.substr(0, startEnd);
    auto diagnostic = parseResponseStartLine(startLine, candidate);
    if (diagnostic != DiagnosticCode::none) {
        return fail(diagnostic);
    }

    HeaderInfo info;
    const auto headerStart = startEnd == npos ? headerBlock.size() : startEnd + 2;
    diagnostic = parseHeaderLines(headerBlock, headerStart, limits_, candidate.headers(), info);
    if (diagnostic != DiagnosticCode::none) {
        return fail(diagnostic);
    }

    const auto bodyStart = headerEnd + 4;
    ParseResult bodyResult;
    if (candidate.status().value() < 200 || candidate.status().value() == 204
        || candidate.status().value() == 304 || limits_.head_response) {
        bodyResult = done();
    } else if (info.chunked) {
        byte_buffer body{&core::defaultCpuMemoryResource()};
        bodyResult = parseChunkedBody(buffer, bodyStart, limits_, body);
        if (bodyResult.status == ParseStatus::done) {
            candidate.set_body(body);
        }
    } else if (info.has_content_length) {
        byte_buffer body{&core::defaultCpuMemoryResource()};
        bodyResult = parseFixedBody(buffer, bodyStart, limits_, info.content_length, body);
        if (bodyResult.status == ParseStatus::done) {
            candidate.set_body(body);
        }
    } else {
        bodyResult = done();
    }

    if (bodyResult.status == ParseStatus::failed) {
        return fail(bodyResult.diagnostic);
    }
    if (bodyResult.status == ParseStatus::need_more) {
        return needMore();
    }
    response_ = candidate;
    status_ = ParseStatus::done;
    diagnostic_ = DiagnosticCode::none;
    return done();
}

ResponseStreamSink::~ResponseStreamSink() = default;

ResponseStreamParser::ResponseStreamParser(ParserLimits limits)
    : limits_(limits), buffer_(&core::defaultCpuMemoryResource())
{}

ParseResult ResponseStreamParser::feed(StringView data, ResponseStreamSink& sink)
{
    if (status_ == ParseStatus::done || status_ == ParseStatus::failed) {
        ParseResult result;
        result.status = status_;
        result.diagnostic = diagnostic_;
        return result;
    }
    if (buffer_.size() > limits_.max_buffer_bytes
        || data.size() > limits_.max_buffer_bytes - buffer_.size())
        return fail(DiagnosticCode::buffer_too_large);
    buffer_.append(data.data(), data.size());
    return parse_buffer(sink);
}

void ResponseStreamParser::reset()
{
    buffer_.clear();
    response_ = Response();
    status_ = ParseStatus::need_more;
    diagnostic_ = DiagnosticCode::none;
    body_mode_ = ResponseStreamBodyMode::none;
    headers_parsed_ = false;
    content_length_ = 0;
    body_received_ = 0;
    chunk_remaining_ = 0;
    expect_chunk_data_crlf_ = false;
}

const Response& ResponseStreamParser::response() const noexcept
{
    return response_;
}

ParseResult ResponseStreamParser::parse_buffer(ResponseStreamSink& sink)
{
    if (!headers_parsed_) {
        const auto headerResult = parse_headers(sink);
        if (headerResult.status != ParseStatus::done) {
            return headerResult;
        }
        if (body_mode_ == ResponseStreamBodyMode::none) {
            return complete();
        }
    }

    if (body_mode_ == ResponseStreamBodyMode::fixed) {
        return parse_fixed_body(sink);
    }
    if (body_mode_ == ResponseStreamBodyMode::chunked) {
        return parse_chunked_body(sink);
    }
    return complete();
}

ParseResult ResponseStreamParser::parse_headers(ResponseStreamSink& sink)
{
    const auto buffer = viewOf(buffer_);
    const auto headerEnd = buffer.find("\r\n\r\n");
    if (hasBareLf(buffer.substr(0, headerEnd)))
        return fail(DiagnosticCode::malformed_start_line);
    if (headerEnd == npos) {
        if (buffer.size() > limits_.max_header_bytes) {
            return fail(DiagnosticCode::header_too_large);
        }
        return needMore();
    }
    if (headerEnd + 4 > limits_.max_header_bytes) {
        return fail(DiagnosticCode::header_too_large);
    }

    Response candidate;
    const auto headerBlock = buffer.substr(0, headerEnd);
    const auto startEnd = headerBlock.find("\r\n");
    const auto startLine = startEnd == npos ? headerBlock : headerBlock.substr(0, startEnd);
    auto diagnostic = parseResponseStartLine(startLine, candidate);
    if (diagnostic != DiagnosticCode::none) {
        return fail(diagnostic);
    }

    HeaderInfo info;
    const auto headerStart = startEnd == npos ? headerBlock.size() : startEnd + 2;
    diagnostic = parseHeaderLines(headerBlock, headerStart, limits_, candidate.headers(), info);
    if (diagnostic != DiagnosticCode::none) {
        return fail(diagnostic);
    }

    if (candidate.status().value() < 200 || candidate.status().value() == 204
        || candidate.status().value() == 304 || limits_.head_response) {
        body_mode_ = ResponseStreamBodyMode::none;
    } else if (info.chunked) {
        body_mode_ = ResponseStreamBodyMode::chunked;
    } else if (info.has_content_length) {
        if (info.content_length > limits_.max_body_bytes) {
            return fail(DiagnosticCode::body_too_large);
        }
        body_mode_ =
            info.content_length == 0 ? ResponseStreamBodyMode::none : ResponseStreamBodyMode::fixed;
        content_length_ = info.content_length;
    } else {
        body_mode_ = ResponseStreamBodyMode::none;
    }

    response_ = candidate;
    headers_parsed_ = true;
    buffer_.erase(0, headerEnd + 4);
    sink.on_headers(response_);
    return done();
}

ParseResult ResponseStreamParser::parse_fixed_body(ResponseStreamSink& sink)
{
    if (body_received_ >= content_length_) {
        return complete();
    }
    if (buffer_.empty()) {
        return needMore();
    }

    const auto remaining = content_length_ - body_received_;
    const auto emitBytes = buffer_.size() < remaining ? buffer_.size() : remaining;
    sink.on_body(StringView(buffer_.data(), emitBytes));
    body_received_ += emitBytes;
    buffer_.erase(0, emitBytes);

    if (body_received_ == content_length_) {
        return complete();
    }
    return needMore();
}

ParseResult ResponseStreamParser::parse_chunked_body(ResponseStreamSink& sink)
{
    for (;;) {
        if (expect_chunk_data_crlf_) {
            if (buffer_.size() < 2) {
                return needMore();
            }
            if (buffer_[0] != '\r' || buffer_[1] != '\n') {
                return fail(DiagnosticCode::invalid_chunk_size);
            }
            buffer_.erase(0, 2);
            expect_chunk_data_crlf_ = false;
        }

        if (chunk_remaining_ == 0) {
            const auto lineEnd = buffer_.find("\r\n");
            if (lineEnd == npos) {
                if (buffer_.size() > limits_.max_header_bytes) {
                    return fail(DiagnosticCode::invalid_chunk_size);
                }
                return needMore();
            }
            const auto sizeText = StringView(buffer_.data(), lineEnd);
            std::size_t chunkSize = 0;
            if (!parseChunkSizeLine(sizeText, chunkSize)) {
                return fail(DiagnosticCode::invalid_chunk_size);
            }
            buffer_.erase(0, lineEnd + 2);
            if (chunkSize == 0) {
                if (buffer_.size() < 2) {
                    return needMore();
                }
                if (buffer_[0] != '\r' || buffer_[1] != '\n') {
                    return fail(DiagnosticCode::invalid_chunk_size);
                }
                buffer_.erase(0, 2);
                return complete();
            }
            if (body_received_ > limits_.max_body_bytes
                || chunkSize > limits_.max_body_bytes - body_received_) {
                return fail(DiagnosticCode::body_too_large);
            }
            chunk_remaining_ = chunkSize;
        }

        if (buffer_.empty()) {
            return needMore();
        }
        const auto emitBytes =
            buffer_.size() < chunk_remaining_ ? buffer_.size() : chunk_remaining_;
        sink.on_body(StringView(buffer_.data(), emitBytes));
        buffer_.erase(0, emitBytes);
        body_received_ += emitBytes;
        chunk_remaining_ -= emitBytes;
        if (chunk_remaining_ == 0) {
            expect_chunk_data_crlf_ = true;
        }
    }
}

ParseResult ResponseStreamParser::complete() noexcept
{
    status_ = ParseStatus::done;
    diagnostic_ = DiagnosticCode::none;
    return done();
}

ParseResult ResponseStreamParser::fail(DiagnosticCode diagnostic) noexcept
{
    status_ = ParseStatus::failed;
    diagnostic_ = diagnostic;
    return failed(diagnostic);
}

} // namespace arp::network::http
