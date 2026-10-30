#include "network/http/uri.h"

#include <cstddef>

namespace arp::network::http {

StringView diagnosticName(DiagnosticCode code) noexcept
{
#define ARP_HTTP_DIAGNOSTIC(name)                                                                  \
    case DiagnosticCode::name:                                                                     \
        return #name
    switch (code) {
        ARP_HTTP_DIAGNOSTIC(none);
        ARP_HTTP_DIAGNOSTIC(malformed_uri);
        ARP_HTTP_DIAGNOSTIC(unsupported_scheme);
        ARP_HTTP_DIAGNOSTIC(missing_host);
        ARP_HTTP_DIAGNOSTIC(invalid_port);
        ARP_HTTP_DIAGNOSTIC(invalid_percent_encoding);
        ARP_HTTP_DIAGNOSTIC(invalid_header_name);
        ARP_HTTP_DIAGNOSTIC(invalid_header_value);
        ARP_HTTP_DIAGNOSTIC(invalid_method);
        ARP_HTTP_DIAGNOSTIC(invalid_status_code);
        ARP_HTTP_DIAGNOSTIC(header_too_large);
        ARP_HTTP_DIAGNOSTIC(too_many_headers);
        ARP_HTTP_DIAGNOSTIC(body_too_large);
        ARP_HTTP_DIAGNOSTIC(malformed_start_line);
        ARP_HTTP_DIAGNOSTIC(malformed_header);
        ARP_HTTP_DIAGNOSTIC(invalid_content_length);
        ARP_HTTP_DIAGNOSTIC(conflicting_content_length);
        ARP_HTTP_DIAGNOSTIC(unsupported_transfer_encoding);
        ARP_HTTP_DIAGNOSTIC(invalid_chunk_size);
        ARP_HTTP_DIAGNOSTIC(connection_closed);
        ARP_HTTP_DIAGNOSTIC(tls_unavailable);
        ARP_HTTP_DIAGNOSTIC(invalid_route);
        ARP_HTTP_DIAGNOSTIC(duplicate_route);
        ARP_HTTP_DIAGNOSTIC(route_limit);
        ARP_HTTP_DIAGNOSTIC(routes_frozen);
        ARP_HTTP_DIAGNOSTIC(invalid_request_target);
        ARP_HTTP_DIAGNOSTIC(ambiguous_framing);
        ARP_HTTP_DIAGNOSTIC(buffer_too_large);
        ARP_HTTP_DIAGNOSTIC(unsupported_http_version);
        ARP_HTTP_DIAGNOSTIC(invalid_host_header);
        ARP_HTTP_DIAGNOSTIC(expectation_not_supported);
        ARP_HTTP_DIAGNOSTIC(server_stopped);
        ARP_HTTP_DIAGNOSTIC(server_busy);
        ARP_HTTP_DIAGNOSTIC(invalid_server_config);
        ARP_HTTP_DIAGNOSTIC(request_expired);
        ARP_HTTP_DIAGNOSTIC(response_already_sent);
        ARP_HTTP_DIAGNOSTIC(unsupported_platform);
        ARP_HTTP_DIAGNOSTIC(invalid_response);
        ARP_HTTP_DIAGNOSTIC(response_aborted);
    }
#undef ARP_HTTP_DIAGNOSTIC
    return "unknown_http_diagnostic";
}

namespace {

constexpr std::size_t npos = StringView::npos;

bool isHex(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

unsigned hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned>(c - 'a' + 10);
    }
    return static_cast<unsigned>(c - 'A' + 10);
}

char toLowerAscii(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

bool isSupportedScheme(StringView scheme) noexcept
{
    return scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss";
}

uint16 defaultPort(StringView scheme) noexcept
{
    if (scheme == "https" || scheme == "wss") {
        return 443;
    }
    return 80;
}

std::size_t firstOf(StringView text, StringView chars, std::size_t start) noexcept
{
    for (std::size_t i = start; i < text.size(); ++i) {
        for (char c : chars) {
            if (text[i] == c) {
                return i;
            }
        }
    }
    return npos;
}

DiagnosticCode assignLower(core::pmr::String& out, StringView text)
{
    out.clear();
    out.reserve(text.size());
    for (char c : text) {
        out.push_back(toLowerAscii(c));
    }
    return DiagnosticCode::none;
}

DiagnosticCode assignDecoded(core::pmr::String& out, StringView text)
{
    out.clear();
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size() || !isHex(text[i + 1]) || !isHex(text[i + 2])) {
            return DiagnosticCode::invalid_percent_encoding;
        }
        const auto value = static_cast<char>((hexValue(text[i + 1]) << 4) | hexValue(text[i + 2]));
        out.push_back(value);
        i += 2;
    }
    return DiagnosticCode::none;
}

StringView slice(StringView text, std::size_t begin, std::size_t end) noexcept
{
    return text.substr(begin, end == npos ? npos : end - begin);
}

DiagnosticCode parsePort(StringView text, uint16& port)
{
    if (text.empty()) {
        return DiagnosticCode::invalid_port;
    }
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return DiagnosticCode::invalid_port;
        }
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > 65535u) {
            return DiagnosticCode::invalid_port;
        }
    }
    port = static_cast<uint16>(value);
    return DiagnosticCode::none;
}

