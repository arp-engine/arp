#include "core/cpu_allocator.h"
#include "rhi/semantics/webgpu_canvas_lifecycle_bridge.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.maxTexture2DSize = 4096;
    probe.maxColorAttachments = 1;
    probe.maxDescriptorBindings = 32;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;

    const auto profile = buildWebGpuRuntimeProfile(probe);
    auto state = makeWebGpuCanvasLifecycleState(profile);
    assert(state.surfaceKind == PlatformSurfaceKind::WebCanvas);
    assert(state.logicalFrameSlots == 2u);
    assert(state.running);
    assert(state.surfaceValid);
    assert(state.acquireAllowed);
    assert(state.presentAllowed);
    assert(!state.nativeHandleExposedToUpperLayer);

    WebGpuCanvasLifecycleBridge bridge(profile);
    auto resized = bridge.lower(state, PlatformLifecycleEvent::Resize);
    assert(resized.success);
    assert(resized.backend == Backend::WebGPU);
    assert(resized.state.logicalFrameSlots == 2u);
    assert(resized.state.swapchainRebuildRequired);
    assert(resized.state.acquireAllowed);
    assert(resized.state.presentAllowed);
    assert(!resized.state.nativeHandleExposedToUpperLayer);
    assert(hasNativePlatformLifecycleSemantic(
        resized, NativePlatformLifecycleSemantic::WebCanvasConfigureSurface));
    assert(hasNativePlatformLifecycleSemantic(
        resized, NativePlatformLifecycleSemantic::DoubleBufferedFrameSlotsPreserved));

    auto lost = bridge.lower(resized.state, PlatformLifecycleEvent::SurfaceLost);
    assert(lost.success);
    assert(!lost.state.surfaceValid);
    assert(!lost.state.acquireAllowed);
    assert(!lost.state.presentAllowed);
    assert(lost.state.releaseSurfaceResources);
    assert(lost.state.swapchainRebuildRequired);
    assert(hasNativePlatformLifecycleSemantic(
        lost, NativePlatformLifecycleSemantic::ReleaseSurfaceFrameResources));

    auto recreated = bridge.lower(lost.state, PlatformLifecycleEvent::SurfaceCreated);
    assert(recreated.success);
    assert(recreated.state.surfaceValid);
    assert(recreated.state.acquireAllowed);
    assert(recreated.state.presentAllowed);
    assert(recreated.state.swapchainRebuildRequired);
    assert(recreated.state.logicalFrameSlots == 2u);
    assert(hasNativePlatformLifecycleSemantic(
        recreated, NativePlatformLifecycleSemantic::WebCanvasConfigureSurface));

    auto memoryWarning = bridge.lower(recreated.state, PlatformLifecycleEvent::MemoryWarning);
    assert(memoryWarning.success);
    assert(memoryWarning.state.releaseTransientResources);
    assert(memoryWarning.state.memoryBudgetRefreshRequired);
    assert(hasNativePlatformLifecycleSemantic(
        memoryWarning, NativePlatformLifecycleSemantic::ReleaseTransientResources));

    WebGpuRuntimeProbe invalidProbe = probe;
    invalidProbe.canvasSurface = false;
    const auto invalidProfile = buildWebGpuRuntimeProfile(invalidProbe);
    const auto invalidState = makeWebGpuCanvasLifecycleState(invalidProfile);
    assert(invalidState.surfaceKind == PlatformSurfaceKind::WebCanvas);
    assert(!invalidState.surfaceValid);
    assert(!invalidState.acquireAllowed);
    assert(!invalidState.presentAllowed);

    auto invalidDecision = lowerWebGpuCanvasLifecycleEvent(
        invalidProfile, invalidState, PlatformLifecycleEvent::SurfaceCreated);
    assert(!invalidDecision.success);
    assert(invalidDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidDecision.diagnostics.find("canvas") != std::pmr::string::npos);

    auto invalidSlots = state;
    invalidSlots.logicalFrameSlots = 3;
    auto invalidSlotsDecision = bridge.lower(invalidSlots, PlatformLifecycleEvent::Resize);
    assert(!invalidSlotsDecision.success);
    assert(invalidSlotsDecision.diagnostics.find("frame slot") != std::pmr::string::npos);

    return 0;
}
