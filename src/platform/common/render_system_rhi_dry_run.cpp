#include "platform/render_system_rhi_dry_run.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

namespace arp::platform {

namespace {

RenderSystemRhiFrameLoopDryRunReport makeDisabledReport()
{
    RenderSystemRhiFrameLoopDryRunReport report;
    report.success = true;
    report.enabled = false;
    report.dryRunOnly = true;
    report.v1RenderPathUnaffected = true;
    report.action = platform::RenderSystemFrameLoopAction::SkipFrame;
    report.diagnostics = "RHI frame-loop dry run is disabled";
    return report;
}

RenderSystemRhiFrameLoopDryRunReport makeReportFromRuntimePlan(
    const platform::RenderSystemFrameLoopRuntimePlan& runtimePlan)
{
    RenderSystemRhiFrameLoopDryRunReport report;
    report.success = runtimePlan.success;
    report.enabled = true;
    report.dryRunOnly = true;
    report.v1RenderPathUnaffected = true;
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
    report.runtimePlan = runtimePlan;
    report.diagnostics = runtimePlan.diagnostics;
    return report;
}

} // namespace

RenderSystemRhiFrameLoopDryRunHook::RenderSystemRhiFrameLoopDryRunHook(
    const RenderSystemRhiFrameLoopDryRunDesc& desc)
    : desc_(desc)
{}

RenderSystemRhiFrameLoopDryRunReport RenderSystemRhiFrameLoopDryRunHook::evaluate(
    const RenderSystemRhiFrameLoopDryRunInput& input) const
{
    return evaluateRenderSystemRhiFrameLoopDryRun(desc_, input);
}

RenderSystemRhiFrameLoopDryRunReport evaluateRenderSystemRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunDesc& desc,
    const RenderSystemRhiFrameLoopDryRunInput& input)
{
    if (!desc.enabled) {
        return makeDisabledReport();
    }

    const auto runtimePlan = platform::planRenderSystemFrameLoopRuntime(
        desc.runtime, input.rendererFramePlan, input.lifecycleState, input.renderSystemState);
    return makeReportFromRuntimePlan(runtimePlan);
}

RenderSystemRhiFrameLoopDryRunCapabilityState summarizeRenderSystemRhiFrameLoopDryRunCapability(
    const RenderSystemRhiFrameLoopDryRunDesc& desc,
    const RenderSystemRhiFrameLoopDryRunReport& report)
{
    RenderSystemRhiFrameLoopDryRunCapabilityState state;
    state.available = true;
    state.enabled = desc.enabled;
    state.lastEvaluationSucceeded = report.success;
    state.dryRunOnly = report.dryRunOnly;
    state.v1RenderPathUnaffected = report.v1RenderPathUnaffected;
    state.action = report.action;
    state.selectedBackend = report.selectedBackend;
    state.telemetryRequired = report.telemetryRequired;
    state.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer;
    state.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer;
    state.diagnostics = report.diagnostics;
    return state;
}

