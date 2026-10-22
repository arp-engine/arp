#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/native_platform_lifecycle_lowering.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    NativePlatformLifecycleLowerer androidLowerer(nativeBackendCapabilityContract(Backend::Vulkan));
    PlatformLifecycleState android;
    android.surfaceKind = PlatformSurfaceKind::AndroidSurface;
    android.logicalFrameSlots = 2;

    auto androidStarted = androidLowerer.lower(android, PlatformLifecycleEvent::Start);
    assert(androidStarted.success);
    assert(androidStarted.state.running);
    assert(!androidStarted.state.surfaceValid);
    assert(!androidStarted.state.acquireAllowed);
    assert(!androidStarted.state.presentAllowed);
    assert(androidStarted.state.logicalFrameSlots == 2u);
    assert(!androidStarted.state.nativeHandleExposedToUpperLayer);

    auto androidSurface =
        androidLowerer.lower(androidStarted.state, PlatformLifecycleEvent::SurfaceCreated);
    assert(androidSurface.success);
    assert(androidSurface.state.surfaceValid);
    assert(androidSurface.state.swapchainRebuildRequired);
    assert(androidSurface.state.acquireAllowed);
    assert(androidSurface.state.presentAllowed);
    assert(hasNativePlatformLifecycleSemantic(
        androidSurface, NativePlatformLifecycleSemantic::AndroidSurfaceInvalidatesSwapchain));
    assert(hasNativePlatformLifecycleSemantic(
        androidSurface, NativePlatformLifecycleSemantic::DoubleBufferedFrameSlotsPreserved));

    auto androidPaused = androidLowerer.lower(androidSurface.state, PlatformLifecycleEvent::Pause);
    assert(androidPaused.success);
    assert(androidPaused.state.paused);
    assert(!androidPaused.state.acquireAllowed);
    assert(!androidPaused.state.presentAllowed);
    assert(androidPaused.state.releaseSurfaceResources);
    assert(hasNativePlatformLifecycleSemantic(
        androidPaused, NativePlatformLifecycleSemantic::AndroidPauseStopsAcquirePresent));
    assert(hasNativePlatformLifecycleSemantic(
        androidPaused, NativePlatformLifecycleSemantic::ReleaseSurfaceFrameResources));

    auto androidLost =
        androidLowerer.lower(androidPaused.state, PlatformLifecycleEvent::SurfaceLost);
    assert(androidLost.success);
    assert(!androidLost.state.surfaceValid);
    assert(androidLost.state.swapchainRebuildRequired);
    assert(!androidLost.state.acquireAllowed);
    assert(!androidLost.state.presentAllowed);
    assert(androidLost.state.releaseSurfaceResources);

    auto androidResumedWithoutSurface =
        androidLowerer.lower(androidLost.state, PlatformLifecycleEvent::Resume);
    assert(androidResumedWithoutSurface.success);
    assert(!androidResumedWithoutSurface.state.paused);
    assert(!androidResumedWithoutSurface.state.surfaceValid);
    assert(!androidResumedWithoutSurface.state.acquireAllowed);
    assert(!androidResumedWithoutSurface.state.presentAllowed);
    assert(androidResumedWithoutSurface.state.memoryBudgetRefreshRequired);
    assert(hasNativePlatformLifecycleSemantic(androidResumedWithoutSurface,
        NativePlatformLifecycleSemantic::AndroidResumeRequiresValidSurface));
    assert(hasNativePlatformLifecycleSemantic(
        androidResumedWithoutSurface, NativePlatformLifecycleSemantic::MemoryBudgetRefresh));

    auto androidRecreated = androidLowerer.lower(
        androidResumedWithoutSurface.state, PlatformLifecycleEvent::SurfaceCreated);
    assert(androidRecreated.success);
    assert(androidRecreated.state.surfaceValid);
    assert(androidRecreated.state.swapchainRebuildRequired);
    assert(androidRecreated.state.acquireAllowed);
    assert(androidRecreated.state.presentAllowed);
    assert(androidRecreated.state.logicalFrameSlots == 2u);

    auto androidPressure =
        androidLowerer.lower(androidRecreated.state, PlatformLifecycleEvent::MemoryWarning);
    assert(androidPressure.success);
    assert(androidPressure.state.memoryBudgetRefreshRequired);
    assert(androidPressure.state.releaseTransientResources);
    assert(hasNativePlatformLifecycleSemantic(
        androidPressure, NativePlatformLifecycleSemantic::MemoryBudgetRefresh));
    assert(hasNativePlatformLifecycleSemantic(
        androidPressure, NativePlatformLifecycleSemantic::ReleaseTransientResources));

    NativePlatformLifecycleLowerer iosLowerer(nativeBackendCapabilityContract(Backend::Metal));
    PlatformLifecycleState ios;
    ios.surfaceKind = PlatformSurfaceKind::IosLayer;
    ios.logicalFrameSlots = 2;
    ios.running = true;
    ios.surfaceValid = true;
    ios.acquireAllowed = true;
    ios.presentAllowed = true;

    auto iosBackgrounded = iosLowerer.lower(ios, PlatformLifecycleEvent::EnterBackground);
    assert(iosBackgrounded.success);
    assert(iosBackgrounded.state.backgrounded);
    assert(!iosBackgrounded.state.acquireAllowed);
    assert(!iosBackgrounded.state.presentAllowed);
    assert(iosBackgrounded.state.logicalFrameSlots == 2u);
    assert(!iosBackgrounded.state.nativeHandleExposedToUpperLayer);
    assert(hasNativePlatformLifecycleSemantic(
        iosBackgrounded, NativePlatformLifecycleSemantic::IosBackgroundStopsDrawableAcquire));

    auto iosWarned = iosLowerer.lower(iosBackgrounded.state, PlatformLifecycleEvent::MemoryWarning);
    assert(iosWarned.success);
    assert(iosWarned.state.memoryBudgetRefreshRequired);
    assert(iosWarned.state.releaseTransientResources);

    auto iosForegrounded =
        iosLowerer.lower(iosWarned.state, PlatformLifecycleEvent::EnterForeground);
    assert(iosForegrounded.success);
    assert(!iosForegrounded.state.backgrounded);
    assert(iosForegrounded.state.swapchainRebuildRequired);
    assert(iosForegrounded.state.memoryBudgetRefreshRequired);
    assert(iosForegrounded.state.acquireAllowed);
    assert(iosForegrounded.state.presentAllowed);
    assert(hasNativePlatformLifecycleSemantic(
        iosForegrounded, NativePlatformLifecycleSemantic::IosLayerInvalidatesDrawableResources));

    auto iosResized = iosLowerer.lower(iosForegrounded.state, PlatformLifecycleEvent::Resize);
    assert(iosResized.success);
    assert(iosResized.state.swapchainRebuildRequired);
    assert(iosResized.state.logicalFrameSlots == 2u);
    assert(hasNativePlatformLifecycleSemantic(
        iosResized, NativePlatformLifecycleSemantic::IosLayerResizeRebuildsDrawableResources));

    NativePlatformLifecycleLowerer webLowerer(nativeBackendCapabilityContract(Backend::WebGPU));
    PlatformLifecycleState web;
    web.surfaceKind = PlatformSurfaceKind::WebCanvas;
    web.logicalFrameSlots = 2;
    web.running = true;
    web.surfaceValid = true;
    web.acquireAllowed = true;
    web.presentAllowed = true;

    auto webResized = webLowerer.lower(web, PlatformLifecycleEvent::Resize);
    assert(webResized.success);
    assert(webResized.state.swapchainRebuildRequired);
    assert(webResized.state.logicalFrameSlots == 2u);
    assert(!webResized.state.nativeHandleExposedToUpperLayer);
    assert(hasNativePlatformLifecycleSemantic(
        webResized, NativePlatformLifecycleSemantic::WebCanvasConfigureSurface));

    NativePlatformLifecycleLowerer nullLowerer(nativeBackendCapabilityContract(Backend::Null));
    PlatformLifecycleState invalid;
    invalid.surfaceKind = PlatformSurfaceKind::Headless;
    invalid.logicalFrameSlots = 3;
    auto invalidDecision = nullLowerer.lower(invalid, PlatformLifecycleEvent::Start);
    assert(!invalidDecision.success);
    assert(invalidDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidDecision.diagnostics.find("frame slot") != std::pmr::string::npos);

    return 0;
}
