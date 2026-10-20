#pragma once

#include "rhi/semantics/native_backend_capabilities.h"

#include <cstdint>

namespace arp::rhi::semantics::vulkan {

struct VulkanDeviceCapabilitySnapshot {
    bool queriedFromNativeDevice = false;
    bool queriedExtendedFeatures = false;
    std::uint32_t maxPushConstantSizeBytes = 0;
    bool supportsDescriptorIndexing = false;
    bool supportsTimelineSynchronization = false;
    bool supportsSecondaryCommandBuffers = false;
    std::uint32_t queueFamilyCount = 0;
    bool hasDeviceLocalMemory = false;
    bool hasHostVisibleMemory = false;
    /// Queried from VkPhysicalDeviceFeatures::depthBiasClamp. Required (VUID-
    /// 00754) before a pipeline may set a non-zero depthBiasClamp. Propagated
    /// to NativeBackendCapabilityContract::depthBiasClampSupported so pipeline
    /// state lowering can fail closed on devices that lack the feature.
    bool supportsDepthBiasClamp = false;
    /// Queried from vkGetPhysicalDeviceFormatProperties(VK_FORMAT_R16G16_SFLOAT)
    /// .optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT. Required
    /// by clients that explicitly request an Rg16Float storage texture.
    /// Per-format probe (not a device-level feature) - Vulkan core allows this
    /// to vary across implementations.
    bool supportsRg16FloatStorageTexture = false;
    bool supportsRgba16FloatStorageTexture = false;
    /// Queried from the Vulkan 1.2 feature chain
    /// (VkPhysicalDeviceVulkan12Features::drawIndirectCount) or the
    /// VK_KHR_draw_indirect_count extension. Required by P5
    /// drawIndexedIndirectCount (GPU-provided draw count). When false, the
    /// WoCC layer falls back to drawIndexedIndirect with a CPU-known max
    /// draw count. IndirectDraw itself (vkCmdDrawIndexedIndirect) is core
    /// since Vulkan 1.0 and does not need probing.
    bool supportsDrawIndirectCount = false;
    /// Queried from VkPhysicalDeviceProperties::limits.timestampPeriod. A
    /// zero period cannot produce a meaningful timestamp conversion.
    bool supportsTimestampQueries = false;
};

NativeBackendCapabilityContract buildVulkanNativeCapabilityContract(
    const VulkanDeviceCapabilitySnapshot& snapshot);

} // namespace arp::rhi::semantics::vulkan
