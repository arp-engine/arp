#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/native_backend_capabilities.h"

namespace arp::rhi::semantics {

// --- NativeEngineFallbackBridge ---

class NativeEngineFallbackBridge {
public:
    explicit NativeEngineFallbackBridge(NativeBackendCapabilityContract contract);

    rhi::EngineFallbackPlan buildPlan(const rhi::EngineFeatureRequirementSet& requirements) const;

private:
    NativeBackendCapabilityContract contract_;
};

rhi::EngineFallbackPlan buildNativeEngineFallbackPlan(
    const NativeBackendCapabilityContract& contract,
    const rhi::EngineFeatureRequirementSet& requirements);

} // namespace arp::rhi::semantics
