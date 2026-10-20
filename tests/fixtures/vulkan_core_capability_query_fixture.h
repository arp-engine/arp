#pragma once

#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "tests/fixtures/vulkan_query_only_runtime_owner.h"

#include <array>
#include <cstring>

namespace arp::tests {

// Injected query source for CPU contract tests only. Synthetic handles and
// callbacks prove validation/control flow, never a native Vulkan payload.
class VulkanCoreCapabilityQueryFixture final {
public:
    VulkanCoreCapabilityQueryFixture() : previous_(active_) { active_ = this; }

    ~VulkanCoreCapabilityQueryFixture() { active_ = previous_; }

    VulkanCoreCapabilityQueryFixture(const VulkanCoreCapabilityQueryFixture&) = delete;
    VulkanCoreCapabilityQueryFixture& operator=(const VulkanCoreCapabilityQueryFixture&) = delete;

    rhi_vulkan::VulkanRhiRuntimeDesc descriptor() const
    {
        rhi_vulkan::VulkanRhiRuntimeDesc result;
        result.enableSyntheticRuntime = false;
        result.instance = rhi_vulkan::syntheticHandle<VkInstance>(1u);
        result.physicalDevice = rhi_vulkan::syntheticHandle<VkPhysicalDevice>(2u);
        result.logicalDevice = rhi_vulkan::syntheticHandle<VkDevice>(3u);
        result.graphicsQueue = rhi_vulkan::syntheticHandle<VkQueue>(4u);
        result.queueFamilyCount = static_cast<std::uint32_t>(families.size());
        result.getInstanceProcAddr = &instanceProcedure;
        result.getDeviceProcAddr = &deviceProcedure;
        result.nativeOwner = core::makeShared<VulkanQueryOnlyRuntimeOwner>(result);
        return result;
    }

    std::array<VkQueueFamilyProperties, 2u> families{
        {{VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 1u, 0u, {}},
            {VK_QUEUE_COMPUTE_BIT, 1u, 0u, {}}}};
    std::uint32_t reportedCount = 2u;
    std::uint32_t secondCount = 2u;
    std::uint32_t memoryTypeCount = 2u;
    unsigned familyQueries = 0u;
    unsigned queueQueries = 0u;
    bool familyQueryAvailable = true;
    bool queueQueryAvailable = true;
    bool memoryQueryAvailable = true;
    VkQueue queriedQueue = rhi_vulkan::syntheticHandle<VkQueue>(4u);

private:
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProcedure(VkInstance, const char* name)
    {
        if (std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties") == 0)
            return active_->familyQueryAvailable
                ? reinterpret_cast<PFN_vkVoidFunction>(&familyProperties)
                : nullptr;
        if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
            return active_->memoryQueryAvailable
                ? reinterpret_cast<PFN_vkVoidFunction>(&memoryProperties)
                : nullptr;
        return nullptr;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProcedure(VkDevice, const char* name)
    {
        return active_->queueQueryAvailable && std::strcmp(name, "vkGetDeviceQueue") == 0
            ? reinterpret_cast<PFN_vkVoidFunction>(&deviceQueue)
            : nullptr;
    }

    static VKAPI_ATTR void VKAPI_CALL familyProperties(
        VkPhysicalDevice, std::uint32_t* count, VkQueueFamilyProperties* output)
    {
        ++active_->familyQueries;
        if (!output) {
            *count = active_->reportedCount;
            return;
        }
        for (std::uint32_t i = 0u; i < *count && i < active_->families.size(); ++i)
            output[i] = active_->families[i];
        *count = active_->secondCount;
    }

    static VKAPI_ATTR void VKAPI_CALL deviceQueue(
        VkDevice, std::uint32_t, std::uint32_t, VkQueue* output)
    {
        ++active_->queueQueries;
        *output = active_->queriedQueue;
    }

    static VKAPI_ATTR void VKAPI_CALL memoryProperties(
        VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* output)
    {
        *output = {};
        output->memoryTypeCount = active_->memoryTypeCount;
        output->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        output->memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }

    inline static VulkanCoreCapabilityQueryFixture* active_ = nullptr;
    VulkanCoreCapabilityQueryFixture* previous_ = nullptr;
};

} // namespace arp::tests
