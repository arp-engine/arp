#pragma once

#include <cstddef>
#include <cstdint>

namespace arp::resource {

enum class ResourceGraphSubmitStatus : std::uint8_t {
    Accepted,
    InvalidRequest,
    NodeBudgetExceeded,
    FileReadBudgetExceeded,
    SourceByteBudgetExceeded,
    QueueFull,
    ServiceClosed,
    AllocationFailed,
};

struct ResourceGraphExecutionBudget {
    std::size_t maxNodes = 0u;
    std::size_t maxFileReads = 0u;   // Accepted reads across the whole graph.
    std::size_t maxSourceBytes = 0u; // Acquired bytes across the whole graph.
    std::size_t maxDecodedBytes = 0u;
    std::size_t completionDispatchBudget = 0u;
};

} // namespace arp::resource
