#include "rhi/semantics/gpu_timing_lowering.h"

namespace arp::rhi::semantics {

GpuTimingLoweringPlan lowerGpuTimingScopes(const rhi::Backend backend,
    const bool timestampFeatureEnabled,
    const std::pmr::vector<GpuTimingScopeRequest>& scopes)
{
    GpuTimingLoweringPlan plan;
    plan.backend = backend;
    plan.scopes = scopes;
    if (scopes.empty()) {
        plan.diagnosticId = "gpu-timing-not-requested";
        return plan;
    }
    switch (backend) {
    case rhi::Backend::Null:
        plan.diagnosticId = "gpu-timing-null-unsupported";
        return plan;
    case rhi::Backend::WebGL:
        plan.diagnosticId = "RHI-WEBGL1-GPU-TIMING-UNSUPPORTED";
        return plan;
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
    case rhi::Backend::WebGPU:
        break;
    default:
        plan.diagnosticId = "gpu-timing-backend-unknown";
        return plan;
    }
    if (!timestampFeatureEnabled) {
        plan.outcome = GpuTimingLoweringOutcome::FeatureNotEnabled;
        plan.diagnosticId = "gpu-timing-feature-not-enabled";
        return plan;
    }
    if (scopes.size() > rhi::GpuTimingSample::kMaxScopes) {
        plan.diagnosticId = "gpu-timing-scope-capacity-exceeded";
        return plan;
    }
    for (std::size_t i = 0; i < scopes.size(); ++i) {
        if (scopes[i].scopeId == 0u) {
            plan.diagnosticId = "gpu-timing-invalid-scope-id";
            return plan;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (scopes[i].scopeId == scopes[j].scopeId) {
                plan.diagnosticId = "gpu-timing-duplicate-scope-id";
                return plan;
            }
        }
        plan.slots.push_back({scopes[i].scopeId,
            static_cast<std::uint32_t>(i * 2u),
            static_cast<std::uint32_t>(i * 2u + 1u)});
    }
    plan.outcome = GpuTimingLoweringOutcome::Native;
    plan.queryCount = static_cast<std::uint32_t>(scopes.size() * 2u);
    plan.diagnosticId = "gpu-timing-native-async-readback";
    return plan;
}

bool gpuTimingPlanValid(const GpuTimingLoweringPlan& plan)
{
    if (plan.outcome != GpuTimingLoweringOutcome::Native)
        return true;
    switch (plan.backend) {
    case rhi::Backend::Vulkan:
    case rhi::Backend::Metal:
    case rhi::Backend::WebGPU:
        break;
    case rhi::Backend::Null:
    case rhi::Backend::WebGL:
    default:
        return false;
    }
    return !plan.scopes.empty() && plan.slots.size() == plan.scopes.size()
        && plan.queryCount == plan.scopes.size() * 2u && plan.readbackLatencyFrames >= 1u
        && plan.backendOwnsQueryLifetime && plan.backendOwnsReadback;
}

} // namespace arp::rhi::semantics
