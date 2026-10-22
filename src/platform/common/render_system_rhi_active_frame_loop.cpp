#include "platform/render_system_rhi_active_frame_loop.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_common.h"

namespace arp::platform {

namespace {

bool statusAllowsActiveFrameLoop(RenderSystemRhiStartupReadinessStatus status)
{
    return status == RenderSystemRhiStartupReadinessStatus::PrototypeComplete
        || status == RenderSystemRhiStartupReadinessStatus::BackendValidated
        || status == RenderSystemRhiStartupReadinessStatus::PlatformValidated;
}

bool acceptsStartupReadiness(const RenderSystemRhiStartupReadinessReport& readiness)
{
    return readiness.canBeginFormalRhiRefactor && readiness.prototypeComplete
        && readiness.hiddenWindowValidated && readiness.diagnosticsOnly && readiness.nonBlocking
        && readiness.v1RenderPathUnaffected && readiness.backendFallbacksHiddenFromUpperLayer
        && !readiness.nativeHandleExposedToUpperLayer && !readiness.diagnosticFailure
        && statusAllowsActiveFrameLoop(readiness.status);
}

bool acceptsStartupSmokeCoverage(const RenderSystemRhiStartupTelemetryReport& telemetry)
{
    return telemetry.success && telemetry.probeExecuted && telemetry.probeCompleted
        && telemetry.diagnosticsOnly && telemetry.nonBlocking && telemetry.v1RenderPathUnaffected
        && (!telemetry.hiddenWindowRequired || telemetry.hiddenWindowUsed) && telemetry.acquireFrame
        && telemetry.recordFrame && telemetry.submitFrame && telemetry.presentFrame
        && telemetry.backendFallbacksHiddenFromUpperLayer
        && !telemetry.nativeHandleExposedToUpperLayer && !telemetry.diagnosticFailure;
}

bool acceptsProductionCutover(const RenderSystemRhiProductionCutoverEvidence& evidence)
{
    return evidence.nullHiddenFrameSmokePassed && evidence.vulkanHiddenWindowSmokePassed
        && evidence.webGpuBrowserRuntimeSmokePassed && evidence.requiredPlatformSmokesPassed
        && evidence.nativeHeaderIsolationVerified && evidence.hotPathBounded && evidence.nonBlocking
        && evidence.backendFallbacksHiddenFromUpperLayer
        && !evidence.nativeHandleExposedToUpperLayer;
}

bool productionCutoverBoundaryFailure(const RenderSystemRhiProductionCutoverEvidence& evidence)
{
    return !evidence.nonBlocking || !evidence.backendFallbacksHiddenFromUpperLayer
        || evidence.nativeHandleExposedToUpperLayer;
}

RenderSystemRhiActiveFrameLoopReport makeBaseReport(const RenderSystemRhiActiveFrameLoopDesc& desc,
    const RenderSystemRhiActiveFrameLoopInput& input)
{
    RenderSystemRhiActiveFrameLoopReport report;
    report.productionDefaultRequested = desc.requestProductionDefault;
    report.productionCutoverAccepted = acceptsProductionCutover(desc.productionCutover);
    report.promotedToProductionDefault =
        desc.requestProductionDefault && report.productionCutoverAccepted;
    report.enabled = desc.enabled || report.promotedToProductionDefault;
    report.guardEnabled = desc.guardEnabled || report.promotedToProductionDefault;
    report.v1RenderPathAvailable = desc.v1FallbackAvailable;
    report.fallbackToV1 = desc.v1FallbackAvailable;
    report.requireStartupReadiness = desc.requireStartupReadiness;
    report.requireStartupSmokeCoverage = desc.requireStartupSmokeCoverage;
    report.startupReadinessStatus = input.startupReadiness.status;
    report.hiddenWindowValidated = input.startupReadiness.hiddenWindowValidated;
    report.nonBlocking = input.startupReadiness.nonBlocking;
    report.startupReadinessAccepted = acceptsStartupReadiness(input.startupReadiness);
    report.startupSmokeCoverageAccepted = acceptsStartupSmokeCoverage(input.startupTelemetry);
    report.startupProbeExecuted = input.startupTelemetry.probeExecuted;
    report.startupProbeCompleted = input.startupTelemetry.probeCompleted;
    report.startupHiddenWindowUsed = input.startupTelemetry.hiddenWindowUsed;
    report.backendFallbacksHiddenFromUpperLayer =
        input.startupReadiness.backendFallbacksHiddenFromUpperLayer
        && input.startupTelemetry.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = input.startupReadiness.nativeHandleExposedToUpperLayer
        || input.startupTelemetry.nativeHandleExposedToUpperLayer;
    if (desc.requestProductionDefault) {
        report.nonBlocking = report.nonBlocking && desc.productionCutover.nonBlocking;
        report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
            && desc.productionCutover.backendFallbacksHiddenFromUpperLayer;
        report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
            || desc.productionCutover.nativeHandleExposedToUpperLayer;
    }
    return report;
}

void useSkipFallback(
    RenderSystemRhiActiveFrameLoopReport& report, const std::pmr::string& diagnostics)
{
    report.success = true;
    report.action = platform::RenderSystemFrameLoopAction::SkipFrame;
    report.skipFrame = true;
    report.usedRhiFrameLoop = false;
    report.activeMode = false;
    report.fallbackToV1 = report.v1RenderPathAvailable;
    arp::core::appendDiagnostic(report.diagnostics, diagnostics);
}

void copyRuntimePlan(RenderSystemRhiActiveFrameLoopReport& report,
    const platform::RenderSystemFrameLoopRuntimePlan& runtimePlan)
{
    report.runtimePlan = runtimePlan;
    report.success = runtimePlan.success;
    report.activeMode = runtimePlan.success
        && runtimePlan.action != platform::RenderSystemFrameLoopAction::DiagnosticFailure;
    report.usedRhiFrameLoop = report.activeMode;
    report.fallbackToV1 = !report.usedRhiFrameLoop && report.v1RenderPathAvailable;
    report.action = runtimePlan.action;
    report.selectedBackend = runtimePlan.selectedBackend;
    report.frameSlot = runtimePlan.frameSlot;
    report.frameSerial = runtimePlan.frameSerial;
    report.acquireFrame = runtimePlan.acquireFrame;
    report.recordFrame = runtimePlan.recordFrame;
    report.submitFrame = runtimePlan.submitFrame;
    report.presentFrame = runtimePlan.presentFrame;
    report.skipFrame = runtimePlan.skipFrame;
    report.runMaintenance = runtimePlan.runMaintenance;
    report.releaseCurrentFrame = runtimePlan.releaseCurrentFrame;
    report.collectCompletedSubmissions = runtimePlan.collectCompletedSubmissions;
    report.swapchainRebuildRequired = runtimePlan.swapchainRebuildRequired;
    report.releaseSurfaceResources = runtimePlan.releaseSurfaceResources;
    report.releaseTransientResources = runtimePlan.releaseTransientResources;
    report.memoryBudgetRefreshRequired = runtimePlan.memoryBudgetRefreshRequired;
    report.telemetryRequired = runtimePlan.telemetryRequired;
    report.backendFallbacksHiddenFromUpperLayer = runtimePlan.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = runtimePlan.nativeHandleExposedToUpperLayer;
    arp::core::appendDiagnostic(report.diagnostics, runtimePlan.diagnostics);
}

} // namespace

RenderSystemRhiActiveFrameLoopHook::RenderSystemRhiActiveFrameLoopHook(
    const RenderSystemRhiActiveFrameLoopDesc& desc)
    : desc_(desc)
{}

RenderSystemRhiActiveFrameLoopReport RenderSystemRhiActiveFrameLoopHook::evaluate(
    const RenderSystemRhiActiveFrameLoopInput& input) const
{
    return evaluateRenderSystemRhiActiveFrameLoop(desc_, input);
}

RenderSystemRhiActiveFrameLoopReport evaluateRenderSystemRhiActiveFrameLoop(
    const RenderSystemRhiActiveFrameLoopDesc& desc,
    const RenderSystemRhiActiveFrameLoopInput& input)
{
    auto report = makeBaseReport(desc, input);

    if (!desc.v1FallbackAvailable) {
        report.action = platform::RenderSystemFrameLoopAction::DiagnosticFailure;
        report.fallbackToV1 = false;
        arp::core::appendDiagnostic(report.diagnostics,
            "RHI active frame loop requires the v1 render path to remain available");
        return report;
    }
    if (desc.requestProductionDefault && !report.productionCutoverAccepted) {
        if (productionCutoverBoundaryFailure(desc.productionCutover)) {
            report.action = platform::RenderSystemFrameLoopAction::DiagnosticFailure;
            report.fallbackToV1 = report.v1RenderPathAvailable;
            arp::core::appendDiagnostic(
                report.diagnostics, "RHI production cutover rejected unsafe default evidence");
            return report;
        }

        useSkipFallback(report,
            "RHI production cutover is waiting for Null, Vulkan, WebGPU, and platform smoke evidence");
        return report;
    }
    if (!report.enabled) {
        useSkipFallback(report, "RHI active frame loop is disabled");
        return report;
    }
    if (!report.guardEnabled) {
        useSkipFallback(report, "RHI active frame loop guard is disabled");
        return report;
    }

    if (desc.requireStartupSmokeCoverage && !report.startupSmokeCoverageAccepted) {
        const bool boundaryFailure = !input.startupTelemetry.diagnosticsOnly
            || !input.startupTelemetry.nonBlocking || !input.startupTelemetry.v1RenderPathUnaffected
            || !input.startupTelemetry.backendFallbacksHiddenFromUpperLayer
            || input.startupTelemetry.nativeHandleExposedToUpperLayer;
        if (boundaryFailure) {
            report.action = platform::RenderSystemFrameLoopAction::DiagnosticFailure;
            arp::core::appendDiagnostic(
                report.diagnostics, "RHI active frame loop rejected unsafe startup smoke coverage");
            return report;
        }

        useSkipFallback(report, "RHI active frame loop is waiting for startup smoke coverage");
        return report;
    }

    if (desc.requireStartupReadiness && !report.startupReadinessAccepted) {
        const bool boundaryFailure = !input.startupReadiness.diagnosticsOnly
            || !input.startupReadiness.nonBlocking || !input.startupReadiness.v1RenderPathUnaffected
            || !input.startupReadiness.backendFallbacksHiddenFromUpperLayer
            || input.startupReadiness.nativeHandleExposedToUpperLayer;
        if (boundaryFailure) {
            report.action = platform::RenderSystemFrameLoopAction::DiagnosticFailure;
            arp::core::appendDiagnostic(
                report.diagnostics, "RHI active frame loop rejected unsafe startup readiness");
            return report;
        }

        useSkipFallback(report, "RHI active frame loop is waiting for startup readiness");
        return report;
    }

    copyRuntimePlan(report,
        platform::planRenderSystemFrameLoopRuntime(
            desc.runtime, input.rendererFramePlan, input.lifecycleState, input.renderSystemState));

    if (!report.runtimePlan.diagnostics.empty()) {
        rhi::RhiErrorHandler::get().report(
            report.runtimePlan.success ? rhi::RhiSeverity::Warning : rhi::RhiSeverity::Error,
            report.runtimePlan.diagnostics.c_str());
    }

    if (!report.backendFallbacksHiddenFromUpperLayer) {
        report.success = false;
        report.activeMode = false;
        report.usedRhiFrameLoop = false;
        report.fallbackToV1 = report.v1RenderPathAvailable;
        report.action = platform::RenderSystemFrameLoopAction::DiagnosticFailure;
        arp::core::appendDiagnostic(
            report.diagnostics, "RHI active frame loop rejected visible backend fallback details");
    }
    if (report.nativeHandleExposedToUpperLayer) {
        report.success = false;
        report.activeMode = false;
        report.usedRhiFrameLoop = false;
        report.fallbackToV1 = report.v1RenderPathAvailable;
        report.action = platform::RenderSystemFrameLoopAction::DiagnosticFailure;
        arp::core::appendDiagnostic(
            report.diagnostics, "RHI active frame loop rejected native handle exposure");
    }

    return report;
}

} // namespace arp::platform
