#include "rhi/semantics/native_platform_lifecycle_lowering.h"

#include <utility>

namespace arp::rhi::semantics {

namespace {

void addUnique(
    NativePlatformLifecycleLoweringDecision& decision, NativePlatformLifecycleSemantic semantic)
{
    if (!hasNativePlatformLifecycleSemantic(decision, semantic)) {
        decision.semantics.push_back(semantic);
    }
}

bool hasDrawableSurface(platform::PlatformSurfaceKind kind)
{
    return rhi::FrameLifecycleAccess::hasDrawableSurface(kind);
}

NativePlatformLifecycleLoweringDecision makeDecision(
    const NativeBackendCapabilityContract& contract, const platform::PlatformLifecycleState& state)
{
    NativePlatformLifecycleLoweringDecision decision;
    decision.success = true;
    decision.backend = contract.backend;
    decision.fallbackMode = rhi::FallbackMode::Native;
    decision.state = state;
    decision.state.nativeHandleExposedToUpperLayer = false;
    return decision;
}

NativePlatformLifecycleLoweringDecision makeFailure(const NativeBackendCapabilityContract& contract,
    const platform::PlatformLifecycleState& state,
    const char* diagnostics)
{
    auto decision = makeDecision(contract, state);
    decision.success = false;
    decision.fallbackMode = rhi::FallbackMode::DiagnosticFailure;
    decision.diagnostics = diagnostics;
    rhi::FrameLifecycleAccess::refresh(decision.state);
    return decision;
}

void markDoubleBuffered(NativePlatformLifecycleLoweringDecision& decision)
{
    addUnique(decision, NativePlatformLifecycleSemantic::DoubleBufferedFrameSlotsPreserved);
}

void markSurfaceRelease(NativePlatformLifecycleLoweringDecision& decision)
{
    decision.state.releaseSurfaceResources = true;
    addUnique(decision, NativePlatformLifecycleSemantic::ReleaseSurfaceFrameResources);
}

void markTransientRelease(NativePlatformLifecycleLoweringDecision& decision)
{
    decision.state.releaseTransientResources = true;
    addUnique(decision, NativePlatformLifecycleSemantic::ReleaseTransientResources);
}

void markMemoryBudgetRefresh(NativePlatformLifecycleLoweringDecision& decision)
{
    decision.state.memoryBudgetRefreshRequired = true;
    addUnique(decision, NativePlatformLifecycleSemantic::MemoryBudgetRefresh);
}

void invalidateSwapchain(NativePlatformLifecycleLoweringDecision& decision)
{
    decision.state.swapchainRebuildRequired = true;
}

void addSurfaceCreatedSemantics(NativePlatformLifecycleLoweringDecision& decision)
{
    switch (decision.state.surfaceKind) {
    case platform::PlatformSurfaceKind::AndroidSurface:
        addUnique(decision, NativePlatformLifecycleSemantic::AndroidSurfaceInvalidatesSwapchain);
        break;
    case platform::PlatformSurfaceKind::IosLayer:
        addUnique(decision, NativePlatformLifecycleSemantic::IosLayerInvalidatesDrawableResources);
        break;
    case platform::PlatformSurfaceKind::WebCanvas:
        addUnique(decision, NativePlatformLifecycleSemantic::WebCanvasConfigureSurface);
        break;
    case platform::PlatformSurfaceKind::DesktopWindow:
        addUnique(decision, NativePlatformLifecycleSemantic::DesktopResizeRebuildsSwapchain);
        break;
    case platform::PlatformSurfaceKind::Headless:
        break;
    }
}

void addSurfaceLostSemantics(NativePlatformLifecycleLoweringDecision& decision)
{
    switch (decision.state.surfaceKind) {
    case platform::PlatformSurfaceKind::AndroidSurface:
        addUnique(decision, NativePlatformLifecycleSemantic::AndroidSurfaceInvalidatesSwapchain);
        break;
    case platform::PlatformSurfaceKind::IosLayer:
        addUnique(decision, NativePlatformLifecycleSemantic::IosLayerInvalidatesDrawableResources);
        break;
    case platform::PlatformSurfaceKind::WebCanvas:
        addUnique(decision, NativePlatformLifecycleSemantic::WebCanvasConfigureSurface);
        break;
    case platform::PlatformSurfaceKind::DesktopWindow:
        addUnique(decision, NativePlatformLifecycleSemantic::DesktopResizeRebuildsSwapchain);
        break;
    case platform::PlatformSurfaceKind::Headless:
        break;
    }
    markSurfaceRelease(decision);
}

void addResizeSemantics(NativePlatformLifecycleLoweringDecision& decision)
{
    switch (decision.state.surfaceKind) {
    case platform::PlatformSurfaceKind::AndroidSurface:
        addUnique(decision, NativePlatformLifecycleSemantic::AndroidSurfaceInvalidatesSwapchain);
        break;
    case platform::PlatformSurfaceKind::IosLayer:
        addUnique(
            decision, NativePlatformLifecycleSemantic::IosLayerResizeRebuildsDrawableResources);
        break;
    case platform::PlatformSurfaceKind::WebCanvas:
        addUnique(decision, NativePlatformLifecycleSemantic::WebCanvasConfigureSurface);
        break;
    case platform::PlatformSurfaceKind::DesktopWindow:
        addUnique(decision, NativePlatformLifecycleSemantic::DesktopResizeRebuildsSwapchain);
        break;
    case platform::PlatformSurfaceKind::Headless:
        break;
    }
}

} // namespace

void applyEventSemantics(
    NativePlatformLifecycleLoweringDecision& decision, platform::PlatformLifecycleEvent event)
{
    if (decision.backend == rhi::Backend::Null) {
        addUnique(decision, NativePlatformLifecycleSemantic::NullRecordedLifecycle);
    }

    switch (event) {
    case platform::PlatformLifecycleEvent::Start:
        break;
    case platform::PlatformLifecycleEvent::Pause:
        if (decision.state.surfaceKind == platform::PlatformSurfaceKind::AndroidSurface) {
            addUnique(decision, NativePlatformLifecycleSemantic::AndroidPauseStopsAcquirePresent);
            markSurfaceRelease(decision);
        }
        break;
    case platform::PlatformLifecycleEvent::Resume:
        if (decision.state.surfaceKind == platform::PlatformSurfaceKind::AndroidSurface) {
            addUnique(decision, NativePlatformLifecycleSemantic::AndroidResumeRequiresValidSurface);
        }
        markMemoryBudgetRefresh(decision);
        break;
    case platform::PlatformLifecycleEvent::Stop:
        markSurfaceRelease(decision);
        break;
    case platform::PlatformLifecycleEvent::SurfaceCreated:
        addSurfaceCreatedSemantics(decision);
        break;
    case platform::PlatformLifecycleEvent::SurfaceLost:
        addSurfaceLostSemantics(decision);
        break;
    case platform::PlatformLifecycleEvent::Resize:
        invalidateSwapchain(decision);
        addResizeSemantics(decision);
        break;
    case platform::PlatformLifecycleEvent::MemoryWarning:
        markMemoryBudgetRefresh(decision);
        markTransientRelease(decision);
        break;
    case platform::PlatformLifecycleEvent::EnterBackground:
        if (decision.state.surfaceKind == platform::PlatformSurfaceKind::IosLayer) {
            addUnique(decision, NativePlatformLifecycleSemantic::IosBackgroundStopsDrawableAcquire);
        }
        break;
    case platform::PlatformLifecycleEvent::EnterForeground:
        if (decision.state.surfaceKind == platform::PlatformSurfaceKind::IosLayer) {
            addUnique(
                decision, NativePlatformLifecycleSemantic::IosLayerInvalidatesDrawableResources);
        }
        invalidateSwapchain(decision);
        markMemoryBudgetRefresh(decision);
        break;
    }
}

NativePlatformLifecycleLowerer::NativePlatformLifecycleLowerer(
    NativeBackendCapabilityContract contract)
    : contract_(std::move(contract))
{}

NativePlatformLifecycleLoweringDecision NativePlatformLifecycleLowerer::lower(
    const platform::PlatformLifecycleState& state, platform::PlatformLifecycleEvent event) const
{
    return lowerPlatformLifecycleEvent(contract_, state, event);
}

NativePlatformLifecycleLoweringDecision lowerPlatformLifecycleEvent(
    const NativeBackendCapabilityContract& contract,
    const platform::PlatformLifecycleState& state,
    platform::PlatformLifecycleEvent event)
{
    if (contract.backend == rhi::Backend::WebGL) {
        return makeFailure(contract, state, "RHI-WEBGL1-LIFECYCLE-EXECUTION-UNAVAILABLE");
    }
    if (state.logicalFrameSlots != 2) {
        return makeFailure(
            contract, state, "platform lifecycle lowering requires two logical frame slots");
    }

    auto decision = makeDecision(contract, state);
    markDoubleBuffered(decision);
    applyEventSemantics(decision, event);
    rhi::FrameLifecycleAccess::refresh(decision.state);
    return decision;
}

bool hasNativePlatformLifecycleSemantic(const NativePlatformLifecycleLoweringDecision& decision,
    NativePlatformLifecycleSemantic semantic)
{
    for (const auto entry : decision.semantics) {
        if (entry == semantic) {
            return true;
        }
    }
    return false;
}

} // namespace arp::rhi::semantics
