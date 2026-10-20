#include "rhi/gpu_capabilities.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    const auto vulkan = backendMappingPolicy(Backend::Vulkan);
    assert(vulkan.nativePushConstants);
    assert(vulkan.nativeSecondaryCommandBuffers);
    assert(vulkan.preservesNativePotential);
    assert(!vulkan.forcesLowestCommonDenominator);
    assert(vulkan.descriptorModel == DescriptorBackendModel::VulkanDescriptorSet);

    const auto webgpu = backendMappingPolicy(Backend::WebGPU);
    assert(!webgpu.nativePushConstants);
    assert(webgpu.emulatesPushConstants);
    assert(webgpu.descriptorModel == DescriptorBackendModel::BackendOwnedDescriptorGroup);
    assert(webgpu.swapchainExposesOnlyTwoLogicalFrameSlots);

    const auto pushConstants = chooseFeatureFallback(Backend::WebGPU, EngineFeature::PushConstants);
    assert(pushConstants.mode == FallbackMode::Emulated);
    assert(pushConstants.upperLayerVisible == false);

    const auto secondaryCommands =
        chooseFeatureFallback(Backend::WebGPU, EngineFeature::SecondaryCommandBuffer);
    // WebGPU has no native secondary command buffer, but exposes an explicit
    // allocator-owned CPU replay lowering. The command descriptor still has
    // to opt into that fallback before recording can succeed.
    assert(secondaryCommands.mode == FallbackMode::CpuReplay);
    assert(secondaryCommands.upperLayerVisible == false);
    return 0;
}
