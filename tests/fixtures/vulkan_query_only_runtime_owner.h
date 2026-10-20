#pragma once

#include "rhi/backends/vulkan/vulkan_runtime.h"

namespace arp::tests {

// CPU query fixture only. This owns immutable synthetic identities, not a
// VkDevice. It cannot provide evidence of native execution or native lifetime.
class VulkanQueryOnlyRuntimeOwner final : public rhi_vulkan::VulkanRhiNativeRuntimeOwner {
public:
    explicit VulkanQueryOnlyRuntimeOwner(const rhi_vulkan::VulkanRhiRuntimeDesc& desc)
        : instance_(desc.instance), physicalDevice_(desc.physicalDevice),
          device_(desc.logicalDevice)
    {}

    VkInstance nativeInstance() const noexcept override { return instance_; }

    VkPhysicalDevice nativePhysicalDevice() const noexcept override { return physicalDevice_; }

    VkDevice nativeDevice() const noexcept override { return device_; }

private:
    const VkInstance instance_;
    const VkPhysicalDevice physicalDevice_;
    const VkDevice device_;
};

} // namespace arp::tests
