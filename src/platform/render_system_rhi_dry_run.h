#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "render_system_frame_loop.h"

#include <string>

namespace arp::platform {

struct RenderSystemRhiFrameLoopDryRunDesc {
    bool enabled = false;
    platform::RenderSystemFrameLoopRuntimeBridgeDesc runtime;
};

struct RenderSystemRhiFrameLoopDryRunInput {
    renderer::RendererRhiFramePlanBuildResult rendererFramePlan;
    platform::PlatformLifecycleState lifecycleState;
    platform::RenderSystemFrameLoopState renderSystemState;
};

struct RenderSystemRhiFrameLoopDryRunReport {
    bool success = false;
    bool enabled = false;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
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

struct RenderSystemRhiFrameLoopDryRunCapabilityState {
    bool available = true;
    bool enabled = false;
    bool lastEvaluationSucceeded = false;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    platform::RenderSystemFrameLoopAction action =
        platform::RenderSystemFrameLoopAction::DiagnosticFailure;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiFrameLoopDryRunSmokeDesc {
    bool requireBeginFrame = false;
    bool allowSkipFrame = true;
    bool allowMaintenance = true;
};

struct RenderSystemRhiFrameLoopDryRunSmokeReport {
    bool success = false;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    bool frameWouldBegin = false;
    bool frameWouldSkip = false;
    bool maintenanceWouldRun = false;
    bool diagnosticFailure = false;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RenderSystemRhiFrameLoopDryRunReport dryRunReport;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiFrameLoopDryRunSmokeCapabilityState {
    bool available = true;
    bool lastEvaluationSucceeded = false;
    bool readyForStartupProbe = false;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    bool frameWouldBegin = false;
    bool frameWouldSkip = false;
    bool maintenanceWouldRun = false;
    bool diagnosticFailure = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiStartupProbeReport {
    bool success = false;
    bool readyForStartup = false;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    bool frameWouldBegin = false;
    bool frameWouldSkip = false;
    bool maintenanceWouldRun = false;
    bool diagnosticFailure = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiStartupProbeCapabilityState {
    bool available = true;
    bool lastEvaluationSucceeded = false;
    bool readyForStartup = false;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    bool frameWouldBegin = false;
    bool frameWouldSkip = false;
    bool maintenanceWouldRun = false;
    bool diagnosticFailure = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::pmr::string diagnostics;
};

enum class RenderSystemRhiStartupGateAction {
    RunStartupProbe,
    WaitForReadiness,
    RunMaintenance,
    DiagnosticFailure
};

struct RenderSystemRhiStartupGateReport {
    bool success = false;
    bool allowStartup = false;
    bool available = true;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    bool frameWouldBegin = false;
    bool frameWouldSkip = false;
    bool maintenanceWouldRun = false;
    bool diagnosticFailure = false;
    RenderSystemRhiStartupGateAction action = RenderSystemRhiStartupGateAction::DiagnosticFailure;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiStartupProbeScheduleDesc {
    bool requireHiddenWindow = true;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t probeFrameCount = 1;
    std::uint32_t maxProbeFrameCount = 2;
};

struct RenderSystemRhiStartupProbeScheduleReport {
    bool success = false;
    bool scheduled = false;
    bool hiddenWindowRequired = true;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    bool waitForReadiness = false;
    bool runMaintenance = false;
    bool diagnosticFailure = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t probeFrameCount = 0;
    std::uint32_t maxProbeFrameCount = 2;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RenderSystemRhiStartupGateReport gateReport;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiStartupProbeBatchReport {
    bool success = false;
    bool canRunProbeBatch = false;
    bool hiddenWindowRequired = true;
    bool dryRunOnly = true;
    bool v1RenderPathUnaffected = true;
    bool waitForReadiness = false;
    bool runMaintenance = false;
    bool diagnosticFailure = false;
    bool nonBlocking = true;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t logicalFrameSlots = 2;
    std::uint32_t frameCount = 0;
    std::uint64_t firstFrameSerial = 0;
    std::uint64_t lastFrameSerial = 0;
    bool acquireFrame = false;
    bool recordFrame = false;
    bool submitFrame = false;
    bool presentFrame = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RenderSystemRhiStartupProbeScheduleReport scheduleReport;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiStartupProbeRunnerDesc {
    bool diagnosticsOnly = true;
    bool hiddenWindowAvailable = false;
    bool allowBlockingWait = false;
    std::uint32_t maxFrameCount = 2;
};

enum class RenderSystemRhiStartupProbeRunAction {
    RunHiddenWindowProbe,
    WaitForReadiness,
    RunMaintenance,
    DiagnosticFailure
};

struct RenderSystemRhiStartupProbeRunReport {
    bool success = false;
    bool executed = false;
    bool completed = false;
    bool diagnosticsOnly = true;
    bool hiddenWindowRequired = true;
    bool hiddenWindowUsed = false;
    bool v1RenderPathUnaffected = true;
    bool waitForReadiness = false;
    bool runMaintenance = false;
    bool diagnosticFailure = false;
    bool nonBlocking = true;
    RenderSystemRhiStartupProbeRunAction action =
        RenderSystemRhiStartupProbeRunAction::DiagnosticFailure;
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
    RenderSystemRhiStartupProbeBatchReport batchReport;
    std::pmr::string diagnostics;
};

class ARP_API RenderSystemRhiFrameLoopDryRunHook {
public:
    explicit RenderSystemRhiFrameLoopDryRunHook(const RenderSystemRhiFrameLoopDryRunDesc& desc);

    RenderSystemRhiFrameLoopDryRunReport evaluate(
        const RenderSystemRhiFrameLoopDryRunInput& input) const;

private:
    RenderSystemRhiFrameLoopDryRunDesc desc_;
};

ARP_API RenderSystemRhiFrameLoopDryRunReport evaluateRenderSystemRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunDesc& desc,
    const RenderSystemRhiFrameLoopDryRunInput& input);

ARP_API RenderSystemRhiFrameLoopDryRunCapabilityState
summarizeRenderSystemRhiFrameLoopDryRunCapability(const RenderSystemRhiFrameLoopDryRunDesc& desc,
    const RenderSystemRhiFrameLoopDryRunReport& report);

ARP_API RenderSystemRhiFrameLoopDryRunSmokeReport evaluateRenderSystemRhiFrameLoopDryRunSmoke(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& desc,
    const RenderSystemRhiFrameLoopDryRunReport& report);

ARP_API RenderSystemRhiFrameLoopDryRunSmokeCapabilityState
summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(
    const RenderSystemRhiFrameLoopDryRunSmokeReport& report);

ARP_API RenderSystemRhiStartupProbeReport evaluateRenderSystemRhiStartupProbe(
    const RenderSystemRhiFrameLoopDryRunSmokeCapabilityState& smoke);

ARP_API RenderSystemRhiStartupProbeCapabilityState summarizeRenderSystemRhiStartupProbeCapability(
    const RenderSystemRhiStartupProbeReport& report);

ARP_API RenderSystemRhiStartupGateReport evaluateRenderSystemRhiStartupGate(
    const RenderSystemRhiStartupProbeCapabilityState& probe);

ARP_API RenderSystemRhiStartupProbeScheduleReport scheduleRenderSystemRhiStartupProbe(
    const RenderSystemRhiStartupProbeScheduleDesc& desc,
    const RenderSystemRhiStartupGateReport& gate);

ARP_API RenderSystemRhiStartupProbeBatchReport planRenderSystemRhiStartupProbeBatch(
    const RenderSystemRhiStartupProbeScheduleReport& schedule);

ARP_API RenderSystemRhiStartupProbeRunReport runRenderSystemRhiStartupProbeBatch(
    const RenderSystemRhiStartupProbeRunnerDesc& desc,
    const RenderSystemRhiStartupProbeBatchReport& batch);

} // namespace arp::platform