RenderSystemRhiFrameLoopDryRunSmokeReport evaluateRenderSystemRhiFrameLoopDryRunSmoke(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& desc,
    const RenderSystemRhiFrameLoopDryRunReport& report)
{
    RenderSystemRhiFrameLoopDryRunSmokeReport smoke;
    smoke.dryRunOnly = report.dryRunOnly;
    smoke.v1RenderPathUnaffected = report.v1RenderPathUnaffected;
    smoke.acquireFrame = report.acquireFrame;
    smoke.recordFrame = report.recordFrame;
    smoke.submitFrame = report.submitFrame;
    smoke.presentFrame = report.presentFrame;
    smoke.telemetryRequired = report.telemetryRequired;
    smoke.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer;
    smoke.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer;
    smoke.dryRunReport = report;
    smoke.diagnostics = report.diagnostics;

    switch (report.action) {
    case platform::RenderSystemFrameLoopAction::BeginFrame:
        smoke.frameWouldBegin = true;
        break;
    case platform::RenderSystemFrameLoopAction::SkipFrame:
        smoke.frameWouldSkip = true;
        break;
    case platform::RenderSystemFrameLoopAction::MaintenanceRequired:
        smoke.maintenanceWouldRun = true;
        break;
    case platform::RenderSystemFrameLoopAction::DiagnosticFailure:
        smoke.diagnosticFailure = true;
        break;
    }

    if (!report.enabled) {
        arp::core::appendDiagnostic(
            smoke.diagnostics, "RHI frame-loop dry-run smoke requires enabled dry run");
    }
    if (!report.dryRunOnly) {
        arp::core::appendDiagnostic(
            smoke.diagnostics, "RHI frame-loop dry-run smoke requires diagnostics-only execution");
    }
    if (!report.v1RenderPathUnaffected) {
        arp::core::appendDiagnostic(smoke.diagnostics,
            "RHI frame-loop dry-run smoke requires v1 render path to remain unaffected");
    }
    if (!report.backendFallbacksHiddenFromUpperLayer) {
        arp::core::appendDiagnostic(smoke.diagnostics,
            "RHI frame-loop dry-run smoke rejected visible backend fallback details");
    }
    if (report.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            smoke.diagnostics, "RHI frame-loop dry-run smoke rejected native handle exposure");
    }
    if (desc.requireBeginFrame && !smoke.frameWouldBegin) {
        arp::core::appendDiagnostic(
            smoke.diagnostics, "RHI frame-loop dry-run smoke requires begin-frame action");
    }
    if (smoke.frameWouldBegin
        && (!report.acquireFrame || !report.recordFrame || !report.submitFrame
            || !report.presentFrame)) {
        arp::core::appendDiagnostic(smoke.diagnostics,
            "RHI frame-loop dry-run smoke begin-frame action requires acquire, record, submit, and present intent");
    }
    if (smoke.frameWouldSkip && !desc.allowSkipFrame) {
        arp::core::appendDiagnostic(
            smoke.diagnostics, "RHI frame-loop dry-run smoke rejected skip-frame action");
    }
    if (smoke.maintenanceWouldRun && !desc.allowMaintenance) {
        arp::core::appendDiagnostic(
            smoke.diagnostics, "RHI frame-loop dry-run smoke rejected maintenance action");
    }

    smoke.success = report.success && report.enabled && report.dryRunOnly
        && report.v1RenderPathUnaffected && report.backendFallbacksHiddenFromUpperLayer
        && !report.nativeHandleExposedToUpperLayer && !smoke.diagnosticFailure
        && (!desc.requireBeginFrame || smoke.frameWouldBegin)
        && (!smoke.frameWouldBegin
            || (report.acquireFrame && report.recordFrame && report.submitFrame
                && report.presentFrame))
        && (!smoke.frameWouldSkip || desc.allowSkipFrame)
        && (!smoke.maintenanceWouldRun || desc.allowMaintenance);
    return smoke;
}

RenderSystemRhiFrameLoopDryRunSmokeCapabilityState
summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(
    const RenderSystemRhiFrameLoopDryRunSmokeReport& report)
{
    RenderSystemRhiFrameLoopDryRunSmokeCapabilityState state;
    state.available = true;
    state.lastEvaluationSucceeded = report.success;
    state.readyForStartupProbe = report.success && report.dryRunOnly
        && report.v1RenderPathUnaffected && report.frameWouldBegin
        && report.backendFallbacksHiddenFromUpperLayer && !report.nativeHandleExposedToUpperLayer;
    state.dryRunOnly = report.dryRunOnly;
    state.v1RenderPathUnaffected = report.v1RenderPathUnaffected;
    state.frameWouldBegin = report.frameWouldBegin;
    state.frameWouldSkip = report.frameWouldSkip;
    state.maintenanceWouldRun = report.maintenanceWouldRun;
    state.diagnosticFailure = report.diagnosticFailure;
    state.selectedBackend = report.dryRunReport.selectedBackend;
    state.frameSlot = report.dryRunReport.frameSlot;
    state.frameSerial = report.dryRunReport.frameSerial;
    state.acquireFrame = report.acquireFrame;
    state.recordFrame = report.recordFrame;
    state.submitFrame = report.submitFrame;
    state.presentFrame = report.presentFrame;
    state.telemetryRequired = report.telemetryRequired;
    state.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer;
    state.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer;
    state.diagnostics = report.diagnostics;
    return state;
}

