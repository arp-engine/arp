#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"
#include "rhi/semantics/frame_lifecycle_transition.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

#include <utility>

namespace arp::rhi::semantics {

WebGpuCanvasLifecycleBridge::WebGpuCanvasLifecycleBridge(rhi::WebGpuRuntimeProfile profile)
    : profile_(std::move(profile))
{}

NativePlatformLifecycleLoweringDecision WebGpuCanvasLifecycleBridge::lower(
    const platform::PlatformLifecycleState& state, platform::PlatformLifecycleEvent event) const
{
    return lowerWebGpuCanvasLifecycleEvent(profile_, state, event);
}

platform::PlatformLifecycleState makeWebGpuCanvasLifecycleState(
    const rhi::WebGpuRuntimeProfile& profile)
{
    platform::PlatformLifecycleState state;
    state.surfaceKind = platform::PlatformSurfaceKind::WebCanvas;
    state.logicalFrameSlots = profile.logicalFrameSlots;
    state.running = profile.browserRuntime;
    state.paused = false;
    state.backgrounded = false;
    state.surfaceValid = profile.canvasSurface
        && profile.status != rhi::WebGpuRuntimeProfileStatus::DiagnosticFailure;
    state.acquireAllowed = state.running && state.surfaceValid;
    state.presentAllowed = state.acquireAllowed;
    state.swapchainRebuildRequired = false;
    state.releaseSurfaceResources = false;
    state.releaseTransientResources = false;
    state.memoryBudgetRefreshRequired = false;
    state.nativeHandleExposedToUpperLayer = false;
    return state;
}

NativePlatformLifecycleLoweringDecision lowerWebGpuCanvasLifecycleEvent(
    const rhi::WebGpuRuntimeProfile& profile,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event)
{
    const auto capabilityBridge = buildWebGpuRuntimeCapabilityContract(profile);
    auto migratedState = state;
    semantics::applyEvent(migratedState, event);
    auto decision = lowerPlatformLifecycleEvent(capabilityBridge.contract, migratedState, event);

    if (!capabilityBridge.success) {
        decision.success = false;
        decision.fallbackMode = rhi::FallbackMode::DiagnosticFailure;
        arp::core::appendDiagnostic(decision.diagnostics, capabilityBridge.diagnostics);
        decision.state.surfaceValid = false;
        decision.state.acquireAllowed = false;
        decision.state.presentAllowed = false;
        decision.state.nativeHandleExposedToUpperLayer = false;
    }

    return decision;
}

} // namespace arp::rhi::semantics
