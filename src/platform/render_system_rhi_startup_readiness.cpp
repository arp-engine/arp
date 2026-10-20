#include "platform/render_system_rhi_startup_readiness.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"

namespace arp::platform {

RenderSystemRhiStartupTelemetryReport collectRenderSystemRhiStartupTelemetry(
    const RenderSystemRhiStartupProbeRunReport& run)
{
    RenderSystemRhiStartupTelemetryReport telemetry;
    telemetry.probeExecuted = run.executed;
    telemetry.probeCompleted = run.completed;
    telemetry.diagnosticsOnly = run.diagnosticsOnly;
    telemetry.hiddenWindowRequired = run.hiddenWindowRequired;
    telemetry.hiddenWindowUsed = run.hiddenWindowUsed;
    telemetry.v1RenderPathUnaffected = run.v1RenderPathUnaffected;
    telemetry.diagnosticFailure = run.diagnosticFailure;
    telemetry.nonBlocking = run.nonBlocking;
    telemetry.selectedBackend = run.selectedBackend;
    telemetry.frameCount = run.frameCount;
    telemetry.completedFrameCount = run.completedFrameCount;
    telemetry.firstFrameSerial = run.firstFrameSerial;
    telemetry.lastFrameSerial = run.lastFrameSerial;
    telemetry.acquireFrame = run.acquireFrame;
    telemetry.recordFrame = run.recordFrame;
    telemetry.submitFrame = run.submitFrame;
    telemetry.presentFrame = run.presentFrame;
    telemetry.telemetryRequired = run.telemetryRequired;
    telemetry.backendFallbacksHiddenFromUpperLayer = run.backendFallbacksHiddenFromUpperLayer;
    telemetry.nativeHandleExposedToUpperLayer = run.nativeHandleExposedToUpperLayer;
    telemetry.runReport = run;
    telemetry.diagnostics = run.diagnostics;

    bool valid = run.success && run.executed && run.completed;
    if (!run.success) {
        arp::core::appendDiagnostic(
            telemetry.diagnostics, "RHI startup telemetry requires a successful startup probe run");
    }
    if (!run.executed || !run.completed) {
        valid = false;
        arp::core::appendDiagnostic(telemetry.diagnostics,
            "RHI startup telemetry requires executed and completed probe frames");
    }
    if (!run.diagnosticsOnly) {
        valid = false;
        arp::core::appendDiagnostic(
            telemetry.diagnostics, "RHI startup telemetry requires diagnostics-only execution");
    }
    if (run.hiddenWindowRequired && !run.hiddenWindowUsed) {
        valid = false;
        arp::core::appendDiagnostic(
            telemetry.diagnostics, "RHI startup telemetry requires hidden-window validation");
    }
    if (!run.v1RenderPathUnaffected) {
        valid = false;
        arp::core::appendDiagnostic(telemetry.diagnostics,
            "RHI startup telemetry requires v1 render path to remain unaffected");
    }
    if (run.diagnosticFailure) {
        valid = false;
    }
    if (!run.nonBlocking) {
        valid = false;
        arp::core::appendDiagnostic(telemetry.diagnostics,
            "RHI startup telemetry rejected blocking startup probe execution");
    }
    if (!run.acquireFrame || !run.recordFrame || !run.submitFrame || !run.presentFrame) {
        valid = false;
        arp::core::appendDiagnostic(telemetry.diagnostics,
            "RHI startup telemetry requires acquire, record, submit, and present intent");
    }
    if (!run.backendFallbacksHiddenFromUpperLayer) {
        valid = false;
        arp::core::appendDiagnostic(telemetry.diagnostics,
            "RHI startup telemetry rejected visible backend fallback details");
    }
    if (run.nativeHandleExposedToUpperLayer) {
        valid = false;
        arp::core::appendDiagnostic(
            telemetry.diagnostics, "RHI startup telemetry rejected native handle exposure");
    }

    telemetry.success = valid;
    telemetry.diagnosticFailure = run.diagnosticFailure || !valid;
    return telemetry;
}

RenderSystemRhiStartupReadinessReport evaluateRenderSystemRhiStartupReadiness(
    const RenderSystemRhiStartupTelemetryReport& telemetry)
{
    RenderSystemRhiStartupReadinessReport readiness;
    readiness.hiddenWindowValidated = telemetry.probeExecuted && telemetry.probeCompleted
        && (!telemetry.hiddenWindowRequired || telemetry.hiddenWindowUsed);
    readiness.diagnosticsOnly = telemetry.diagnosticsOnly;
    readiness.nonBlocking = telemetry.nonBlocking;
    readiness.v1RenderPathUnaffected = telemetry.v1RenderPathUnaffected;
    readiness.selectedBackend = telemetry.selectedBackend;
    readiness.acquireFrame = telemetry.acquireFrame;
    readiness.recordFrame = telemetry.recordFrame;
    readiness.submitFrame = telemetry.submitFrame;
    readiness.presentFrame = telemetry.presentFrame;
    readiness.telemetryRequired = telemetry.telemetryRequired;
    readiness.backendFallbacksHiddenFromUpperLayer = telemetry.backendFallbacksHiddenFromUpperLayer;
    readiness.nativeHandleExposedToUpperLayer = telemetry.nativeHandleExposedToUpperLayer;
    readiness.telemetryReport = telemetry;
    readiness.diagnostics = telemetry.diagnostics;

    const bool boundaryFailure = !telemetry.diagnosticsOnly || !telemetry.nonBlocking
        || !telemetry.v1RenderPathUnaffected || !telemetry.backendFallbacksHiddenFromUpperLayer
        || telemetry.nativeHandleExposedToUpperLayer;
    readiness.diagnosticFailure = telemetry.diagnosticFailure || !telemetry.success
        || boundaryFailure || !readiness.hiddenWindowValidated;

    if (readiness.diagnosticFailure) {
        readiness.status = RenderSystemRhiStartupReadinessStatus::DiagnosticFailure;
        arp::core::appendDiagnostic(
            readiness.diagnostics, "RHI startup readiness rejected incomplete startup telemetry");
        return readiness;
    }

    readiness.contractComplete = true;
    readiness.prototypeComplete = true;
    readiness.backendValidated = false;
    readiness.platformValidated = false;
    readiness.canBeginFormalRhiRefactor = true;
    readiness.status = RenderSystemRhiStartupReadinessStatus::PrototypeComplete;
    return readiness;
}

RenderSystemRhiFormalRefactorStartGateReport evaluateRenderSystemRhiFormalRefactorStartGate(
    const RenderSystemRhiStartupReadinessReport& readiness)
{
    RenderSystemRhiFormalRefactorStartGateReport gate;
    gate.requireTelemetry = readiness.telemetryRequired;
    gate.requireHiddenWindowValidation = !readiness.hiddenWindowValidated;
    gate.status = readiness.status;
    gate.selectedBackend = readiness.selectedBackend;
    gate.diagnosticsOnly = readiness.diagnosticsOnly;
    gate.nonBlocking = readiness.nonBlocking;
    gate.v1RenderPathUnaffected = readiness.v1RenderPathUnaffected;
    gate.backendFallbacksHiddenFromUpperLayer = readiness.backendFallbacksHiddenFromUpperLayer;
    gate.nativeHandleExposedToUpperLayer = readiness.nativeHandleExposedToUpperLayer;
    gate.readinessReport = readiness;
    gate.diagnostics = readiness.diagnostics;

    gate.success = readiness.canBeginFormalRhiRefactor && !readiness.diagnosticFailure
        && readiness.prototypeComplete && readiness.hiddenWindowValidated
        && readiness.backendFallbacksHiddenFromUpperLayer
        && !readiness.nativeHandleExposedToUpperLayer;
    gate.allowFormalRefactorStart = gate.success;
    gate.allowRendererMigration = false;
    gate.allowFrameLoopMigration = false;
    if (!gate.success) {
        arp::core::appendDiagnostic(
            gate.diagnostics, "RHI formal refactor start gate rejected startup readiness");
    }
    return gate;
}

} // namespace arp::platform
