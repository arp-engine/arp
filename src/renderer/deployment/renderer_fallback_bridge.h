#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"

namespace arp::renderer::deployment {

// --- NativeRendererFallbackBridge ---

class NativeRendererFallbackBridge {
public:
    explicit NativeRendererFallbackBridge(rhi::semantics::NativeBackendCapabilityContract contract);

    rhi::EngineFallbackPlan buildPlan(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const;

private:
    rhi::semantics::NativeBackendCapabilityContract contract_;
};

rhi::EngineFallbackPlan buildNativeRendererFallbackPlan(
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan);

} // namespace arp::renderer::deployment
