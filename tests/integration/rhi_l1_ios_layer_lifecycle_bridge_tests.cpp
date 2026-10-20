#include "core/cpu_allocator.h"
#include "rhi/semantics/ios_layer_lifecycle_bridge.h"

#include <cassert>
#include <string>

namespace {

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedMetalContract()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

arp::rhi::semantics::IosLayerLifecycleValidationDesc makeDesc()
{
    arp::rhi::semantics::IosLayerLifecycleValidationDesc desc;
    desc.contract = makeQueriedMetalContract();
    desc.logicalFrameSlots = 2;
    desc.runtimeActive = true;
    desc.backgrounded = false;
    desc.layerValid = true;
    return desc;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto report = validateIosLayerLifecycle(makeDesc());
    assert(report.success);
    assert(report.contractComplete);
    assert(report.backend == Backend::Metal);
    assert(report.initialState.surfaceKind == PlatformSurfaceKind::IosLayer);
    assert(report.initialState.logicalFrameSlots == 2u);
    assert(report.initialState.acquireAllowed);
    assert(report.initialState.presentAllowed);
    assert(report.backgroundValidated);
    assert(report.foregroundValidated);
    assert(report.layerResizeValidated);
    assert(report.layerRecreationValidated);
    assert(report.drawableAcquirePresentBlockingValidated);
    assert(report.memoryWarningValidated);
    assert(report.twoFrameSlotsPreserved);
    assert(report.logicalFrameSlots == 2u);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(report.diagnostics.find("native handle") == std::pmr::string::npos);

    assert(report.backgroundDecision.state.backgrounded);
    assert(!report.backgroundDecision.state.acquireAllowed);
    assert(!report.backgroundDecision.state.presentAllowed);
    assert(hasNativePlatformLifecycleSemantic(report.backgroundDecision,
        NativePlatformLifecycleSemantic::IosBackgroundStopsDrawableAcquire));

    assert(report.memoryWarningDecision.state.memoryBudgetRefreshRequired);
    assert(report.memoryWarningDecision.state.releaseTransientResources);
    assert(!report.memoryWarningDecision.state.acquireAllowed);
    assert(!report.memoryWarningDecision.state.presentAllowed);
    assert(hasNativePlatformLifecycleSemantic(
        report.memoryWarningDecision, NativePlatformLifecycleSemantic::MemoryBudgetRefresh));
    assert(hasNativePlatformLifecycleSemantic(
        report.memoryWarningDecision, NativePlatformLifecycleSemantic::ReleaseTransientResources));

    assert(!report.foregroundDecision.state.backgrounded);
    assert(report.foregroundDecision.state.acquireAllowed);
    assert(report.foregroundDecision.state.presentAllowed);
    assert(report.foregroundDecision.state.swapchainRebuildRequired);
    assert(report.foregroundDecision.state.memoryBudgetRefreshRequired);
    assert(hasNativePlatformLifecycleSemantic(report.foregroundDecision,
        NativePlatformLifecycleSemantic::IosLayerInvalidatesDrawableResources));

    assert(report.resizeDecision.state.swapchainRebuildRequired);
    assert(report.resizeDecision.state.acquireAllowed);
    assert(report.resizeDecision.state.presentAllowed);
    assert(hasNativePlatformLifecycleSemantic(report.resizeDecision,
        NativePlatformLifecycleSemantic::IosLayerResizeRebuildsDrawableResources));

    assert(!report.layerLostDecision.state.surfaceValid);
    assert(!report.layerLostDecision.state.acquireAllowed);
    assert(!report.layerLostDecision.state.presentAllowed);
    assert(report.layerLostDecision.state.releaseSurfaceResources);
    assert(hasNativePlatformLifecycleSemantic(
        report.layerLostDecision, NativePlatformLifecycleSemantic::ReleaseSurfaceFrameResources));

    assert(report.layerRecreatedDecision.state.surfaceValid);
    assert(report.layerRecreatedDecision.state.acquireAllowed);
    assert(report.layerRecreatedDecision.state.presentAllowed);
    assert(report.layerRecreatedDecision.state.swapchainRebuildRequired);
    assert(hasNativePlatformLifecycleSemantic(report.layerRecreatedDecision,
        NativePlatformLifecycleSemantic::IosLayerInvalidatesDrawableResources));

    IosLayerLifecycleBridge bridge(makeDesc());
    const auto bridgeResize = bridge.lower(report.initialState, PlatformLifecycleEvent::Resize);
    assert(bridgeResize.success);
    assert(bridgeResize.state.swapchainRebuildRequired);
    assert(hasNativePlatformLifecycleSemantic(
        bridgeResize, NativePlatformLifecycleSemantic::IosLayerResizeRebuildsDrawableResources));

    auto invalidSlots = makeDesc();
    invalidSlots.logicalFrameSlots = 3;
    const auto slotRejected = validateIosLayerLifecycle(invalidSlots);
    assert(!slotRejected.success);
    assert(!slotRejected.twoFrameSlotsPreserved);
    assert(slotRejected.diagnostics.find("frame slot") != std::pmr::string::npos);

    auto wrongBackend = makeDesc();
    wrongBackend.contract = nativeBackendCapabilityContract(Backend::Vulkan);
    const auto backendRejected = validateIosLayerLifecycle(wrongBackend);
    assert(!backendRejected.success);
    assert(!backendRejected.contractComplete);
    assert(backendRejected.diagnostics.find("iOS") != std::pmr::string::npos);

    return 0;
}
