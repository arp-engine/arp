#pragma once

#include "core/io/file_result.h"
#include "network/http/byte_read_transport.h"

namespace arp::core::io::detail {
inline FileIoStatus httpFileStatus(network::http::ByteReadStatus status) noexcept
{
    using network::http::ByteReadStatus;
    switch (status) {
    case ByteReadStatus::Accepted:
        return FileIoStatus::Accepted;
    case ByteReadStatus::Success:
        return FileIoStatus::Success;
    case ByteReadStatus::Unsupported:
        return FileIoStatus::Unsupported;
    case ByteReadStatus::Cancelled:
        return FileIoStatus::Cancelled;
    case ByteReadStatus::Timeout:
        return FileIoStatus::Timeout;
    case ByteReadStatus::BudgetExceeded:
        return FileIoStatus::BudgetExceeded;
    case ByteReadStatus::InvalidRequest:
        return FileIoStatus::InvalidRequest;
    case ByteReadStatus::ProtocolError:
        return FileIoStatus::IntegrityMismatch;
    case ByteReadStatus::TransportError:
        return FileIoStatus::BackendUnavailable;
    case ByteReadStatus::QueueFull:
        return FileIoStatus::QueueFull;
    }
    return FileIoStatus::BackendUnavailable;
}
} // namespace arp::core::io::detail