UriParseResult failed(DiagnosticCode diagnostic)
{
    UriParseResult result;
    result.diagnostic = diagnostic;
    return result;
}

} // namespace

Uri::Uri()
    : scheme_(&core::defaultCpuMemoryResource()), host_(&core::defaultCpuMemoryResource()),
      path_(&core::defaultCpuMemoryResource()), query_(&core::defaultCpuMemoryResource()),
      fragment_(&core::defaultCpuMemoryResource()),
      request_target_(&core::defaultCpuMemoryResource())
{}

UriParseResult Uri::parse(StringView text)
{
    if (text.empty()) {
        return failed(DiagnosticCode::malformed_uri);
    }

    UriParseResult result;
    auto& uri = result.uri;
    const auto schemeMark = text.find("://");

    if (schemeMark == npos) {
        if (text.front() != '/') {
            return failed(DiagnosticCode::malformed_uri);
        }
        const auto diagnostic = uri.assign_path_query_fragment(text);
        if (diagnostic != DiagnosticCode::none) {
            return failed(diagnostic);
        }
        return result;
    }

    assignLower(uri.scheme_, text.substr(0, schemeMark));
    if (!isSupportedScheme(uri.scheme())) {
        return failed(DiagnosticCode::unsupported_scheme);
    }

    uri.absolute_ = true;
    uri.port_ = defaultPort(uri.scheme());
    std::size_t cursor = schemeMark + 3;
    if (cursor >= text.size()) {
        return failed(DiagnosticCode::missing_host);
    }

    const auto authorityEnd = firstOf(text, "/?#", cursor);
    const auto authority = slice(text, cursor, authorityEnd);
    if (authority.empty()) {
        return failed(DiagnosticCode::missing_host);
    }

    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == npos || close == 1) {
            return failed(DiagnosticCode::missing_host);
        }
        uri.host_.assign(authority.data() + 1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                return failed(DiagnosticCode::malformed_uri);
            }
            uri.explicit_port_ = true;
            const auto diagnostic = parsePort(authority.substr(close + 2), uri.port_);
            if (diagnostic != DiagnosticCode::none) {
                return failed(diagnostic);
            }
        }
    } else {
        const auto portMark = authority.find(':');
        const auto host = slice(authority, 0, portMark);
        if (host.empty()) {
            return failed(DiagnosticCode::missing_host);
        }
        uri.host_.assign(host.data(), host.size());
        if (portMark != npos) {
            uri.explicit_port_ = true;
            const auto diagnostic = parsePort(authority.substr(portMark + 1), uri.port_);
            if (diagnostic != DiagnosticCode::none) {
                return failed(diagnostic);
            }
        }
    }

    const auto suffix = authorityEnd == npos ? StringView() : text.substr(authorityEnd);
    const auto diagnostic = uri.assign_path_query_fragment(suffix);
    if (diagnostic != DiagnosticCode::none) {
        return failed(diagnostic);
    }
    return result;
}

DiagnosticCode Uri::assign_path_query_fragment(StringView suffix)
{
    const auto fragmentMark = suffix.find('#');
    const auto beforeFragment = slice(suffix, 0, fragmentMark);
    const auto queryMark = beforeFragment.find('?');

    auto pathView = slice(beforeFragment, 0, queryMark);
    if (pathView.empty() && is_absolute()) {
        pathView = "/";
    }
    auto diagnostic = assignDecoded(path_, pathView);
    if (diagnostic != DiagnosticCode::none) {
        return diagnostic;
    }

    if (queryMark != npos) {
        diagnostic = assignDecoded(query_, beforeFragment.substr(queryMark + 1));
        if (diagnostic != DiagnosticCode::none) {
            return diagnostic;
        }
    } else {
        query_.clear();
    }

    if (fragmentMark != npos) {
        diagnostic = assignDecoded(fragment_, suffix.substr(fragmentMark + 1));
        if (diagnostic != DiagnosticCode::none) {
            return diagnostic;
        }
    } else {
        fragment_.clear();
    }
    const auto encodedPath = slice(beforeFragment, 0, queryMark);
    request_target_.assign(encodedPath.empty() ? StringView("/") : encodedPath);
    if (queryMark != npos) {
        request_target_ += '?';
        const auto encodedQuery = beforeFragment.substr(queryMark + 1);
        request_target_.append(encodedQuery.data(), encodedQuery.size());
    }
    return DiagnosticCode::none;
}

StringView Uri::request_target() const noexcept
{
    return request_target_;
}

bool Uri::is_absolute() const noexcept
{
    return absolute_;
}

bool Uri::has_explicit_port() const noexcept
{
    return explicit_port_;
}

StringView Uri::scheme() const noexcept
{
    return StringView(scheme_.data(), scheme_.size());
}

StringView Uri::host() const noexcept
{
    return StringView(host_.data(), host_.size());
}

uint16 Uri::port() const noexcept
{
    return port_;
}

StringView Uri::path() const noexcept
{
    return StringView(path_.data(), path_.size());
}

StringView Uri::query() const noexcept
{
    return StringView(query_.data(), query_.size());
}

StringView Uri::fragment() const noexcept
{
    return StringView(fragment_.data(), fragment_.size());
}

bool UriParseResult::success() const noexcept
{
    return diagnostic == DiagnosticCode::none;
}

} // namespace arp::network::http
