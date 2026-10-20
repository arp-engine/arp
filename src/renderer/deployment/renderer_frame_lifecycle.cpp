#include "renderer/deployment/renderer_frame_lifecycle.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "renderer/deployment/renderer_deployment_evidence_admission.h"

#include <utility>

namespace arp::renderer::deployment {

RendererFrameStartGate::RendererFrameStartGate(const RendererFrameStartGateDesc& desc) : desc_(desc)
{}

RendererFrameStartDecision RendererFrameStartGate::evaluate(
    const RendererBackendDeploymentPlan& deployment,
    const render_graph::RhiFramePlan& framePlan,
    const platform::PlatformLifecycleState& lifecycleState) const
{
    return evaluateRendererFrameStart(desc_, deployment, framePlan, lifecycleState);
}

RendererFrameStartDecision evaluateRendererFrameStart(const RendererFrameStartGateDesc& desc,
    const RendererBackendDeploymentPlan& deployment,
    const render_graph::RhiFramePlan& framePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    RendererFrameStartDecision decision;
    decision.backend = deployment.selectedBackend;
    decision.frameSlot = framePlan.frameSlot;
    decision.telemetryRequired = deployment.telemetryRequired;
    decision.swapchainRebuildRequired = lifecycleState.swapchainRebuildRequired;
    decision.releaseSurfaceResources = lifecycleState.releaseSurfaceResources;
    decision.releaseTransientResources = lifecycleState.releaseTransientResources;
    decision.memoryBudgetRefreshRequired = lifecycleState.memoryBudgetRefreshRequired;
    decision.backendFallbacksHiddenFromUpperLayer = deployment.backendFallbacksHiddenFromUpperLayer;
    decision.nativeHandleExposedToUpperLayer = deployment.nativeHandleExposedToUpperLayer
        || lifecycleState.nativeHandleExposedToUpperLayer;
    arp::core::appendDiagnostic(decision.diagnostics,
        RendererDeploymentEvidenceAdmission::reject(
            deployment.selectedBackend, RendererDeploymentEvidenceStage::FrameStart));

    if (desc.logicalFrameSlots != 2 || lifecycleState.logicalFrameSlots != 2) {
        arp::core::appendDiagnostic(
            decision.diagnostics, "renderer frame start requires two logical frame slots");
    }
    if (framePlan.frameSlot >= desc.logicalFrameSlots) {
        arp::core::appendDiagnostic(decision.diagnostics,
            "renderer frame start frame slot is outside the double-buffer range");
    }
    if (!deployment.success || !deployment.canStartFrame) {
        arp::core::appendDiagnostic(decision.diagnostics, deployment.diagnostics);
        arp::core::appendDiagnostic(
            decision.diagnostics, "renderer backend deployment cannot start frame");
    }
    if (desc.requireAcquireAllowed && !lifecycleState.acquireAllowed) {
        arp::core::appendDiagnostic(
            decision.diagnostics, "renderer frame start requires acquire permission");
    }
    if (desc.requirePresentAllowed && !lifecycleState.presentAllowed) {
        arp::core::appendDiagnostic(
            decision.diagnostics, "renderer frame start requires present permission");
    }
    if (decision.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            decision.diagnostics, "renderer frame start rejected native handle exposure");
    }

    decision.success = false;
    decision.canStartFrame = false;
    decision.telemetryRequired = true;
    return decision;
}

RendererFrameLifecyclePlanner::RendererFrameLifecyclePlanner(
    const RendererFrameLifecyclePlannerDesc& desc)
    : desc_(desc)
{}

RendererFrameLifecyclePlan RendererFrameLifecyclePlanner::plan(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState) const
{
    return planRendererFrameLifecycle(desc_, rendererFramePlan, lifecycleState);
}

RendererFrameLifecyclePlan planRendererFrameLifecycle(const RendererFrameLifecyclePlannerDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    RendererFrameLifecyclePlan plan;
    plan.deployment = planRendererBackendDeployment(desc.deployment, rendererFramePlan);
    plan.frameStart = evaluateRendererFrameStart(
        desc.frameStart, plan.deployment, rendererFramePlan.plan, lifecycleState);

    plan.success = plan.deployment.success && plan.frameStart.success;
    plan.canStartFrame = plan.success && plan.frameStart.canStartFrame;
    plan.selectedBackend = plan.deployment.selectedBackend;
    plan.telemetryRequired = plan.deployment.telemetryRequired || plan.frameStart.telemetryRequired;
    plan.backendFallbacksHiddenFromUpperLayer = plan.deployment.backendFallbacksHiddenFromUpperLayer
        && plan.frameStart.backendFallbacksHiddenFromUpperLayer
        && !rendererFramePlan.backendSpecificBranchingRequired;
    plan.nativeHandleExposedToUpperLayer = plan.deployment.nativeHandleExposedToUpperLayer
        || plan.frameStart.nativeHandleExposedToUpperLayer
        || rendererFramePlan.nativeHandleExposedToUpperLayer;
    arp::core::appendDiagnostic(plan.diagnostics, plan.deployment.diagnostics);
    arp::core::appendDiagnostic(plan.diagnostics, plan.frameStart.diagnostics);

    return plan;
}

