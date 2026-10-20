#include "rhi/semantics/vulkan_device_capability_query.h"

namespace arp::rhi::semantics::vulkan {

namespace {

void addFeature(NativeBackendCapabilityContract& contract, NativeBackendFeature feature)
{
    contract.features.push_back(feature);
}

} // namespace

NativeBackendCapabilityContract buildVulkanNativeCapabilityContract(
    const VulkanDeviceCapabilitySnapshot& snapshot)
{
    NativeBackendCapabilityContract contract;
    contract.backend = rhi::Backend::Vulkan;
    contract.preservesNativePotential = true;
    contract.flattenedToPortableSubset = false;
    contract.queriedFromNativeDevice = snapshot.queriedFromNativeDevice;
    contract.queriedExtendedFeatures = snapshot.queriedExtendedFeatures;
    contract.maxPushConstantSizeBytes = snapshot.maxPushConstantSizeBytes;
    contract.queueFamilyCount = snapshot.queueFamilyCount;
    contract.exposesBackendSpecificMemoryModel =
        snapshot.hasDeviceLocalMemory || snapshot.hasHostVisibleMemory;
    // Propagate the queried depthBiasClamp feature so pipeline state lowering
    // can fail closed when a descriptor uses a non-zero clamp on a device
    // that did not enable the feature (VUID-00754).
    contract.depthBiasClampSupported = snapshot.supportsDepthBiasClamp;

    if (snapshot.maxPushConstantSizeBytes > 0) {
        addFeature(contract, NativeBackendFeature::PushConstants);
    }
    if (snapshot.supportsDescriptorIndexing) {
        addFeature(contract, NativeBackendFeature::DescriptorIndexing);
    }
    if (snapshot.supportsSecondaryCommandBuffers) {
        addFeature(contract, NativeBackendFeature::SecondaryCommandBuffers);
    }
    if (snapshot.supportsTimelineSynchronization) {
        addFeature(contract, NativeBackendFeature::TimelineSynchronization);
    }
    if (snapshot.queueFamilyCount > 0) {
        addFeature(contract, NativeBackendFeature::QueueFamilies);
    }
    if (snapshot.hasDeviceLocalMemory && snapshot.hasHostVisibleMemory) {
        addFeature(contract, NativeBackendFeature::ExplicitMemoryControl);
    }

    // Subpass + input attachment is Vulkan core (not an extension). Always
    // available on any conformant Vulkan implementation. On tile-based GPUs
    // (Mali/Adreno) this hits tile memory; on desktop IMR it is functionally
    // correct but provides no bandwidth benefit.
    addFeature(contract, NativeBackendFeature::SubpassInputAttachment);

    // Compute dispatch (vkCmdDispatch), storage image binding
    // (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE + VK_IMAGE_LAYOUT_GENERAL), and
    // storage buffer binding (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT +
    // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) are Vulkan core capabilities. This
    // mirrors the static vulkanNativeFeatures() declaration so snapshot-driven
    // capability reporting does not incorrectly disable DDGI and other
    // compute paths.
    addFeature(contract, NativeBackendFeature::ComputeDispatch);
    addFeature(contract, NativeBackendFeature::StorageTextureBinding);
    addFeature(contract, NativeBackendFeature::StorageBufferBinding);

    // Scene geometry ray queries are lowered to the software BVH compute path
    // on Vulkan, which requires only the core compute and storage-buffer
    // capabilities reported above.
    addFeature(contract, NativeBackendFeature::SceneGeometryRayQuery);

    // Per-format storage-texture probe for Rg16Float. Vulkan core allows
    // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
    // to vary per format across implementations, so this is probed explicitly
    // via vkGetPhysicalDeviceFormatProperties rather than inferred from the
    // coarse StorageTextureBinding capability (AGENTS.md:31 - no inference
    // from "usually supported"). When the snapshot did not query the native
    // device (synthetic runtime), fail closed rather than assume.
    if (snapshot.supportsRg16FloatStorageTexture) {
        addFeature(contract, NativeBackendFeature::Rg16FloatStorageTexture);
    }
    if (snapshot.supportsRgba16FloatStorageTexture) {
        addFeature(contract, NativeBackendFeature::Rgba16FloatStorageTexture);
    }

    // Indirect draw (vkCmdDrawIndexedIndirect) is Vulkan core (1.0). Always
    // available on any conformant Vulkan device.
    addFeature(contract, NativeBackendFeature::IndirectDraw);

    // vkCmdDrawIndexedIndirectCount is core on Vulkan 1.2+ or 1.1 +
    // VK_KHR_draw_indirect_count. Probed via the Vulkan 1.2 feature chain
    // in render_system_vulkan.cpp. When the snapshot did not query the
    // native device (synthetic runtime), fail closed rather than assume.
    if (snapshot.supportsDrawIndirectCount) {
        addFeature(contract, NativeBackendFeature::DrawIndirectCount);
    }
    if (snapshot.supportsTimestampQueries) {
        addFeature(contract, NativeBackendFeature::TimestampQueries);
    }

    return contract;
}

} // namespace arp::rhi::semantics::vulkan