RenderSystemRhiStartupProbeReport evaluateRenderSystemRhiStartupProbe(
    const RenderSystemRhiFrameLoopDryRunSmokeCapabilityState& smoke)
{
    RenderSystemRhiStartupProbeReport probe;
    probe.readyForStartup = smoke.readyForStartupProbe;
    probe.dryRunOnly = smoke.dryRunOnly;
    probe.v1RenderPathUnaffected = smoke.v1RenderPathUnaffected;
    probe.frameWouldBegin = smoke.frameWouldBegin;
    probe.frameWouldSkip = smoke.frameWouldSkip;
    probe.maintenanceWouldRun = smoke.maintenanceWouldRun;
    probe.diagnosticFailure = smoke.diagnosticFailure;
    probe.selectedBackend = smoke.selectedBackend;
    probe.frameSlot = smoke.frameSlot;
    probe.frameSerial = smoke.frameSerial;
    probe.acquireFrame = smoke.acquireFrame;
    probe.recordFrame = smoke.recordFrame;
    probe.submitFrame = smoke.submitFrame;
    probe.presentFrame = smoke.presentFrame;
    probe.telemetryRequired = smoke.telemetryRequired;
    probe.backendFallbacksHiddenFromUpperLayer = smoke.backendFallbacksHiddenFromUpperLayer;
    probe.nativeHandleExposedToUpperLayer = smoke.nativeHandleExposedToUpperLayer;
    probe.diagnostics = smoke.diagnostics;

    if (!smoke.available) {
        arp::core::appendDiagnostic(
            probe.diagnostics, "RHI startup probe requires smoke capability availability");
    }
    if (!smoke.readyForStartupProbe) {
        arp::core::appendDiagnostic(probe.diagnostics, "RHI startup probe is not ready");
    }
    if (!smoke.dryRunOnly) {
        arp::core::appendDiagnostic(
            probe.diagnostics, "RHI startup probe requires diagnostics-only smoke");
    }
    if (!smoke.v1RenderPathUnaffected) {
        arp::core::appendDiagnostic(
            probe.diagnostics, "RHI startup probe requires v1 render path to remain unaffected");
    }
    if (!smoke.backendFallbacksHiddenFromUpperLayer) {
        arp::core::appendDiagnostic(
            probe.diagnostics, "RHI startup probe rejected visible backend fallback details");
    }
    if (smoke.nativeHandleExposedToUpperLayer) {
        arp::core::appendDiagnostic(
            probe.diagnostics, "RHI startup probe rejected native handle exposure");
    }

    probe.success = smoke.available && smoke.readyForStartupProbe && smoke.dryRunOnly
        && smoke.v1RenderPathUnaffected && smoke.backendFallbacksHiddenFromUpperLayer
        && !smoke.nativeHandleExposedToUpperLayer;
    return probe;
}

