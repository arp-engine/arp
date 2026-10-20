#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "platform/render_system_rhi_startup_readiness.h"

namespace arp::platform {

struct RenderSystemRhiProductionCutoverEvidence {
    bool nullHiddenFrameSmokePassed = false;
    bool vulkanHiddenWindowSmokePassed = false;
    bool webGpuBrowserRuntimeSmokePassed = false;
    bool requiredPlatformSmokesPassed = false;
    bool nativeHeaderIsolationVerified = false;
    bool hotPathBounded = false;
    bool nonBlocking = true;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
};

struct RenderSystemRhiActiveFrameLoopDesc {
    bool enabled = false;
    bool guardEnabled = false;
    bool requestProductionDefault = false;
    bool v1FallbackAvailable = true;
    bool requireStartupReadiness = true;
    bool requireStartupSmokeCoverage = false;
    RenderSystemRhiProductionCutoverEvidence productionCutover;
    platform::RenderSystemFrameLoopRuntimeBridgeDesc runtime;
};

struct RenderSystemRhiActiveFrameLoopInput {
    renderer::RendererRhiFramePlanBuildResult rendererFramePlan;
    platform::PlatformLifecycleState lifecycleState;
    platform::RenderSystemFrameLoopState renderSystemState;
    RenderSystemRhiStartupReadinessReport startupReadiness;
    RenderSystemRhiStartupTelemetryReport startupTelemetry;
};

struct RenderSystemRhiActiveFrameLoopReport {
    bool success = false;
    bool enabled = false;
    bool guardEnabled = false;
    bool productionDefaultRequested = false;
    bool productionCutoverAccepted = false;
    bool promotedToProductionDefault = false;
    bool activeMode = false;
    bool v1RenderPathAvailable = true;
    bool fallbackToV1 = true;
    bool usedRhiFrameLoop = false;
    bool requireStartupReadiness = true;
    bool requireStartupSmokeCoverage = false;
    bool startupReadinessAccepted = false;
    bool startupSmokeCoverageAccepted = false;
    bool startupProbeExecuted = false;
    bool startupProbeCompleted = false;
    bool startupHiddenWindowUsed = false;
    bool hiddenWindowValidated = false;
    bool nonBlocking = true;
    RenderSystemRhiStartupReadinessStatus startupReadinessStatus =
        RenderSystemRhiStartupReadinessStatus::DiagnosticFailure;
    platform::RenderSystemFrameLoopAction action =
        platform::RenderSystemFrameLoopAction::DiagnosticFailure;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool skipFrame = false;
    bool runMaintenance = false;
    bool releaseCurrentFrame = false;
    bool collectCompletedSubmissions = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool releaseTransientResources = false;
    bool memoryBudgetRefreshRequired = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    platform::RenderSystemFrameLoopRuntimePlan runtimePlan;
    std::pmr::string diagnostics;
};

class ARP_API RenderSystemRhiActiveFrameLoopHook {
public:
    explicit RenderSystemRhiActiveFrameLoopHook(const RenderSystemRhiActiveFrameLoopDesc& desc);

    RenderSystemRhiActiveFrameLoopReport evaluate(
        const RenderSystemRhiActiveFrameLoopInput& input) const;

private:
    RenderSystemRhiActiveFrameLoopDesc desc_;
};

ARP_API RenderSystemRhiActiveFrameLoopReport evaluateRenderSystemRhiActiveFrameLoop(
    const RenderSystemRhiActiveFrameLoopDesc& desc,
    const RenderSystemRhiActiveFrameLoopInput& input);

} // namespace arp::platform
