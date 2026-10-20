#pragma once

#include <cstdint>

namespace arp::rhi {

struct BindlessBindingBudget {
    std::uint32_t maxBindings = 0;
    std::uint64_t maxResidentBytes = 0;
    // Reserved until final completion, including snapshot, source and arguments.
    std::uint64_t maxBindingBytes = 0;
    std::uint32_t maxArgumentBuffers = 0;
    std::uint64_t maxArgumentBufferBytes = 0;
};

using BindlessComputeBindingBudget = BindlessBindingBudget;
using BindlessGraphicsBindingBudget = BindlessBindingBudget;

} // namespace arp::rhi
