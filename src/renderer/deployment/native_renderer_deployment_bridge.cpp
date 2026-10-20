#include "renderer/deployment/native_renderer_deployment_bridge.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "renderer/deployment/renderer_deployment_evidence_admission.h"

#include <utility>

namespace arp::renderer::deployment {

namespace {

bool isNativeFirstContract(const rhi::semantics::NativeBackendCapabilityContract& contract)
{
    return contract.preservesNativePotential && !contract.flattenedToPortableSubset;
}

NativeRendererDeploymentReadinessReport makeReportFromPlan(
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const rhi::EngineFallbackPlan& fallbackPlan)
{
    NativeRendererDeploymentReadinessReport report;
    report.backend = contract.backend;
    report.telemetryRequired = fallbackPlan.telemetryRequired;
    report.nativeFeatureCount = fallbackPlan.nativeFeatureCount;
    report.emulatedFeatureCount = fallbackPlan.emulatedFeatureCount;
    report.cpuReplayFeatureCount = fallbackPlan.cpuReplayFeatureCount;
    report.degradedFeatureCount = fallbackPlan.degradedFeatureCount;
    report.diagnosticFailureCount = fallbackPlan.diagnosticFailureCount;
    report.upperLayerVisibleFallbacks = fallbackPlan.upperLayerVisibleFallbacks;
    report.backendFallbacksHiddenFromUpperLayer = fallbackPlan.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = rendererFramePlan.nativeHandleExposedToUpperLayer;
    report.diagnostics = fallbackPlan.diagnostics;

    if (!isNativeFirstContract(contract)) {
        ++report.diagnosticFailureCount;
        report.telemetryRequired = true;
        arp::core::appendDiagnostic(report.diagnostics,
            "native renderer deployment requires native-first backend capability contract");
    }

    // Structural fallback decisions and caller-set query flags are not native
    // workload evidence. Preserve diagnostics without issuing execution rights.
    report.success = false;
    report.contractComplete = false;
    report.backendValidated = false;
    report.canStartFrame = false;
    report.status = NativeRendererDeploymentReadinessStatus::DiagnosticFailure;
    report.telemetryRequired = true;
    if (report.diagnosticFailureCount == 0u)
        report.diagnosticFailureCount = 1u;
    arp::core::appendDiagnostic(report.diagnostics,
        RendererDeploymentEvidenceAdmission::reject(
            contract.backend, RendererDeploymentEvidenceStage::NativeReport));

    return report;
}

} // namespace

NativeRendererDeploymentBridge::NativeRendererDeploymentBridge(
    rhi::semantics::NativeBackendCapabilityContract contract)
    : contract_(std::move(contract))
{}

NativeRendererDeploymentReadinessReport NativeRendererDeploymentBridge::evaluate(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const
{
    return evaluateNativeRendererDeploymentReadiness(contract_, rendererFramePlan);
}

NativeRendererDeploymentReadinessReport evaluateNativeRendererDeploymentReadiness(
    const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    const auto fallbackPlan = buildNativeRendererFallbackPlan(contract, rendererFramePlan);
    return makeReportFromPlan(contract, rendererFramePlan, fallbackPlan);
}

void evaluateAndApplyFallback(const rhi::semantics::NativeBackendCapabilityContract& contract,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    rhi::DeviceCapabilities& caps)
{
    const auto fallbackPlan = buildNativeRendererFallbackPlan(contract, rendererFramePlan);
    fallbackPlan.applyRestrictionsTo(caps);
}

} // namespace arp::renderer::deployment
