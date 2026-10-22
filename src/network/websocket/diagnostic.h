#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "network/compat.h"

namespace arp::network::websocket {

enum class DiagnosticCode {
    none,
    missing_header,
    unsupported_version,
    missing_key,
    invalid_key,
    accept_mismatch,
    invalid_status,
    invalid_upgrade,
    invalid_connection,
    unsupported_subprotocol,
    invalid_opcode,
    invalid_reserved_bits,
    fragmented_control_frame,
    oversized_payload,
    incomplete_frame,
    invalid_close_payload,
    continuation_without_message,
    invalid_fragment_start,
};

} // namespace arp::network::websocket
