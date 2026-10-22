#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/native_synchronization_lowering.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    SynchronizationWorkloadDesc frameSync;
    frameSync.label = "FrameSubmitPresent";
    frameSync.logicalFrameSlots = 2;
    frameSync.usesAcquire = true;
    frameSync.usesQueueSubmit = true;
    frameSync.usesPresent = true;
    frameSync.requiresTimelineProgress = true;

    const auto vulkan =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::Vulkan), frameSync);
    assert(vulkan.success);
    assert(vulkan.backend == Backend::Vulkan);
    assert(vulkan.fallbackMode == FallbackMode::Native);
    assert(vulkan.logicalFrameSlots == 2u);
    assert(!vulkan.nativeHandleExposedToUpperLayer);
    assert(!vulkan.upperLayerBlockingWaitRequired);
    assert(
        hasNativeSynchronizationSemantic(vulkan, NativeSynchronizationSemantic::VulkanQueueSubmit));
    assert(hasNativeSynchronizationSemantic(
        vulkan, NativeSynchronizationSemantic::VulkanTimelineSemaphore));
    assert(hasNativeSynchronizationSemantic(
        vulkan, NativeSynchronizationSemantic::VulkanBinaryAcquireSemaphore));
    assert(hasNativeSynchronizationSemantic(
        vulkan, NativeSynchronizationSemantic::VulkanPresentQueue));
    assert(hasNativeSynchronizationSemantic(
        vulkan, NativeSynchronizationSemantic::DoubleBufferedFrameSlot));

    auto limitedVulkanContract = nativeBackendCapabilityContract(Backend::Vulkan);
    limitedVulkanContract.features = {
        NativeBackendFeature::QueueFamilies,
        NativeBackendFeature::ExplicitMemoryControl,
    };
    const auto limitedVulkan = lowerSynchronizationWorkload(limitedVulkanContract, frameSync);
    assert(limitedVulkan.success);
    assert(limitedVulkan.fallbackMode == FallbackMode::Emulated);
    assert(!limitedVulkan.upperLayerBlockingWaitRequired);
    assert(hasNativeSynchronizationSemantic(
        limitedVulkan, NativeSynchronizationSemantic::VulkanFenceFrameRetire));

    const auto metal =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::Metal), frameSync);
    assert(metal.success);
    assert(metal.backend == Backend::Metal);
    assert(metal.fallbackMode == FallbackMode::Emulated);
    assert(!metal.nativeHandleExposedToUpperLayer);
    assert(!metal.upperLayerBlockingWaitRequired);
    assert(hasNativeSynchronizationSemantic(
        metal, NativeSynchronizationSemantic::MetalCommandBufferCompletion));
    assert(hasNativeSynchronizationSemantic(
        metal, NativeSynchronizationSemantic::MetalDrawablePresent));
    assert(hasNativeSynchronizationSemantic(
        metal, NativeSynchronizationSemantic::DoubleBufferedFrameSlot));

    const auto webgpu =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::WebGPU), frameSync);
    assert(webgpu.success);
    assert(webgpu.backend == Backend::WebGPU);
    assert(webgpu.fallbackMode == FallbackMode::Emulated);
    assert(!webgpu.nativeHandleExposedToUpperLayer);
    assert(!webgpu.upperLayerBlockingWaitRequired);
    assert(
        hasNativeSynchronizationSemantic(webgpu, NativeSynchronizationSemantic::WebGpuQueueSubmit));
    assert(hasNativeSynchronizationSemantic(
        webgpu, NativeSynchronizationSemantic::WebGpuSubmittedWorkDone));
    assert(hasNativeSynchronizationSemantic(
        webgpu, NativeSynchronizationSemantic::WebGpuSurfacePresent));
    assert(hasNativeSynchronizationSemantic(
        webgpu, NativeSynchronizationSemantic::DoubleBufferedFrameSlot));

    const auto nullSync =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::Null), frameSync);
    assert(nullSync.success);
    assert(nullSync.backend == Backend::Null);
    assert(nullSync.fallbackMode == FallbackMode::Native);
    assert(hasNativeSynchronizationSemantic(
        nullSync, NativeSynchronizationSemantic::NullRecordedSynchronization));

    auto invalid = frameSync;
    invalid.logicalFrameSlots = 1;
    const auto invalidDecision =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::Vulkan), invalid);
    assert(!invalidDecision.success);
    assert(invalidDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidDecision.diagnostics.find("frame slot") != std::pmr::string::npos);

    // Phase 5: variable frames-in-flight is now supported. logicalFrameSlots
    // >= 2 must succeed on Vulkan (native sync).
    auto triple = frameSync;
    triple.logicalFrameSlots = 3;
    const auto tripleDecision =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::Vulkan), triple);
    assert(tripleDecision.success);
    assert(tripleDecision.logicalFrameSlots == 3u);

    return 0;
}
