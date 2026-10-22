#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "platform/render_system_rhi_dry_run.h"

namespace arp::platform {

enum class RenderSystemRhiStartupReadinessStatus {
    ContractComplete,
    PrototypeComplete,
    BackendValidated,
    PlatformValidated,
    DiagnosticFailure
};

struct RenderSystemRhiStartupTelemetryReport {
    bool success = false;
    bool probeExecuted = false;
    bool probeCompleted = false;
    bool diagnosticsOnly = true;
    bool hiddenWindowRequired = true;
    bool hiddenWindowUsed = false;
    bool v1RenderPathUnaffected = true;
    bool diagnosticFailure = false;
    bool nonBlocking = true;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameCount = 0;
    std::uint32_t completedFrameCount = 0;
    std::uint64_t firstFrameSerial = 0;
    std::uint64_t lastFrameSerial = 0;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RenderSystemRhiStartupProbeRunReport runReport;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiStartupReadinessReport {
    RenderSystemRhiStartupReadinessStatus status =
        RenderSystemRhiStartupReadinessStatus::DiagnosticFailure;
    bool canBeginFormalRhiRefactor = false;
    bool contractComplete = false;
    bool prototypeComplete = false;
    bool backendValidated = false;
    bool platformValidated = false;
    bool diagnosticFailure = true;
    bool hiddenWindowValidated = false;
    bool diagnosticsOnly = true;
    bool nonBlocking = true;
    bool v1RenderPathUnaffected = true;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RenderSystemRhiStartupTelemetryReport telemetryReport;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiFormalRefactorStartGateReport {
    bool success = false;
    bool allowFormalRefactorStart = false;
    bool allowRendererMigration = false;
    bool allowFrameLoopMigration = false;
    bool requireTelemetry = false;
    bool requireHiddenWindowValidation = true;
    RenderSystemRhiStartupReadinessStatus status =
        RenderSystemRhiStartupReadinessStatus::DiagnosticFailure;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    bool diagnosticsOnly = true;
    bool nonBlocking = true;
    bool v1RenderPathUnaffected = true;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RenderSystemRhiStartupReadinessReport readinessReport;
    std::pmr::string diagnostics;
};

ARP_API RenderSystemRhiStartupTelemetryReport collectRenderSystemRhiStartupTelemetry(
    const RenderSystemRhiStartupProbeRunReport& run);

ARP_API RenderSystemRhiStartupReadinessReport evaluateRenderSystemRhiStartupReadiness(
    const RenderSystemRhiStartupTelemetryReport& telemetry);

ARP_API RenderSystemRhiFormalRefactorStartGateReport evaluateRenderSystemRhiFormalRefactorStartGate(
    const RenderSystemRhiStartupReadinessReport& readiness);

} // namespace arp::platform