RenderSystemRhiStartupProbeCapabilityState summarizeRenderSystemRhiStartupProbeCapability(
    const RenderSystemRhiStartupProbeReport& report)
{
    RenderSystemRhiStartupProbeCapabilityState state;
    state.available = true;
    state.lastEvaluationSucceeded = report.success;
    state.readyForStartup = report.readyForStartup;
    state.dryRunOnly = report.dryRunOnly;
    state.v1RenderPathUnaffected = report.v1RenderPathUnaffected;
    state.frameWouldBegin = report.frameWouldBegin;
    state.frameWouldSkip = report.frameWouldSkip;
    state.maintenanceWouldRun = report.maintenanceWouldRun;
    state.diagnosticFailure = report.diagnosticFailure;
    state.selectedBackend = report.selectedBackend;
    state.frameSlot = report.frameSlot;
    state.frameSerial = report.frameSerial;
    state.acquireFrame = report.acquireFrame;
    state.recordFrame = report.recordFrame;
    state.submitFrame = report.submitFrame;
    state.presentFrame = report.presentFrame;
    state.telemetryRequired = report.telemetryRequired;
    state.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer;
    state.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer;
    state.diagnostics = report.diagnostics;
    return state;
}

RenderSystemRhiStartupGateReport evaluateRenderSystemRhiStartupGate(
    const RenderSystemRhiStartupProbeCapabilityState& probe)
{
    RenderSystemRhiStartupGateReport gate;
    gate.available = probe.available;
    gate.dryRunOnly = probe.dryRunOnly;
    gate.v1RenderPathUnaffected = probe.v1RenderPathUnaffected;
    gate.frameWouldBegin = probe.frameWouldBegin;
    gate.frameWouldSkip = probe.frameWouldSkip;
    gate.maintenanceWouldRun = probe.maintenanceWouldRun;
    gate.diagnosticFailure = probe.diagnosticFailure;
    gate.selectedBackend = probe.selectedBackend;
    gate.frameSlot = probe.frameSlot;
    gate.frameSerial = probe.frameSerial;
    gate.acquireFrame = probe.acquireFrame;
    gate.recordFrame = probe.recordFrame;
    gate.submitFrame = probe.submitFrame;
    gate.presentFrame = probe.presentFrame;
    gate.telemetryRequired = probe.telemetryRequired;
    gate.backendFallbacksHiddenFromUpperLayer = probe.backendFallbacksHiddenFromUpperLayer;
    gate.nativeHandleExposedToUpperLayer = probe.nativeHandleExposedToUpperLayer;
    gate.diagnostics = probe.diagnostics;

    const bool boundaryFailure = !probe.available || !probe.dryRunOnly
        || !probe.v1RenderPathUnaffected || !probe.backendFallbacksHiddenFromUpperLayer
        || probe.nativeHandleExposedToUpperLayer;
    gate.allowStartup = probe.lastEvaluationSucceeded && probe.readyForStartup
        && probe.frameWouldBegin && !boundaryFailure && !probe.diagnosticFailure;

    if (boundaryFailure || probe.diagnosticFailure) {
        gate.action = RenderSystemRhiStartupGateAction::DiagnosticFailure;
        if (!probe.available) {
            arp::core::appendDiagnostic(gate.diagnostics,
                "RHI startup gate requires startup probe capability availability");
        }
        if (!probe.dryRunOnly) {
            arp::core::appendDiagnostic(
                gate.diagnostics, "RHI startup gate requires diagnostics-only startup probe");
        }
        if (!probe.v1RenderPathUnaffected) {
            arp::core::appendDiagnostic(
                gate.diagnostics, "RHI startup gate requires v1 render path to remain unaffected");
        }
        if (!probe.backendFallbacksHiddenFromUpperLayer) {
            arp::core::appendDiagnostic(
                gate.diagnostics, "RHI startup gate rejected visible backend fallback details");
        }
        if (probe.nativeHandleExposedToUpperLayer) {
            arp::core::appendDiagnostic(
                gate.diagnostics, "RHI startup gate rejected native handle exposure");
        }
        return gate;
    }

    gate.success = true;
    if (gate.allowStartup) {
        gate.action = RenderSystemRhiStartupGateAction::RunStartupProbe;
        return gate;
    }
    if (probe.maintenanceWouldRun) {
        gate.action = RenderSystemRhiStartupGateAction::RunMaintenance;
        arp::core::appendDiagnostic(
            gate.diagnostics, "RHI startup gate requires maintenance before startup");
        return gate;
    }

    gate.action = RenderSystemRhiStartupGateAction::WaitForReadiness;
    arp::core::appendDiagnostic(
        gate.diagnostics, "RHI startup gate is waiting for startup readiness");
    if (!probe.lastEvaluationSucceeded && !probe.frameWouldBegin && !probe.frameWouldSkip
        && !probe.maintenanceWouldRun) {
        arp::core::appendDiagnostic(
            gate.diagnostics, "RHI startup gate requires startup probe evaluation");
    }
    return gate;
}

