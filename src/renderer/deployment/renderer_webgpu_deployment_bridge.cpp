#include "renderer/deployment/renderer_webgpu_deployment_bridge.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "renderer/deployment/webgpu_framegraph_runtime_bridge.h"

#include <utility>

namespace arp::renderer::deployment {

namespace {

WebGpuDeploymentReadinessReport makeRendererDiagnosticFailure(
    const rhi::WebGpuRuntimeProfile& profile,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    WebGpuDeploymentReadinessReport report;
    report.success = false;
    report.backend = rhi::Backend::WebGPU;
    report.status = WebGpuDeploymentReadinessStatus::DiagnosticFailure;
    report.contractComplete = false;
    report.platformValidated = false;
    report.canStartFrame = false;
    report.telemetryRequired = true;
    report.backendFallbacksHiddenFromUpperLayer =
        !rendererFramePlan.backendSpecificBranchingRequired;
    report.nativeHandleExposedToUpperLayer = rendererFramePlan.nativeHandleExposedToUpperLayer;
    report.diagnosticFailureCount = 1;

    if (!rendererFramePlan.diagnostics.empty()) {
        arp::core::appendDiagnostic(report.diagnostics, rendererFramePlan.diagnostics);
    }
    if (rendererFramePlan.backendSpecificBranchingRequired) {
        arp::core::appendDiagnostic(
            report.diagnostics, "renderer RHI deployment rejected backend branching");
    }
    if (rendererFramePlan.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            report.diagnostics, "renderer RHI deployment rejected native handle exposure");
    }
    if (profile.status == rhi::WebGpuRuntimeProfileStatus::DiagnosticFailure) {
        arp::core::appendDiagnostic(report.diagnostics, profile.diagnostics);
    }
    if (report.diagnostics.empty()) {
        report.diagnostics = "renderer RHI deployment failed before backend lowering";
    }

    return report;
}

bool hasRendererBoundaryFailure(const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    return !rendererFramePlan.success || rendererFramePlan.backendSpecificBranchingRequired
        || rendererFramePlan.nativeHandleExposedToUpperLayer;
}

} // namespace

RendererWebGpuDeploymentBridge::RendererWebGpuDeploymentBridge(rhi::WebGpuRuntimeProfile profile)
    : profile_(std::move(profile))
{}

WebGpuDeploymentReadinessReport RendererWebGpuDeploymentBridge::evaluate(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event,
    std::uint32_t maxTelemetryDiagnostics) const
{
    return evaluateRendererWebGpuDeploymentReadiness(
        profile_, rendererFramePlan, state, event, maxTelemetryDiagnostics);
}

WebGpuDeploymentReadinessReport evaluateRendererWebGpuDeploymentReadiness(
    const rhi::WebGpuRuntimeProfile& profile,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event,
    std::uint32_t maxTelemetryDiagnostics)
{
    if (hasRendererBoundaryFailure(rendererFramePlan)) {
        return makeRendererDiagnosticFailure(profile, rendererFramePlan);
    }

    return buildWebGpuFrameGraphDeploymentReadiness(
        profile, rendererFramePlan.plan, state, event, maxTelemetryDiagnostics);
}

} // namespace arp::renderer::deployment
