#include "network/http/message.h"

namespace arp::network::http {

namespace {

bool isMethodToken(StringView text) noexcept
{
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < 'A' || c > 'Z') {
            return false;
        }
    }
    return true;
}

MethodParseResult methodResult(Method method) noexcept
{
    MethodParseResult result;
    result.method = method;
    return result;
}

MethodParseResult invalidMethod() noexcept
{
    MethodParseResult result;
    result.diagnostic = DiagnosticCode::invalid_method;
    return result;
}

byte_buffer bodyFromText(StringView text)
{
    byte_buffer body{&core::defaultCpuMemoryResource()};
    body.reserve(text.size());
    for (char c : text) {
        body.push_back(static_cast<std::uint8_t>(c));
    }
    return body;
}

StringView bodyAsText(const byte_buffer& body) noexcept
{
    return StringView(reinterpret_cast<const char*>(body.data()), body.size());
}

} // namespace

bool MethodParseResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

StatusCode::StatusCode(std::uint16_t value) noexcept : value_(value)
{}

StatusCodeParseResult StatusCode::parse(std::uint16_t value) noexcept
{
    StatusCodeParseResult result;
    if (value < 100 || value > 599) {
        result.diagnostic = DiagnosticCode::invalid_status_code;
        return result;
    }
    result.status = StatusCode(value);
    return result;
}

std::uint16_t StatusCode::value() const noexcept
{
    return value_;
}

bool StatusCodeParseResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

Request::Request()
    : target_(&core::defaultCpuMemoryResource()), body_(&core::defaultCpuMemoryResource())
{}

Method Request::method() const noexcept
{
    return method_;
}

void Request::set_method(Method method) noexcept
{
    method_ = method;
}

StringView Request::target() const noexcept
{
    return StringView(target_.data(), target_.size());
}

void Request::set_target(StringView target)
{
    target_.assign(target.data(), target.size());
}

Version Request::version() const noexcept
{
    return version_;
}

void Request::set_version(Version version) noexcept
{
    version_ = version;
}

HeaderList& Request::headers() noexcept
{
    return headers_;
}

const HeaderList& Request::headers() const noexcept
{
    return headers_;
}

DiagnosticCode Request::add_header(StringView name, StringView value)
{
    return headers_.add(name, value);
}

StringView Request::header(StringView name) const noexcept
{
    return headers_.firstValue(name);
}

DiagnosticCode Request::set_content_type(ContentType type)
{
    return add_header("Content-Type", contentTypeText(type));
}

const byte_buffer& Request::body() const noexcept
{
    return body_;
}

void Request::set_body(const byte_buffer& body)
{
    body_ = body;
}

void Request::set_body_text(StringView text)
{
    body_ = bodyFromText(text);
}

StringView Request::body_text() const noexcept
{
    return bodyAsText(body_);
}

Response::Response()
    : reason_(&core::defaultCpuMemoryResource()), body_(&core::defaultCpuMemoryResource())
{}

StatusCode Response::status() const noexcept
{
    return status_;
}

void Response::set_status(StatusCode status) noexcept
{
    status_ = status;
}

StringView Response::reason() const noexcept
{
    return StringView(reason_.data(), reason_.size());
}

void Response::set_reason(StringView reason)
{
    reason_.assign(reason.data(), reason.size());
}

Version Response::version() const noexcept
{
    return version_;
}

void Response::set_version(Version version) noexcept
{
    version_ = version;
}

HeaderList& Response::headers() noexcept
{
    return headers_;
}

const HeaderList& Response::headers() const noexcept
{
    return headers_;
}

DiagnosticCode Response::add_header(StringView name, StringView value)
{
    return headers_.add(name, value);
}

StringView Response::header(StringView name) const noexcept
{
    return headers_.firstValue(name);
}

DiagnosticCode Response::set_content_type(ContentType type)
{
    return add_header("Content-Type", contentTypeText(type));
}

const byte_buffer& Response::body() const noexcept
{
    return body_;
}

void Response::set_body(const byte_buffer& body)
{
    body_ = body;
}

void Response::set_body_text(StringView text)
{
    body_ = bodyFromText(text);
}

StringView Response::body_text() const noexcept
{
    return bodyAsText(body_);
}

MethodParseResult parseMethod(StringView text) noexcept
{
    if (!isMethodToken(text)) {
        return invalidMethod();
    }
    if (text == "GET") {
        return methodResult(Method::Get);
    }
    if (text == "POST") {
        return methodResult(Method::Post);
    }
    if (text == "PUT") {
        return methodResult(Method::Put);
    }
    if (text == "DELETE") {
        return methodResult(Method::Delete);
    }
    if (text == "PATCH") {
        return methodResult(Method::Patch);
    }
    if (text == "HEAD") {
        return methodResult(Method::Head);
    }
    if (text == "OPTIONS") {
        return methodResult(Method::Options);
    }
    if (text == "TRACE") {
        return methodResult(Method::Trace);
    }
    if (text == "CONNECT") {
        return methodResult(Method::Connect);
    }
    return invalidMethod();
}

StringView methodText(Method method) noexcept
{
    switch (method) {
    case Method::Get:
        return "GET";
    case Method::Post:
        return "POST";
    case Method::Put:
        return "PUT";
    case Method::Delete:
        return "DELETE";
    case Method::Patch:
        return "PATCH";
    case Method::Head:
        return "HEAD";
    case Method::Options:
        return "OPTIONS";
    case Method::Trace:
        return "TRACE";
    case Method::Connect:
        return "CONNECT";
    }
    return "";
}

StringView contentTypeText(ContentType type) noexcept
{
    switch (type) {
    case ContentType::Html:
        return "text/html";
    case ContentType::Plain:
        return "text/plain";
    case ContentType::Json:
        return "application/json";
    case ContentType::Xml:
        return "text/xml";
    }
    return "";
}

} // namespace arp::network::http
