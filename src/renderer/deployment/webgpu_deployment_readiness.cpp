#include "renderer/deployment/webgpu_deployment_readiness.h"

#include "core/diagnostic_builder.h"
#include "renderer/deployment/renderer_deployment_evidence_admission.h"

namespace arp::renderer::deployment {

WebGpuDeploymentReadinessReport WebGpuDeploymentReadinessGate::evaluate(
    const rhi::semantics::WebGpuRuntimeTelemetryReport& telemetry) const
{
    return evaluateWebGpuDeploymentReadiness(telemetry);
}

WebGpuDeploymentReadinessReport evaluateWebGpuDeploymentReadiness(
    const rhi::semantics::WebGpuRuntimeTelemetryReport& telemetry)
{
    WebGpuDeploymentReadinessReport readiness;
    readiness.backend = telemetry.backend;
    readiness.status = WebGpuDeploymentReadinessStatus::DiagnosticFailure;
    readiness.telemetryRequired = true;
    readiness.swapchainRebuildRequired = telemetry.swapchainRebuildRequired;
    readiness.releaseSurfaceResources = telemetry.releaseSurfaceResources;
    readiness.releaseTransientResources = telemetry.releaseTransientResources;
    readiness.memoryBudgetRefreshRequired = telemetry.memoryBudgetRefreshRequired;
    readiness.backendFallbacksHiddenFromUpperLayer = telemetry.backendFallbacksHiddenFromUpperLayer;
    readiness.nativeHandleExposedToUpperLayer = telemetry.nativeHandleExposedToUpperLayer;
    readiness.cpuReplayFeatureCount = telemetry.cpuReplayFeatureCount;
    readiness.degradedFeatureCount = telemetry.degradedFeatureCount;
    readiness.diagnosticFailureCount = telemetry.diagnosticFailureCount;
    readiness.diagnostics = telemetry.diagnostics;

    if (telemetry.backend != rhi::Backend::WebGPU) {
        arp::core::appendDiagnostic(
            readiness.diagnostics, "WebGPU deployment readiness requires a WebGPU backend report");
        ++readiness.diagnosticFailureCount;
    }
    if (!telemetry.browserRuntime) {
        arp::core::appendDiagnostic(
            readiness.diagnostics, "WebGPU deployment readiness requires a browser runtime");
        ++readiness.diagnosticFailureCount;
    }
    if (!telemetry.canvasSurface) {
        arp::core::appendDiagnostic(
            readiness.diagnostics, "WebGPU deployment readiness requires a canvas surface");
        ++readiness.diagnosticFailureCount;
    }
    if (telemetry.logicalFrameSlots != 2 || telemetry.requestedSwapchainImageCount != 2) {
        arp::core::appendDiagnostic(
            readiness.diagnostics, "WebGPU deployment readiness requires double buffering");
        ++readiness.diagnosticFailureCount;
    }
    if (!telemetry.backendFallbacksHiddenFromUpperLayer) {
        arp::core::appendDiagnostic(readiness.diagnostics,
            "WebGPU deployment readiness rejected visible backend fallbacks");
        ++readiness.diagnosticFailureCount;
    }
    if (telemetry.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            readiness.diagnostics, "WebGPU deployment readiness rejected native handle exposure");
        ++readiness.diagnosticFailureCount;
    }

    readiness.success = false;
    readiness.canStartFrame = false;
    readiness.contractComplete = false;
    readiness.platformValidated = false;
    if (readiness.diagnosticFailureCount == 0u)
        readiness.diagnosticFailureCount = 1u;
    arp::core::appendDiagnostic(readiness.diagnostics,
        RendererDeploymentEvidenceAdmission::reject(
            telemetry.backend, RendererDeploymentEvidenceStage::WebGpuReport));

    return readiness;
}

} // namespace arp::renderer::deployment