RendererFrameLifecycleTelemetryCollector::RendererFrameLifecycleTelemetryCollector(
    const RendererFrameLifecycleTelemetryCollectorDesc& desc)
    : desc_(desc)
{
    diagnostics_.reserve(desc_.maxDiagnostics);
}

void RendererFrameLifecycleTelemetryCollector::record(const RendererFrameLifecyclePlan& lifecycle)
{
    lifecycleRecorded_ = true;
    report_.success = lifecycle.success;
    report_.canStartFrame = lifecycle.canStartFrame;
    report_.selectedBackend = lifecycle.selectedBackend;
    report_.frameSlot = lifecycle.frameStart.frameSlot;
    report_.candidateCount = lifecycle.deployment.candidateCount;
    report_.diagnosticCandidateCount = lifecycle.deployment.diagnosticCandidateCount;
    report_.nativeFeatureCount = lifecycle.deployment.selectedReadiness.nativeFeatureCount;
    report_.emulatedFeatureCount = lifecycle.deployment.selectedReadiness.emulatedFeatureCount;
    report_.cpuReplayFeatureCount = lifecycle.deployment.selectedReadiness.cpuReplayFeatureCount;
    report_.degradedFeatureCount = lifecycle.deployment.selectedReadiness.degradedFeatureCount;
    report_.deploymentDiagnosticFailureCount = lifecycle.deployment.success ? 0 : 1;
    report_.frameStartDiagnosticFailureCount = lifecycle.frameStart.success ? 0 : 1;
    report_.telemetryRequired = lifecycle.telemetryRequired;
    report_.backendFallbacksHiddenFromUpperLayer = lifecycle.backendFallbacksHiddenFromUpperLayer;
    report_.nativeHandleExposedToUpperLayer = lifecycle.nativeHandleExposedToUpperLayer;
    report_.swapchainRebuildRequired = lifecycle.frameStart.swapchainRebuildRequired;
    report_.releaseSurfaceResources = lifecycle.frameStart.releaseSurfaceResources;
    report_.releaseTransientResources = lifecycle.frameStart.releaseTransientResources;
    report_.memoryBudgetRefreshRequired = lifecycle.frameStart.memoryBudgetRefreshRequired;

    if (!lifecycle.diagnostics.empty()) {
        report_.telemetryRequired = true;
        recordDiagnostic(lifecycle.diagnostics);
    }
    if (report_.degradedFeatureCount > 0) {
        report_.telemetryRequired = true;
    }
}

RendererFrameLifecycleTelemetryReport RendererFrameLifecycleTelemetryCollector::buildReport() const
{
    RendererFrameLifecycleTelemetryReport report;
    report = report_; // Assignment retains the destination's explicit core resource.
    report.recordedDiagnosticCount = static_cast<std::uint32_t>(diagnostics_.size());
    report.droppedDiagnosticCount = droppedDiagnosticCount_;
    report.diagnosticCapacityExhausted = diagnosticCapacityExhausted_;
    report.diagnosticFailureCount =
        report.deploymentDiagnosticFailureCount + report.frameStartDiagnosticFailureCount;

    for (const auto& diagnostics : diagnostics_) {
        arp::core::appendDiagnostic(report.diagnostics, diagnostics);
    }

    if (diagnosticCapacityExhausted_) {
        arp::core::appendDiagnostic(report.diagnostics,
            "renderer frame lifecycle telemetry diagnostics capacity exhausted");
        report.telemetryRequired = true;
    }

    if (!lifecycleRecorded_) {
        arp::core::appendDiagnostic(
            report.diagnostics, "renderer frame lifecycle telemetry requires a lifecycle plan");
        ++report.diagnosticFailureCount;
        report.telemetryRequired = true;
        report.success = false;
        report.canStartFrame = false;
    }

    report.success = false;
    report.canStartFrame = false;
    report.telemetryRequired = true;
    if (report.diagnosticFailureCount == 0u)
        report.diagnosticFailureCount = 1u;
    arp::core::appendDiagnostic(report.diagnostics,
        RendererDeploymentEvidenceAdmission::reject(
            report.selectedBackend, RendererDeploymentEvidenceStage::LifecycleCollection));
    return report;
}

void RendererFrameLifecycleTelemetryCollector::recordDiagnostic(
    const core::pmr::String& diagnostics)
{
    if (diagnostics.empty()) {
        return;
    }

    if (diagnostics_.size() < desc_.maxDiagnostics) {
        diagnostics_.push_back(diagnostics);
        return;
    }

    diagnosticCapacityExhausted_ = true;
    ++droppedDiagnosticCount_;
}

