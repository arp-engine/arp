#include "network/http/header.h"

#include "core/string_utils.h"

namespace arp::network::http {

namespace {

bool isTokenChar(char c) noexcept
{
    if (c <= 32 || c >= 127) {
        return false;
    }
    switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ';':
    case ':':
    case '\\':
    case '"':
    case '/':
    case '[':
    case ']':
    case '?':
    case '=':
    case '{':
    case '}':
        return false;
    default:
        return true;
    }
}

} // namespace

Header::Header()
    : name_(&core::defaultCpuMemoryResource()), value_(&core::defaultCpuMemoryResource())
{}

Header::Header(StringView name, StringView value)
    : name_(name, &core::defaultCpuMemoryResource()),
      value_(value, &core::defaultCpuMemoryResource())
{}

StringView Header::name() const noexcept
{
    return StringView(name_.data(), name_.size());
}

StringView Header::value() const noexcept
{
    return StringView(value_.data(), value_.size());
}

HeaderList::HeaderList() : headers_(&core::defaultCpuMemoryResource())
{}

DiagnosticCode HeaderList::add(StringView name, StringView value)
{
    auto diagnostic = validateHeaderName(name);
    if (diagnostic != DiagnosticCode::none) {
        return diagnostic;
    }
    diagnostic = validateHeaderValue(value);
    if (diagnostic != DiagnosticCode::none) {
        return diagnostic;
    }
    headers_.emplace_back(name, value);
    return DiagnosticCode::none;
}

std::size_t HeaderList::size() const noexcept
{
    return headers_.size();
}

std::size_t HeaderList::count(StringView name) const noexcept
{
    std::size_t total = 0;
    for (const auto& header : headers_) {
        if (core::asciiEqualsIgnoreCase(header.name(), name)) {
            ++total;
        }
    }
    return total;
}

StringView HeaderList::firstValue(StringView name) const noexcept
{
    for (const auto& header : headers_) {
        if (core::asciiEqualsIgnoreCase(header.name(), name)) {
            return header.value();
        }
    }
    return StringView();
}

const Header& HeaderList::at(std::size_t index) const noexcept
{
    return headers_[index];
}

DiagnosticCode validateHeaderName(StringView name) noexcept
{
    if (name.empty()) {
        return DiagnosticCode::invalid_header_name;
    }
    for (char c : name) {
        if (!isTokenChar(c)) {
            return DiagnosticCode::invalid_header_name;
        }
    }
    return DiagnosticCode::none;
}

DiagnosticCode validateHeaderValue(StringView value) noexcept
{
    for (char c : value) {
        if (c == '\r' || c == '\n') {
            return DiagnosticCode::invalid_header_value;
        }
    }
    return DiagnosticCode::none;
}

} // namespace arp::network::http
