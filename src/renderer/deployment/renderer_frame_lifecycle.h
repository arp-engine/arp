#pragma once

#include "core/cpu_allocator.h"
#include "renderer/deployment/renderer_backend_deployment.h"
#include "renderer/render_graph/rhi_frame_plan.h"

#include <vector>

namespace arp::renderer::deployment {

struct RendererFrameStartGateDesc {
    std::uint32_t logicalFrameSlots = 2;
    bool requireAcquireAllowed = true;
    bool requirePresentAllowed = true;
};

struct RendererFrameStartDecision {
    bool success = false;
    bool canStartFrame = false;
    rhi::Backend backend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    bool telemetryRequired = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool releaseTransientResources = false;
    bool memoryBudgetRefreshRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class RendererFrameStartGate {
public:
    explicit RendererFrameStartGate(const RendererFrameStartGateDesc& desc);

    RendererFrameStartDecision evaluate(const RendererBackendDeploymentPlan& deployment,
        const render_graph::RhiFramePlan& framePlan,
        const platform::PlatformLifecycleState& lifecycleState) const;

private:
    RendererFrameStartGateDesc desc_;
};

RendererFrameStartDecision evaluateRendererFrameStart(const RendererFrameStartGateDesc& desc,
    const RendererBackendDeploymentPlan& deployment,
    const render_graph::RhiFramePlan& framePlan,
    const platform::PlatformLifecycleState& lifecycleState);

struct RendererFrameLifecyclePlannerDesc {
    RendererBackendDeploymentPlannerDesc deployment;
    RendererFrameStartGateDesc frameStart;
};

struct RendererFrameLifecyclePlan {
    bool success = false;
    bool canStartFrame = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RendererBackendDeploymentPlan deployment;
    RendererFrameStartDecision frameStart;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class RendererFrameLifecyclePlanner {
public:
    explicit RendererFrameLifecyclePlanner(const RendererFrameLifecyclePlannerDesc& desc);

    RendererFrameLifecyclePlan plan(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState) const;

private:
    RendererFrameLifecyclePlannerDesc desc_;
};

RendererFrameLifecyclePlan planRendererFrameLifecycle(const RendererFrameLifecyclePlannerDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState);

struct RendererFrameLifecycleTelemetryCollectorDesc {
    std::uint32_t maxDiagnostics = 8;
};

struct RendererFrameLifecycleTelemetryReport {
    bool success = false;
    bool canStartFrame = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint32_t candidateCount = 0;
    std::uint32_t diagnosticCandidateCount = 0;
    std::uint32_t nativeFeatureCount = 0;
    std::uint32_t emulatedFeatureCount = 0;
    std::uint32_t cpuReplayFeatureCount = 0;
    std::uint32_t degradedFeatureCount = 0;
    std::uint32_t deploymentDiagnosticFailureCount = 0;
    std::uint32_t frameStartDiagnosticFailureCount = 0;
    std::uint32_t diagnosticFailureCount = 0;
    std::uint32_t recordedDiagnosticCount = 0;
    std::uint32_t droppedDiagnosticCount = 0;
    bool diagnosticCapacityExhausted = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool releaseTransientResources = false;
    bool memoryBudgetRefreshRequired = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class RendererFrameLifecycleTelemetryCollector {
public:
    explicit RendererFrameLifecycleTelemetryCollector(
        const RendererFrameLifecycleTelemetryCollectorDesc& desc);

    void record(const RendererFrameLifecyclePlan& lifecycle);
    RendererFrameLifecycleTelemetryReport buildReport() const;

private:
    void recordDiagnostic(const core::pmr::String& diagnostics);

    RendererFrameLifecycleTelemetryCollectorDesc desc_;
    RendererFrameLifecycleTelemetryReport report_;
    core::pmr::Vector<core::pmr::String> diagnostics_{&core::defaultCpuMemoryResource()};
    std::uint32_t droppedDiagnosticCount_ = 0;
    bool diagnosticCapacityExhausted_ = false;
    bool lifecycleRecorded_ = false;
};

RendererFrameLifecycleTelemetryReport buildRendererFrameLifecycleTelemetryReport(
    const RendererFrameLifecyclePlan& lifecycle, std::uint32_t maxDiagnostics = 8);

enum class RendererFrameLifecycleReadinessStatus {
    FrameStartAllowed,
    FrameStartBlocked,
    DiagnosticFailure
};

struct RendererFrameLifecycleReadinessReport {
    bool success = false;
    RendererFrameLifecycleReadinessStatus status =
        RendererFrameLifecycleReadinessStatus::DiagnosticFailure;
    bool canStartFrame = false;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    std::uint32_t candidateCount = 0;
    std::uint32_t degradedFeatureCount = 0;
    std::uint32_t diagnosticFailureCount = 0;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class RendererFrameLifecycleReadinessGate {
public:
    RendererFrameLifecycleReadinessReport evaluate(
        const RendererFrameLifecycleTelemetryReport& telemetry) const;
};

RendererFrameLifecycleReadinessReport evaluateRendererFrameLifecycleReadiness(
    const RendererFrameLifecycleTelemetryReport& telemetry);

struct RendererFrameLifecycleBridgeDesc {
    RendererFrameLifecyclePlannerDesc planner;
    std::uint32_t maxTelemetryDiagnostics = 8;
};

struct RendererFrameLifecycleDecision {
    bool success = false;
    bool canStartFrame = false;
    RendererFrameLifecycleReadinessStatus status =
        RendererFrameLifecycleReadinessStatus::DiagnosticFailure;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    RendererFrameLifecyclePlan lifecycle;
    RendererFrameLifecycleTelemetryReport telemetry;
    RendererFrameLifecycleReadinessReport readiness;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class RendererFrameLifecycleBridge {
public:
    explicit RendererFrameLifecycleBridge(const RendererFrameLifecycleBridgeDesc& desc);

    RendererFrameLifecycleDecision evaluate(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState) const;

private:
    RendererFrameLifecycleBridgeDesc desc_;
};

RendererFrameLifecycleDecision evaluateRendererFrameLifecycleBridge(
    const RendererFrameLifecycleBridgeDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState);

} // namespace arp::renderer::deployment
