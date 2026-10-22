#pragma once

#include "core/memory/budget_vector_preflight.h"

#include <array>
#include <cstdint>

namespace arp::rhi::semantics {

// Bytes and counts remain separate dimensions; none measures driver memory.
struct WebGlMemoryCounters {
    std::uint64_t cpuMetadataBytes = 0u;
    std::uint64_t cpuUploadBytes = 0u;
    std::uint64_t cpuReloadBytes = 0u;
    std::uint64_t logicalBufferBytes = 0u;
    std::uint64_t logicalTextureBytes = 0u;
    std::uint64_t objectCount = 0u;
    std::uint64_t commandCount = 0u;
    std::uint64_t referenceCount = 0u;
};

struct WebGlMemoryBudget {
    std::uint32_t contractVersion = 0u;
    WebGlMemoryCounters capacity;
};

struct WebGlMemoryRequest {
    std::uint32_t contractVersion = 0u;
    WebGlMemoryCounters addition;
    bool requiresNativePhysicalBound = false;
};

class WebGlMemoryBudgetContract final {
public:
    // Pure preflight; caller-provided live values are not ownership evidence.
    // P2/P3 must use the original registry and fixed policy before reservation.
    static const char* assess(const WebGlMemoryBudget& budget,
        const WebGlMemoryCounters& live,
        const WebGlMemoryRequest& request) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGL1-MEMORY-BUDGET-VERSION";
        if (request.contractVersion != 1u)
            return "RHI-WEBGL1-MEMORY-REQUEST-VERSION";
        if (request.requiresNativePhysicalBound)
            return "RHI-WEBGL1-MEMORY-PHYSICAL-BOUND-UNAVAILABLE";
        constexpr std::array fields{&WebGlMemoryCounters::cpuMetadataBytes,
            &WebGlMemoryCounters::cpuUploadBytes,
            &WebGlMemoryCounters::cpuReloadBytes,
            &WebGlMemoryCounters::logicalBufferBytes,
            &WebGlMemoryCounters::logicalTextureBytes,
            &WebGlMemoryCounters::objectCount,
            &WebGlMemoryCounters::commandCount,
            &WebGlMemoryCounters::referenceCount};
        const auto result = core::BudgetVectorPreflight<WebGlMemoryCounters, fields.size()>::assess(
            budget.capacity, std::array{live}, request.addition, fields);
        switch (result.status) {
        case core::BudgetVectorStatus::Fits:
            return nullptr;
        case core::BudgetVectorStatus::LiveExceedsCapacity:
            return "RHI-WEBGL1-MEMORY-LIVE-EXCEEDS-CAPACITY";
        case core::BudgetVectorStatus::LiveOverflow:
        case core::BudgetVectorStatus::Overflow:
            return "RHI-WEBGL1-MEMORY-OVERFLOW";
        case core::BudgetVectorStatus::Capacity:
            return "RHI-WEBGL1-MEMORY-CAPACITY";
        }
        return "RHI-WEBGL1-MEMORY-CAPACITY";
    }
};

} // namespace arp::rhi::semantics
