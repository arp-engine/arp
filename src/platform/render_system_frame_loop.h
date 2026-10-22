#pragma once
#include "core/memory/cpu_allocator.h"
#include "renderer/deployment/renderer_frame_lifecycle.h"

#include <cstdint>
#include <string>

namespace arp::platform {

enum class RendererFrameLoopAction {
    BeginFrame,
    SkipFrame,
    MaintenanceRequired,
    DiagnosticFailure
};

struct RendererFrameLoopBridgeDesc {
    renderer::deployment::RendererFrameLifecycleBridgeDesc lifecycle;
    std::uint32_t logicalFrameSlots = 2;
};

struct RendererFrameLoopDecision {
    bool success = false;
    bool canStartFrame = false;
    RendererFrameLoopAction action = RendererFrameLoopAction::DiagnosticFailure;
    rhi::Backend selectedBackend = rhi::Backend::Null;
    std::uint32_t frameSlot = 0;
    bool shouldAcquireFrame = false;
    bool shouldRecordFrame = false;
    bool shouldSubmitFrame = false;
    bool shouldPresentFrame = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool releaseTransientResources = false;
    bool memoryBudgetRefreshRequired = false;
    bool telemetryRequired = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    renderer::deployment::RendererFrameLifecycleDecision lifecycle;
    std::pmr::string diagnostics;
};

class RendererFrameLoopBridge {
public:
    explicit RendererFrameLoopBridge(const RendererFrameLoopBridgeDesc& desc);

    RendererFrameLoopDecision evaluate(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState) const;

private:
    RendererFrameLoopBridgeDesc desc_;
};

RendererFrameLoopDecision evaluateRendererFrameLoopBridge(const RendererFrameLoopBridgeDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState);

enum class RenderSystemFrameLoopAction {
    BeginFrame,
    SkipFrame,
    MaintenanceRequired,
    DiagnosticFailure
};

struct RenderSystemFrameLoopAdapterDesc {
    std::uint32_t logicalFrameSlots = 2;
};

struct RenderSystemFrameLoopState {
    bool renderPipelineReady = false;
    bool frameInProgress = false;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint64_t nextFrameSerial = 0;
};

struct RenderSystemFrameLoopPlan {
    bool success = false;
    RenderSystemFrameLoopAction action = RenderSystemFrameLoopAction::DiagnosticFailure;
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
    std::pmr::string diagnostics;
};

class RenderSystemFrameLoopAdapter {
public:
    explicit RenderSystemFrameLoopAdapter(const RenderSystemFrameLoopAdapterDesc& desc);

    RenderSystemFrameLoopPlan plan(
        const RendererFrameLoopDecision& frameLoop, const RenderSystemFrameLoopState& state) const;

private:
    RenderSystemFrameLoopAdapterDesc desc_;
};

RenderSystemFrameLoopPlan planRenderSystemFrameLoop(const RenderSystemFrameLoopAdapterDesc& desc,
    const RendererFrameLoopDecision& frameLoop,
    const RenderSystemFrameLoopState& state);

struct RenderSystemFrameLoopRuntimeBridgeDesc {
    RendererFrameLoopBridgeDesc rendererFrameLoop;
    RenderSystemFrameLoopAdapterDesc renderSystem;
};

struct RenderSystemFrameLoopRuntimePlan {
    bool success = false;
    RenderSystemFrameLoopAction action = RenderSystemFrameLoopAction::DiagnosticFailure;
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
    RendererFrameLoopDecision rendererFrameLoop;
    RenderSystemFrameLoopPlan renderSystemPlan;
    std::pmr::string diagnostics;
};

class RenderSystemFrameLoopRuntimeBridge {
public:
    explicit RenderSystemFrameLoopRuntimeBridge(const RenderSystemFrameLoopRuntimeBridgeDesc& desc);

    RenderSystemFrameLoopRuntimePlan plan(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState,
        const RenderSystemFrameLoopState& renderSystemState) const;

private:
    RenderSystemFrameLoopRuntimeBridgeDesc desc_;
};

RenderSystemFrameLoopRuntimePlan planRenderSystemFrameLoopRuntime(
    const RenderSystemFrameLoopRuntimeBridgeDesc& desc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState,
    const RenderSystemFrameLoopState& renderSystemState);

} // namespace arp::platform
