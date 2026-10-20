#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/vulkan_device_capability_query.h"

#include <cassert>

int main()
{
    using namespace arp::rhi_l1;
    using namespace arp::rhi::semantics::vulkan;

    VulkanDeviceCapabilitySnapshot fullSnapshot;
    fullSnapshot.queriedFromNativeDevice = true;
    fullSnapshot.queriedExtendedFeatures = true;
    fullSnapshot.maxPushConstantSizeBytes = 256;
    fullSnapshot.supportsDescriptorIndexing = true;
    fullSnapshot.supportsTimelineSynchronization = true;
    fullSnapshot.supportsSecondaryCommandBuffers = true;
    fullSnapshot.queueFamilyCount = 3;
    fullSnapshot.hasDeviceLocalMemory = true;
    fullSnapshot.hasHostVisibleMemory = true;

    const auto fullContract = buildVulkanNativeCapabilityContract(fullSnapshot);
    assert(fullContract.backend == arp::rhi::Backend::Vulkan);
    assert(fullContract.queriedFromNativeDevice);
    assert(fullContract.queriedExtendedFeatures);
    assert(fullContract.maxPushConstantSizeBytes == 256u);
    assert(fullContract.queueFamilyCount == 3u);
    assert(fullContract.exposesBackendSpecificMemoryModel);
    assert(hasNativeFeature(fullContract, NativeBackendFeature::PushConstants));
    assert(hasNativeFeature(fullContract, NativeBackendFeature::DescriptorIndexing));
    assert(hasNativeFeature(fullContract, NativeBackendFeature::SecondaryCommandBuffers));
    assert(hasNativeFeature(fullContract, NativeBackendFeature::TimelineSynchronization));
    assert(hasNativeFeature(fullContract, NativeBackendFeature::QueueFamilies));
    assert(hasNativeFeature(fullContract, NativeBackendFeature::ExplicitMemoryControl));

    VulkanDeviceCapabilitySnapshot limitedSnapshot;
    limitedSnapshot.queriedFromNativeDevice = true;
    limitedSnapshot.supportsSecondaryCommandBuffers = true;
    limitedSnapshot.queueFamilyCount = 1;

    const auto limitedContract = buildVulkanNativeCapabilityContract(limitedSnapshot);
    assert(limitedContract.queriedFromNativeDevice);
    assert(!hasNativeFeature(limitedContract, NativeBackendFeature::PushConstants));
    assert(!hasNativeFeature(limitedContract, NativeBackendFeature::DescriptorIndexing));
    assert(hasNativeFeature(limitedContract, NativeBackendFeature::SecondaryCommandBuffers));
    assert(hasNativeFeature(limitedContract, NativeBackendFeature::QueueFamilies));
    assert(!hasNativeFeature(limitedContract, NativeBackendFeature::ExplicitMemoryControl));
    return 0;
}
