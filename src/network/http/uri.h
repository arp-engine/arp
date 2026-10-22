#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "network/compat.h"

namespace arp::network::http {

enum class DiagnosticCode {
    none,
    malformed_uri,
    unsupported_scheme,
    missing_host,
    invalid_port,
    invalid_percent_encoding,
    invalid_header_name,
    invalid_header_value,
    invalid_method,
    invalid_status_code,
    header_too_large,
    too_many_headers,
    body_too_large,
    malformed_start_line,
    malformed_header,
    invalid_content_length,
    conflicting_content_length,
    unsupported_transfer_encoding,
    invalid_chunk_size,
    connection_closed,
    tls_unavailable,
    invalid_route,
    duplicate_route,
    route_limit,
    routes_frozen,
    invalid_request_target,
    ambiguous_framing,
    buffer_too_large,
    unsupported_http_version,
    invalid_host_header,
    expectation_not_supported,
    server_stopped,
    server_busy,
    invalid_server_config,
    request_expired,
    response_already_sent,
    unsupported_platform,
    invalid_response,
};

ARP_API StringView diagnosticName(DiagnosticCode code) noexcept;

class Uri;

struct UriParseResult;

class ARP_API Uri {
public:
    Uri();

    static UriParseResult parse(StringView text);

    bool is_absolute() const noexcept;
    bool has_explicit_port() const noexcept;
    StringView scheme() const noexcept;
    StringView host() const noexcept;
    uint16 port() const noexcept;
    StringView path() const noexcept;
    StringView query() const noexcept;
    StringView fragment() const noexcept;

private:
    friend struct UriParseResult;

    DiagnosticCode assign_path_query_fragment(StringView suffix);

    core::pmr::String scheme_;
    core::pmr::String host_;
    core::pmr::String path_;
    core::pmr::String query_;
    core::pmr::String fragment_;
    uint16 port_ = 0;
    bool absolute_ = false;
    bool explicit_port_ = false;
};

struct UriParseResult {
    Uri uri;
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

} // namespace arp::network::http
