#include "network/http/serializer.h"

#include <cstddef>
#include <cstdint>

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

void appendHex(core::pmr::String& out, std::size_t value)
{
    constexpr char hex[] = "0123456789abcdef";
    char digits[32]{};
    std::size_t count = 0;
    do {
        digits[count++] = hex[value & 0x0f];
        value >>= 4;
    } while (value > 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
}

bool asciiEqualInsensitive(StringView left, StringView right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        char l = left[i];
        char r = right[i];
        if (l >= 'A' && l <= 'Z') {
            l = static_cast<char>(l - 'A' + 'a');
        }
        if (r >= 'A' && r <= 'Z') {
            r = static_cast<char>(r - 'A' + 'a');
        }
        if (l != r) {
            return false;
        }
    }
    return true;
}

void appendView(core::pmr::String& out, StringView value)
{
    out.append(value.data(), value.size());
}

void appendVersion(core::pmr::String& out, Version version)
{
    out += "HTTP/";
    appendUnsigned(out, version.major);
    out.push_back('.');
    appendUnsigned(out, version.minor);
}

void appendBody(core::pmr::String& out, const byte_buffer& body)
{
    for (auto byte : body) {
        out.push_back(static_cast<char>(byte));
    }
}

void appendHeaders(core::pmr::String& out, const HeaderList& headers, bool closeConnection = false)
{
    for (std::size_t i = 0; i < headers.size(); ++i) {
        const auto& header = headers.at(i);
        if (asciiEqualInsensitive(header.name(), "Content-Length")
            || asciiEqualInsensitive(header.name(), "Transfer-Encoding")
            || (closeConnection && asciiEqualInsensitive(header.name(), "Connection"))) {
            continue;
        }
        appendView(out, header.name());
        out += ": ";
        appendView(out, header.value());
        out += "\r\n";
    }
}

void appendContentLength(core::pmr::String& out, std::size_t length)
{
    out += "Content-Length: ";
    appendUnsigned(out, length);
    out += "\r\n\r\n";
}

void appendChunked(core::pmr::String& out, const byte_buffer& body)
{
    out += "Transfer-Encoding: chunked\r\n\r\n";
    if (!body.empty()) {
        appendHex(out, body.size());
        out += "\r\n";
        appendBody(out, body);
        out += "\r\n";
    }
    out += "0\r\n\r\n";
}

} // namespace

bool SerializeResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

SerializeResult Serializer::serializeRequest(
    const Request& request, const SerializationOptions& options)
{
    SerializeResult result;
    appendView(result.text, methodText(request.method()));
    result.text.push_back(' ');
    appendView(result.text, request.target());
    result.text.push_back(' ');
    appendVersion(result.text, request.version());
    result.text += "\r\n";
    appendHeaders(result.text, request.headers());
    if (options.transfer_mode == BodyTransferMode::chunked) {
        appendChunked(result.text, request.body());
    } else {
        appendContentLength(result.text, request.body().size());
        appendBody(result.text, request.body());
    }
    return result;
}

SerializeResult Serializer::serializeResponse(
    const Response& response, const SerializationOptions& options)
{
    SerializeResult result;
    if (!StatusCode::parse(response.status().value()).success()
        || validateHeaderValue(response.reason()) != DiagnosticCode::none) {
        result.diagnostic = DiagnosticCode::invalid_response;
        return result;
    }
    appendVersion(result.text, response.version());
    result.text.push_back(' ');
    appendUnsigned(result.text, response.status().value());
    result.text.push_back(' ');
    appendView(result.text, response.reason());
    result.text += "\r\n";
    appendHeaders(result.text, response.headers(), options.close_connection);
    if (options.close_connection)
        result.text += "Connection: close\r\n";
    const auto status = response.status().value();
    if (status < 200 || status == 204) {
        result.text += "\r\n";
        return result;
    }
    if (options.head_response || status == 304) {
        appendContentLength(result.text, response.body().size());
        return result;
    }
    if (options.transfer_mode == BodyTransferMode::chunked) {
        appendChunked(result.text, response.body());
    } else {
        appendContentLength(result.text, response.body().size());
        appendBody(result.text, response.body());
    }
    return result;
}

} // namespace arp::network::http
