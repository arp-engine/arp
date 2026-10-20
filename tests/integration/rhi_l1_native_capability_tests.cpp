#include "rhi/semantics/native_backend_capabilities.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto vulkan = nativeBackendCapabilityContract(Backend::Vulkan);
    assert(vulkan.backend == Backend::Vulkan);
    assert(vulkan.preservesNativePotential);
    assert(!vulkan.flattenedToPortableSubset);
    assert(vulkan.exposesBackendSpecificMemoryModel);
    assert(hasNativeFeature(vulkan, NativeBackendFeature::PushConstants));
    assert(hasNativeFeature(vulkan, NativeBackendFeature::DescriptorIndexing));
    assert(hasNativeFeature(vulkan, NativeBackendFeature::SecondaryCommandBuffers));
    assert(hasNativeFeature(vulkan, NativeBackendFeature::TimelineSynchronization));
    assert(hasNativeFeature(vulkan, NativeBackendFeature::QueueFamilies));
    assert(hasNativeFeature(vulkan, NativeBackendFeature::ExplicitMemoryControl));

    const auto metal = nativeBackendCapabilityContract(Backend::Metal);
    assert(metal.preservesNativePotential);
    assert(hasNativeFeature(metal, NativeBackendFeature::ArgumentBuffers));
    assert(hasNativeFeature(metal, NativeBackendFeature::TileMemoryHints));

    const auto webgpu = nativeBackendCapabilityContract(Backend::WebGPU);
    assert(webgpu.preservesNativePotential);
    assert(!hasNativeFeature(webgpu, NativeBackendFeature::PushConstants));
    assert(hasNativeFeature(webgpu, NativeBackendFeature::RuntimeLimits));
    assert(hasNativeFeature(webgpu, NativeBackendFeature::OptionalRuntimeFeatures));
    return 0;
}
