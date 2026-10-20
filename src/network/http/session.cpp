#include "network/http/session.h"

namespace arp::network::http {

bool SessionStatus::success() const noexcept
{
    return !transport_error && diagnostic == DiagnosticCode::none;
}

Session::Session()
    : request_parser_(config_.parser_limits), response_parser_(config_.parser_limits),
      response_stream_parser_(config_.parser_limits),
      read_buffer_(&core::defaultCpuMemoryResource())
{}

Session::Session(SessionConfig config)
    : config_(config), request_parser_(config_.parser_limits),
      response_parser_(config_.parser_limits), response_stream_parser_(config_.parser_limits),
      read_buffer_(&core::defaultCpuMemoryResource())
{}

const SessionConfig& Session::config() const noexcept
{
    return config_;
}

} // namespace arp::network::http
