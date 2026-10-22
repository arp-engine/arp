#include "renderer/deployment/renderer_fallback_bridge.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

namespace arp::renderer::deployment {

namespace {

bool hasRendererBoundaryFailure(const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    return !rendererFramePlan.success || rendererFramePlan.backendSpecificBranchingRequired
        || rendererFramePlan.nativeHandleExposedToUpperLayer;
}

rhi::EngineFallbackPlan makeRendererDiagnosticFailure(
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    rhi::EngineFallbackPlan plan;
    plan.success = false;
    plan.backend = contract.backend;
    plan.logicalFrameSlots = rendererFramePlan.plan.featureRequirements.logicalFrameSlots == 0
        ? 2
        : rendererFramePlan.plan.featureRequirements.logicalFrameSlots;
    plan.diagnosticFailureCount = 1;
    plan.telemetryRequired = true;
    plan.upperLayerVisibleFallbacks = rendererFramePlan.backendSpecificBranchingRequired
        || rendererFramePlan.nativeHandleExposedToUpperLayer;
    plan.backendFallbacksHiddenFromUpperLayer = !rendererFramePlan.backendSpecificBranchingRequired
        && !rendererFramePlan.nativeHandleExposedToUpperLayer;

    arp::core::appendDiagnostic(plan.diagnostics, rendererFramePlan.diagnostics);
    if (rendererFramePlan.backendSpecificBranchingRequired) {
        arp::core::appendDiagnostic(
            plan.diagnostics, "renderer RHI native fallback rejected backend branching");
    }
    if (rendererFramePlan.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            plan.diagnostics, "renderer RHI native fallback rejected native handle exposure");
    }
    if (plan.diagnostics.empty()) {
        plan.diagnostics = "renderer RHI native fallback failed before backend lowering";
    }

    return plan;
}

} // namespace

NativeRendererFallbackBridge::NativeRendererFallbackBridge(
    rhi::semantics::NativeBackendCapabilityContract contract)
    : contract_(std::move(contract))
{}

rhi::EngineFallbackPlan NativeRendererFallbackBridge::buildPlan(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const
{
    return buildNativeRendererFallbackPlan(contract_, rendererFramePlan);
}

rhi::EngineFallbackPlan buildNativeRendererFallbackPlan(
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    if (hasRendererBoundaryFailure(rendererFramePlan)) {
        return makeRendererDiagnosticFailure(contract, rendererFramePlan);
    }

    return rhi::semantics::buildNativeEngineFallbackPlan(
        contract, rendererFramePlan.plan.featureRequirements);
}

} // namespace arp::renderer::deployment
