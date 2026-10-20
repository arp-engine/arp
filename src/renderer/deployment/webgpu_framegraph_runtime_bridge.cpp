#include "renderer/deployment/webgpu_framegraph_runtime_bridge.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

#include <utility>

namespace arp::renderer::deployment {

WebGpuFrameGraphRuntimeBridge::WebGpuFrameGraphRuntimeBridge(rhi::WebGpuRuntimeProfile profile)
    : profile_(std::move(profile))
{}

rhi::EngineFallbackPlan WebGpuFrameGraphRuntimeBridge::buildPlan(
    const render_graph::RhiFramePlan& framePlan) const
{
    return buildWebGpuFrameGraphRuntimeFallbackPlan(profile_, framePlan);
}

rhi::EngineFallbackPlan buildWebGpuFrameGraphRuntimeFallbackPlan(
    const rhi::WebGpuRuntimeProfile& profile, const render_graph::RhiFramePlan& framePlan)
{
    const auto capabilityBridge = rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);
    auto plan = rhi::semantics::buildNativeEngineFallbackPlan(
        capabilityBridge.contract, framePlan.featureRequirements);

    if (!capabilityBridge.success) {
        plan.success = false;
        ++plan.diagnosticFailureCount;
        plan.telemetryRequired = true;
        arp::core::appendDiagnostic(plan.diagnostics, capabilityBridge.diagnostics);
    }

    return plan;
}

WebGpuFrameGraphDeploymentBridge::WebGpuFrameGraphDeploymentBridge(
    rhi::WebGpuRuntimeProfile profile)
    : profile_(std::move(profile))
{}

WebGpuDeploymentReadinessReport WebGpuFrameGraphDeploymentBridge::evaluate(
    const render_graph::RhiFramePlan& framePlan,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event,
    std::uint32_t maxTelemetryDiagnostics) const
{
    return buildWebGpuFrameGraphDeploymentReadiness(
        profile_, framePlan, state, event, maxTelemetryDiagnostics);
}

WebGpuDeploymentReadinessReport buildWebGpuFrameGraphDeploymentReadiness(
    const rhi::WebGpuRuntimeProfile& profile,
    const render_graph::RhiFramePlan& framePlan,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event,
    std::uint32_t maxTelemetryDiagnostics)
{
    const auto fallbackPlan = buildWebGpuFrameGraphRuntimeFallbackPlan(profile, framePlan);
    const auto lifecycleDecision =
        rhi::semantics::lowerWebGpuCanvasLifecycleEvent(profile, state, event);

    rhi::semantics::WebGpuRuntimeTelemetryCollector collector({maxTelemetryDiagnostics});
    collector.recordProfile(profile);
    collector.recordFallbackPlan(fallbackPlan);
    collector.recordLifecycleDecision(lifecycleDecision);
    return evaluateWebGpuDeploymentReadiness(collector.buildReport());
}

} // namespace arp::renderer::deployment
