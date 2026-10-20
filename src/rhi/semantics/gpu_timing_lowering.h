#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"

#include <cstdint>
#include <vector>

namespace arp::rhi::semantics {

struct GpuTimingScopeRequest {
    /// Opaque, non-zero caller-owned key. RHI preserves but never interprets
    /// this value, so each application can evolve its own profiling taxonomy.
    std::uint64_t scopeId = 0;
    std::pmr::string label;
    bool required = false;
};

enum class GpuTimingLoweringOutcome : std::uint8_t {
    Native,
    FeatureNotEnabled,
    Unsupported,
};

/// L2 plan: query slots, frame serial latency and result ownership. Backends
/// receive only this plan and lower it to native APIs internally.
struct GpuTimingScopeSlot {
    std::uint64_t scopeId = 0;
    std::uint32_t beginQuery = 0;
    std::uint32_t endQuery = 0;
};

struct GpuTimingLoweringPlan {
    std::uint32_t contractVersion = 1;
    rhi::Backend backend = rhi::Backend::Null;
    GpuTimingLoweringOutcome outcome = GpuTimingLoweringOutcome::Unsupported;
    std::uint32_t queryCount = 0;
    std::uint32_t readbackLatencyFrames = 2;
    bool backendOwnsQueryLifetime = true;
    bool backendOwnsReadback = true;
    std::pmr::vector<GpuTimingScopeRequest> scopes;
    std::pmr::vector<GpuTimingScopeSlot> slots;
    std::pmr::string diagnosticId;
};

GpuTimingLoweringPlan lowerGpuTimingScopes(rhi::Backend backend,
    bool timestampFeatureEnabled,
    const std::pmr::vector<GpuTimingScopeRequest>& scopes);

bool gpuTimingPlanValid(const GpuTimingLoweringPlan& plan);

} // namespace arp::rhi::semantics