RenderSystemRhiStartupProbeScheduleReport scheduleRenderSystemRhiStartupProbe(
    const RenderSystemRhiStartupProbeScheduleDesc& desc,
    const RenderSystemRhiStartupGateReport& gate)
{
    RenderSystemRhiStartupProbeScheduleReport schedule;
    schedule.hiddenWindowRequired = desc.requireHiddenWindow;
    schedule.dryRunOnly = gate.dryRunOnly;
    schedule.v1RenderPathUnaffected = gate.v1RenderPathUnaffected;
    schedule.selectedBackend = gate.selectedBackend;
    schedule.frameSlot = gate.frameSlot;
    schedule.frameSerial = gate.frameSerial;
    schedule.logicalFrameSlots = desc.logicalFrameSlots;
    schedule.probeFrameCount = desc.probeFrameCount;
    schedule.maxProbeFrameCount = desc.maxProbeFrameCount;
    schedule.acquireFrame = gate.acquireFrame;
    schedule.recordFrame = gate.recordFrame;
    schedule.submitFrame = gate.submitFrame;
    schedule.presentFrame = gate.presentFrame;
    schedule.telemetryRequired = gate.telemetryRequired;
    schedule.backendFallbacksHiddenFromUpperLayer = gate.backendFallbacksHiddenFromUpperLayer;
    schedule.nativeHandleExposedToUpperLayer = gate.nativeHandleExposedToUpperLayer;
    schedule.gateReport = gate;
    schedule.diagnostics = gate.diagnostics;

    if (gate.action == RenderSystemRhiStartupGateAction::WaitForReadiness) {
        schedule.success = gate.success;
        schedule.waitForReadiness = true;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule is waiting for readiness");
        return schedule;
    }
    if (gate.action == RenderSystemRhiStartupGateAction::RunMaintenance) {
        schedule.success = gate.success;
        schedule.runMaintenance = true;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule requires maintenance");
        return schedule;
    }
    if (gate.action == RenderSystemRhiStartupGateAction::DiagnosticFailure || !gate.success) {
        schedule.diagnosticFailure = true;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule rejected diagnostic startup gate");
        return schedule;
    }

    if (!gate.allowStartup) {
        schedule.waitForReadiness = true;
        schedule.success = true;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule is waiting for startup allowance");
        return schedule;
    }

    bool validSchedule = true;
    if (desc.logicalFrameSlots != 2u) {
        validSchedule = false;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule requires two logical frame slots");
    }
    if (desc.probeFrameCount == 0u || desc.probeFrameCount > desc.maxProbeFrameCount) {
        validSchedule = false;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule requires a valid frame count");
    }
    if (desc.maxProbeFrameCount > desc.logicalFrameSlots) {
        validSchedule = false;
        arp::core::appendDiagnostic(schedule.diagnostics,
            "RHI startup probe schedule frame count must fit logical frame slots");
    }
    if (gate.frameSlot >= desc.logicalFrameSlots) {
        validSchedule = false;
        arp::core::appendDiagnostic(schedule.diagnostics,
            "RHI startup probe schedule rejected frame slot outside double buffer");
    }
    if (!gate.acquireFrame || !gate.recordFrame || !gate.submitFrame || !gate.presentFrame) {
        validSchedule = false;
        arp::core::appendDiagnostic(schedule.diagnostics,
            "RHI startup probe schedule requires acquire, record, submit, and present intent");
    }
    if (!gate.dryRunOnly) {
        validSchedule = false;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule requires diagnostics-only gate");
    }
    if (!gate.v1RenderPathUnaffected) {
        validSchedule = false;
        arp::core::appendDiagnostic(schedule.diagnostics,
            "RHI startup probe schedule requires v1 render path to remain unaffected");
    }
    if (!gate.backendFallbacksHiddenFromUpperLayer) {
        validSchedule = false;
        arp::core::appendDiagnostic(schedule.diagnostics,
            "RHI startup probe schedule rejected visible backend fallback details");
    }
    if (gate.nativeHandleExposedToUpperLayer) {
        validSchedule = false;
        arp::core::appendDiagnostic(
            schedule.diagnostics, "RHI startup probe schedule rejected native handle exposure");
    }

    schedule.success = validSchedule;
    schedule.scheduled = validSchedule;
    schedule.diagnosticFailure = !validSchedule;
    return schedule;
}