RendererFrameLifecycleTelemetryReport buildRendererFrameLifecycleTelemetryReport(
    const RendererFrameLifecyclePlan& lifecycle, std::uint32_t maxDiagnostics)
{
    RendererFrameLifecycleTelemetryCollector collector({maxDiagnostics});
    collector.record(lifecycle);
    return collector.buildReport();
}

RendererFrameLifecycleReadinessReport RendererFrameLifecycleReadinessGate::evaluate(
    const RendererFrameLifecycleTelemetryReport& telemetry) const
{
    return evaluateRendererFrameLifecycleReadiness(telemetry);
}

RendererFrameLifecycleReadinessReport evaluateRendererFrameLifecycleReadiness(
    const RendererFrameLifecycleTelemetryReport& telemetry)
{
    RendererFrameLifecycleReadinessReport readiness;
    readiness.selectedBackend = telemetry.selectedBackend;
    readiness.frameSlot = telemetry.frameSlot;
    readiness.candidateCount = telemetry.candidateCount;
    readiness.degradedFeatureCount = telemetry.degradedFeatureCount;
    readiness.diagnosticFailureCount = telemetry.diagnosticFailureCount;
    readiness.telemetryRequired = telemetry.telemetryRequired;
    readiness.backendFallbacksHiddenFromUpperLayer = telemetry.backendFallbacksHiddenFromUpperLayer;
    readiness.nativeHandleExposedToUpperLayer = telemetry.nativeHandleExposedToUpperLayer;
    readiness.diagnostics = telemetry.diagnostics;

    if (telemetry.frameSlot >= 2) {
        arp::core::appendDiagnostic(readiness.diagnostics,
            "renderer frame lifecycle readiness rejected frame slot outside double-buffer range");
        ++readiness.diagnosticFailureCount;
    }
    if (telemetry.candidateCount == 0) {
        arp::core::appendDiagnostic(readiness.diagnostics,
            "renderer frame lifecycle readiness requires at least one backend candidate");
        ++readiness.diagnosticFailureCount;
    }
    if (!telemetry.backendFallbacksHiddenFromUpperLayer) {
        arp::core::appendDiagnostic(readiness.diagnostics,
            "renderer frame lifecycle readiness rejected visible backend fallbacks");
        ++readiness.diagnosticFailureCount;
    }
    if (telemetry.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(readiness.diagnostics,
            "renderer frame lifecycle readiness rejected native handle exposure");
        ++readiness.diagnosticFailureCount;
    }
    if (telemetry.diagnosticCapacityExhausted) {
        arp::core::appendDiagnostic(readiness.diagnostics,
            "renderer frame lifecycle readiness requires complete diagnostics");
        ++readiness.diagnosticFailureCount;
    }

    readiness.status = RendererFrameLifecycleReadinessStatus::DiagnosticFailure;
    readiness.success = false;
    readiness.canStartFrame = false;
    readiness.telemetryRequired = true;
    if (readiness.diagnosticFailureCount == 0u)
        readiness.diagnosticFailureCount = 1u;
    arp::core::appendDiagnostic(readiness.diagnostics,
        RendererDeploymentEvidenceAdmission::reject(
            telemetry.selectedBackend, RendererDeploymentEvidenceStage::LifecycleReadiness));
    return readiness;
}

RendererFrameLifecycleBridge::RendererFrameLifecycleBridge(
    const RendererFrameLifecycleBridgeDesc& desc)
    : desc_(desc)
{}

RendererFrameLifecycleDecision RendererFrameLifecycleBridge::evaluate(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState) const
{
    return evaluateRendererFrameLifecycleBridge(desc_, rendererFramePlan, lifecycleState);
}

RendererFrameLifecycleDecision evaluateRendererFrameLifecycleBridge(
    const RendererFrameLifecycleBridgeDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    RendererFrameLifecycleDecision decision;
    decision.lifecycle =
        planRendererFrameLifecycle(desc.planner, rendererFramePlan, lifecycleState);
    decision.telemetry = buildRendererFrameLifecycleTelemetryReport(
        decision.lifecycle, desc.maxTelemetryDiagnostics);
    decision.readiness = evaluateRendererFrameLifecycleReadiness(decision.telemetry);

    decision.success = decision.readiness.success;
    decision.canStartFrame = decision.readiness.canStartFrame;
    decision.status = decision.readiness.status;
    decision.selectedBackend = decision.readiness.selectedBackend;
    decision.telemetryRequired = decision.lifecycle.telemetryRequired
        || decision.telemetry.telemetryRequired || decision.readiness.telemetryRequired;
    decision.backendFallbacksHiddenFromUpperLayer =
        decision.readiness.backendFallbacksHiddenFromUpperLayer;
    decision.nativeHandleExposedToUpperLayer = decision.readiness.nativeHandleExposedToUpperLayer;
    decision.diagnostics = decision.readiness.diagnostics;
    return decision;
}

} // namespace arp::renderer::deployment
