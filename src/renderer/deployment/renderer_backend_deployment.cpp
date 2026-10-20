#include "renderer/deployment/renderer_backend_deployment.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "renderer/deployment/renderer_deployment_evidence_admission.h"

#include <limits>
#include <string_view>
#include <utility>

namespace arp::renderer::deployment {

namespace {

void markDiagnosticFailure(RendererBackendSelectionResult& result, std::string_view reason)
{
    result.diagnosticFailureUserVisible = true;
    result.diagnosticFailureSilent = false;
    result.diagnosticFailureReason = reason;
    arp::core::appendDiagnostic(result.diagnostics, reason);
}

RendererBackendSelectionResult makeCapacityFailure(
    const RendererBackendSelectionDesc& desc, const char* diagnostic)
{
    RendererBackendSelectionResult result;
    result.success = false;
    result.candidateCount = desc.candidates.size() > UINT32_MAX
        ? 0u
        : static_cast<std::uint32_t>(desc.candidates.size());
    result.diagnosticCandidateCount = 1;
    result.telemetryRequired = true;
    result.selectedCandidateIndex = std::numeric_limits<std::uint32_t>::max();
    markDiagnosticFailure(result, diagnostic);
    arp::core::appendDiagnostic(result.diagnosticFailureReason,
        "RHI diagnostic failure: renderer backend selection candidate capacity exhausted");
    arp::core::appendDiagnostic(result.diagnostics,
        "RHI diagnostic failure: renderer backend selection candidate capacity exhausted");
    return result;
}

} // namespace

RendererBackendSelectionPolicy::RendererBackendSelectionPolicy(RendererBackendPreference preference)
    : preference_(preference)
{}

RendererBackendSelectionResult RendererBackendSelectionPolicy::select(
    const RendererBackendSelectionDesc& desc) const
{
    return selectRendererBackend(preference_, desc);
}

RendererBackendSelectionResult selectRendererBackend(
    RendererBackendPreference preference, const RendererBackendSelectionDesc& desc)
{
    switch (preference) {
    case RendererBackendPreference::NativeValidatedFirst:
    case RendererBackendPreference::WebFirst:
        break;
    default: {
        RendererBackendSelectionResult result;
        result.selectedCandidateIndex = std::numeric_limits<std::uint32_t>::max();
        result.telemetryRequired = true;
        markDiagnosticFailure(result, "RHI-RENDERER-DEPLOYMENT-PREFERENCE-UNKNOWN");
        return result;
    }
    }

    if (const auto* diagnostic = RendererDeploymentEvidenceAdmission::candidateCountDiagnostic(
            desc.candidates.size(), desc.maxCandidates)) {
        return makeCapacityFailure(desc, diagnostic);
    }

    RendererBackendSelectionResult result;
    result.candidateCount = static_cast<std::uint32_t>(desc.candidates.size());
    result.selectedCandidateIndex = std::numeric_limits<std::uint32_t>::max();

    for (std::uint32_t index = 0; index < result.candidateCount; ++index) {
        const auto& candidate = desc.candidates[index];
        ++result.diagnosticCandidateCount;
        arp::core::appendDiagnostic(result.diagnostics,
            RendererDeploymentEvidenceAdmission::reject(
                candidate.backend, RendererDeploymentEvidenceStage::CandidateSelection));
        arp::core::appendDiagnostic(result.diagnostics, candidate.diagnostics);
    }

    result.telemetryRequired = true;
    markDiagnosticFailure(result,
        "RHI-RENDERER-DEPLOYMENT-NO-AUTHENTICATED-CANDIDATE; RHI diagnostic failure: no renderer backend candidate can start frame");
    return result;
}

namespace {

NativeRendererDeploymentReadinessReport mapWebGpuReadiness(
    const WebGpuDeploymentReadinessReport& readiness)
{
    NativeRendererDeploymentReadinessReport report;
    report.backend = readiness.backend;
    report.telemetryRequired = true;
    report.degradedFeatureCount = readiness.degradedFeatureCount;
    report.diagnosticFailureCount = readiness.diagnosticFailureCount;
    report.backendFallbacksHiddenFromUpperLayer = readiness.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = readiness.nativeHandleExposedToUpperLayer;
    report.cpuReplayFeatureCount = readiness.cpuReplayFeatureCount;
    report.upperLayerVisibleFallbacks = !readiness.backendFallbacksHiddenFromUpperLayer
        || readiness.nativeHandleExposedToUpperLayer;
    report.diagnostics = readiness.diagnostics;
    if (report.diagnosticFailureCount == 0u)
        report.diagnosticFailureCount = 1u;
    arp::core::appendDiagnostic(report.diagnostics,
        RendererDeploymentEvidenceAdmission::reject(
            report.backend, RendererDeploymentEvidenceStage::NativeReport));
    return report;
}

bool canAppendCandidate(const RendererBackendCandidateBuildResult& result)
{
    return result.selection.candidates.size() < result.selection.maxCandidates;
}

void appendCandidate(RendererBackendCandidateBuildResult& result,
    const NativeRendererDeploymentReadinessReport& candidate)
{
    if (!canAppendCandidate(result)) {
        result.success = false;
        result.capacityExceeded = true;
        arp::core::appendDiagnostic(
            result.diagnostics, RendererDeploymentEvidenceAdmission::capacityDiagnostic());
        arp::core::appendDiagnostic(
            result.diagnostics, "renderer backend candidate capacity exhausted");
        return;
    }

    result.selection.candidates.push_back(candidate);
}

} // namespace

RendererBackendCandidateBuilder::RendererBackendCandidateBuilder(
    const RendererBackendCandidateBuilderDesc& desc)
    : desc_(desc)
{}

RendererBackendCandidateBuildResult RendererBackendCandidateBuilder::build(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const
{
    return buildRendererBackendCandidates(desc_, rendererFramePlan);
}

RendererBackendCandidateBuildResult buildRendererBackendCandidates(
    const RendererBackendCandidateBuilderDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    RendererBackendCandidateBuildResult result;
    result.success = true;
    result.selection.maxCandidates = desc.maxCandidates;
    if (desc.maxCandidates == 0) {
        result.success = false;
        result.capacityExceeded = true;
        result.diagnostics = RendererDeploymentEvidenceAdmission::capacityDiagnostic();
        arp::core::appendDiagnostic(
            result.diagnostics, "renderer backend candidate capacity exhausted");
        return result;
    }
    result.selection.candidates.reserve(desc.maxCandidates);

    for (const auto& contract : desc.nativeContracts) {
        if (contract.backend == rhi::Backend::WebGPU && desc.includeWebGpuRuntimeProfile) {
            continue;
        }

        appendCandidate(
            result, evaluateNativeRendererDeploymentReadiness(contract, rendererFramePlan));
        if (result.capacityExceeded) {
            return result;
        }
    }

    if (desc.includeWebGpuRuntimeProfile) {
        const auto webGpuReadiness = evaluateRendererWebGpuDeploymentReadiness(desc.webGpuProfile,
            rendererFramePlan,
            desc.webGpuLifecycleState,
            desc.webGpuLifecycleEvent,
            desc.maxTelemetryDiagnostics);
        appendCandidate(result, mapWebGpuReadiness(webGpuReadiness));
    }

    return result;
}

namespace {

RendererBackendDeploymentPlan makeCandidateBuildFailure(
    const RendererBackendCandidateBuildResult& candidateBuild)
{
    RendererBackendDeploymentPlan plan;
    plan.success = false;
    plan.canStartFrame = false;
    plan.capacityExceeded = candidateBuild.capacityExceeded;
    plan.candidateCount = static_cast<std::uint32_t>(candidateBuild.selection.candidates.size());
    plan.diagnosticCandidateCount = candidateBuild.capacityExceeded ? 1 : 0;
    plan.selectedCandidateIndex = std::numeric_limits<std::uint32_t>::max();
    plan.diagnostics = candidateBuild.diagnostics;
    return plan;
}

RendererBackendDeploymentPlan makeSelectionPlan(
    const RendererBackendCandidateBuildResult& candidateBuild,
    const RendererBackendSelectionResult& selection)
{
    RendererBackendDeploymentPlan plan;
    plan.success = selection.success;
    plan.capacityExceeded = candidateBuild.capacityExceeded;
    plan.selectedBackend = selection.selectedBackend;
    plan.selectedStatus = selection.selectedStatus;
    plan.selectedCandidateIndex = selection.selectedCandidateIndex;
    plan.candidateCount = selection.candidateCount;
    plan.diagnosticCandidateCount = selection.diagnosticCandidateCount;
    plan.telemetryRequired = selection.telemetryRequired;
    arp::core::appendDiagnostic(plan.diagnostics, candidateBuild.diagnostics);
    arp::core::appendDiagnostic(plan.diagnostics, selection.diagnostics);

    if (selection.success
        && selection.selectedCandidateIndex < candidateBuild.selection.candidates.size()) {
        plan.selectedReadiness =
            candidateBuild.selection.candidates[selection.selectedCandidateIndex];
        plan.canStartFrame = plan.selectedReadiness.canStartFrame;
        plan.backendFallbacksHiddenFromUpperLayer =
            plan.selectedReadiness.backendFallbacksHiddenFromUpperLayer;
        plan.nativeHandleExposedToUpperLayer =
            plan.selectedReadiness.nativeHandleExposedToUpperLayer;
        plan.telemetryRequired = plan.telemetryRequired || plan.selectedReadiness.telemetryRequired;
    } else {
        plan.canStartFrame = false;
    }

    return plan;
}

} // namespace

RendererBackendDeploymentPlanner::RendererBackendDeploymentPlanner(
    const RendererBackendDeploymentPlannerDesc& desc)
    : desc_(desc)
{}

RendererBackendDeploymentPlan RendererBackendDeploymentPlanner::plan(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan) const
{
    return planRendererBackendDeployment(desc_, rendererFramePlan);
}

RendererBackendDeploymentPlan planRendererBackendDeployment(
    const RendererBackendDeploymentPlannerDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan)
{
    const auto candidateBuild = buildRendererBackendCandidates(desc.candidates, rendererFramePlan);
    if (!candidateBuild.success) {
        return makeCandidateBuildFailure(candidateBuild);
    }

    const auto selection = selectRendererBackend(desc.preference, candidateBuild.selection);
    return makeSelectionPlan(candidateBuild, selection);
}

} // namespace arp::renderer::deployment