RenderSystemRhiStartupProbeBatchReport planRenderSystemRhiStartupProbeBatch(
    const RenderSystemRhiStartupProbeScheduleReport& schedule)
{
    RenderSystemRhiStartupProbeBatchReport batch;
    batch.hiddenWindowRequired = schedule.hiddenWindowRequired;
    batch.dryRunOnly = schedule.dryRunOnly;
    batch.v1RenderPathUnaffected = schedule.v1RenderPathUnaffected;
    batch.waitForReadiness = schedule.waitForReadiness;
    batch.runMaintenance = schedule.runMaintenance;
    batch.diagnosticFailure = schedule.diagnosticFailure;
    batch.selectedBackend = schedule.selectedBackend;
    batch.logicalFrameSlots = schedule.logicalFrameSlots;
    batch.frameCount = schedule.probeFrameCount;
    batch.firstFrameSerial = schedule.frameSerial;
    if (schedule.probeFrameCount > 0u) {
        batch.lastFrameSerial = schedule.frameSerial + schedule.probeFrameCount - 1u;
    }
    batch.acquireFrame = schedule.acquireFrame;
    batch.recordFrame = schedule.recordFrame;
    batch.submitFrame = schedule.submitFrame;
    batch.presentFrame = schedule.presentFrame;
    batch.telemetryRequired = schedule.telemetryRequired;
    batch.backendFallbacksHiddenFromUpperLayer = schedule.backendFallbacksHiddenFromUpperLayer;
    batch.nativeHandleExposedToUpperLayer = schedule.nativeHandleExposedToUpperLayer;
    batch.scheduleReport = schedule;
    batch.diagnostics = schedule.diagnostics;

    if (schedule.diagnosticFailure || !schedule.success) {
        batch.diagnosticFailure = true;
        arp::core::appendDiagnostic(
            batch.diagnostics, "RHI startup probe batch rejected diagnostic schedule");
        return batch;
    }
    if (schedule.waitForReadiness) {
        batch.success = true;
        arp::core::appendDiagnostic(
            batch.diagnostics, "RHI startup probe batch is waiting for readiness");
        return batch;
    }
    if (schedule.runMaintenance) {
        batch.success = true;
        arp::core::appendDiagnostic(
            batch.diagnostics, "RHI startup probe batch requires maintenance");
        return batch;
    }

    batch.canRunProbeBatch = schedule.scheduled;
    batch.success = schedule.scheduled && schedule.dryRunOnly && schedule.v1RenderPathUnaffected
        && schedule.backendFallbacksHiddenFromUpperLayer
        && !schedule.nativeHandleExposedToUpperLayer;
    batch.diagnosticFailure = !batch.success;
    if (!batch.success) {
        arp::core::appendDiagnostic(batch.diagnostics,
            "RHI startup probe batch requires a scheduled diagnostics-only probe");
    }
    return batch;
}

RenderSystemRhiStartupProbeRunReport runRenderSystemRhiStartupProbeBatch(
    const RenderSystemRhiStartupProbeRunnerDesc& desc,
    const RenderSystemRhiStartupProbeBatchReport& batch)
{
    RenderSystemRhiStartupProbeRunReport run;
    run.diagnosticsOnly = desc.diagnosticsOnly && batch.dryRunOnly;
    run.hiddenWindowRequired = batch.hiddenWindowRequired;
    run.v1RenderPathUnaffected = batch.v1RenderPathUnaffected;
    run.waitForReadiness = batch.waitForReadiness;
    run.runMaintenance = batch.runMaintenance;
    run.diagnosticFailure = batch.diagnosticFailure;
    run.nonBlocking = batch.nonBlocking && !desc.allowBlockingWait;
    run.selectedBackend = batch.selectedBackend;
    run.frameCount = batch.frameCount;
    run.firstFrameSerial = batch.firstFrameSerial;
    run.lastFrameSerial = batch.lastFrameSerial;
    run.acquireFrame = batch.acquireFrame;
    run.recordFrame = batch.recordFrame;
    run.submitFrame = batch.submitFrame;
    run.presentFrame = batch.presentFrame;
    run.telemetryRequired = batch.telemetryRequired;
    run.backendFallbacksHiddenFromUpperLayer = batch.backendFallbacksHiddenFromUpperLayer;
    run.nativeHandleExposedToUpperLayer = batch.nativeHandleExposedToUpperLayer;
    run.batchReport = batch;
    run.diagnostics = batch.diagnostics;

    if (batch.diagnosticFailure || !batch.success) {
        run.action = RenderSystemRhiStartupProbeRunAction::DiagnosticFailure;
        run.diagnosticFailure = true;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner rejected diagnostic batch");
        return run;
    }
    if (batch.waitForReadiness) {
        run.success = true;
        run.action = RenderSystemRhiStartupProbeRunAction::WaitForReadiness;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner is waiting for readiness");
        return run;
    }
    if (batch.runMaintenance) {
        run.success = true;
        run.action = RenderSystemRhiStartupProbeRunAction::RunMaintenance;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner requires maintenance");
        return run;
    }

    bool canRun = batch.canRunProbeBatch;
    if (!desc.diagnosticsOnly) {
        canRun = false;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner requires diagnostics-only execution");
    }
    if (batch.hiddenWindowRequired && !desc.hiddenWindowAvailable) {
        canRun = false;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner requires a hidden window");
    }
    if (desc.allowBlockingWait || !batch.nonBlocking) {
        canRun = false;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner rejected blocking wait");
    }
    if (batch.frameCount == 0u || batch.frameCount > desc.maxFrameCount) {
        canRun = false;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner rejected frame count");
    }
    if (!batch.backendFallbacksHiddenFromUpperLayer) {
        canRun = false;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner rejected visible backend fallback details");
    }
    if (batch.nativeHandleExposedToUpperLayer) {
        canRun = false;
        arp::core::appendDiagnostic(
            run.diagnostics, "RHI startup probe runner rejected native handle exposure");
    }

    run.success = canRun;
    run.executed = canRun;
    run.completed = canRun;
    run.completedFrameCount = canRun ? batch.frameCount : 0u;
    run.hiddenWindowUsed = canRun && batch.hiddenWindowRequired;
    run.diagnosticFailure = !canRun;
    run.action = canRun ? RenderSystemRhiStartupProbeRunAction::RunHiddenWindowProbe
                        : RenderSystemRhiStartupProbeRunAction::DiagnosticFailure;
    return run;
}

} // namespace arp::platform
