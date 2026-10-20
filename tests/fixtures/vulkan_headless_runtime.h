#pragma once

#include "rhi/backends/vulkan/vulkan_runtime.h"

#include <cstdio>
#include <vector>

namespace arp::tests {

// Native-only test bootstrap. Explicitly requires Vulkan 1.2 timeline support
// so tests can keep a real submission pending without relying on GPU speed.
class VulkanHeadlessRuntime final {
    class NativeOwner final : public rhi_vulkan::VulkanRhiNativeRuntimeOwner {
    public:
        ~NativeOwner() override
        {
            // Cold test teardown only. This result never issues completion.
            if (device) {
                const auto result = vkDeviceWaitIdle(device);
                if (result != VK_SUCCESS)
                    std::fprintf(stderr, "Native fixture idle failed: %d\n", int(result));
                vkDestroyDevice(device, nullptr);
            }
            if (instance)
                vkDestroyInstance(instance, nullptr);
        }

        VkInstance nativeInstance() const noexcept override { return instance; }

        VkPhysicalDevice nativePhysicalDevice() const noexcept override { return physical; }

        VkDevice nativeDevice() const noexcept override { return device; }

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
    };

public:
    explicit VulkanHeadlessRuntime(bool enableDescriptorIndexing = false,
        PFN_vkGetDeviceProcAddr deviceResolver = vkGetDeviceProcAddr,
        bool useGraphicsQueueForPresent = false)
        : owner_(core::makeShared<NativeOwner>())
    {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "ArpNativeCompletionFixture";
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app;
        const auto instanceResult = vkCreateInstance(&instanceInfo, nullptr, &owner_->instance);
        if (instanceResult != VK_SUCCESS) {
            std::fprintf(
                stderr, "Native fixture vkCreateInstance failed: %d\n", int(instanceResult));
            return;
        }
        std::uint32_t count = 0;
        const auto countResult = vkEnumeratePhysicalDevices(owner_->instance, &count, nullptr);
        if (countResult != VK_SUCCESS) {
            std::fprintf(stderr,
                "Native fixture vkEnumeratePhysicalDevices(count) failed: %d\n",
                int(countResult));
            return;
        }
        std::vector<VkPhysicalDevice> devices(count);
        const auto devicesResult =
            vkEnumeratePhysicalDevices(owner_->instance, &count, devices.data());
        if (devicesResult != VK_SUCCESS) {
            std::fprintf(stderr,
                "Native fixture vkEnumeratePhysicalDevices(list) failed: %d\n",
                int(devicesResult));
            return;
        }
        for (const auto physical : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_2) {
                std::fprintf(stderr,
                    "Native fixture rejected %s: apiVersion=%u requires Vulkan 1.2\n",
                    properties.deviceName,
                    properties.apiVersion);
                continue;
            }
            VkPhysicalDeviceVulkan12Features supported12{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceVulkan11Features supported11{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
            supported11.pNext = &supported12;
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &supported11;
            vkGetPhysicalDeviceFeatures2(physical, &features);
            if (!supported12.timelineSemaphore) {
                std::fprintf(stderr,
                    "Native fixture rejected %s: timelineSemaphore=false\n",
                    properties.deviceName);
                continue;
            }
            // Slang graphics modules declare DrawParameters. Match the
            // production Vulkan bootstrap's queried Vulkan 1.1 enablement;
            // firstInstance=0 does not exempt a declared SPIR-V capability.
            if (enableDescriptorIndexing && !supported11.shaderDrawParameters) {
                std::fprintf(stderr,
                    "Native fixture rejected %s: shaderDrawParameters=false\n",
                    properties.deviceName);
                continue;
            }
            VkPhysicalDeviceVulkan12Features enabled12{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            enabled12.timelineSemaphore = VK_TRUE;
            VkPhysicalDeviceVulkan11Features enabled11{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
            enabled11.shaderDrawParameters = supported11.shaderDrawParameters;
            enabled11.pNext = &enabled12;
            VkPhysicalDeviceFeatures2 enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            enabled.pNext = &enabled11;
            enabled.features.drawIndirectFirstInstance =
                features.features.drawIndirectFirstInstance;
            if (enableDescriptorIndexing)
                rhi_vulkan::VulkanDescriptorIndexingFeatures::capture(
                    features.features, supported12)
                    .applyTo(enabled.features, enabled12);
            std::uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, queues.data());
            for (std::uint32_t family = 0; family < queueCount; ++family) {
                if (!(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    || !queues[family].queueCount || !queues[family].timestampValidBits)
                    continue;
                const float priority = 1.0f;
                VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                queueInfo.queueFamilyIndex = family;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &priority;
                VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                deviceInfo.pNext = &enabled;
                deviceInfo.queueCreateInfoCount = 1;
                deviceInfo.pQueueCreateInfos = &queueInfo;
                const auto deviceResult =
                    vkCreateDevice(physical, &deviceInfo, nullptr, &owner_->device);
                if (deviceResult != VK_SUCCESS) {
                    std::fprintf(stderr,
                        "Native fixture vkCreateDevice failed: device=%s family=%u result=%d\n",
                        properties.deviceName,
                        family,
                        int(deviceResult));
                    continue;
                }
                owner_->physical = physical;
                VkQueue queue = VK_NULL_HANDLE;
                vkGetDeviceQueue(owner_->device, family, 0, &queue);
                rhi_vulkan::VulkanRhiRuntimeDesc desc;
                desc.enableSyntheticRuntime = false;
                desc.nativeOwner = owner_;
                desc.instance = owner_->instance;
                desc.physicalDevice = physical;
                desc.logicalDevice = owner_->device;
                desc.graphicsQueue = queue;
                desc.presentQueue = useGraphicsQueueForPresent ? queue : VK_NULL_HANDLE;
                desc.graphicsQueueFamilyIndex = family;
                desc.queueFamilyCount = 1;
                desc.getInstanceProcAddr = vkGetInstanceProcAddr;
                desc.getDeviceProcAddr = deviceResolver;
                desc.descriptorIndexingEnablementReported = true;
                desc.descriptorIndexingEnabled =
                    rhi_vulkan::VulkanDescriptorIndexingFeatures::capture(
                        enabled.features, enabled12);
                desc.indirectFirstInstanceEnablementReported = true;
                desc.indirectFirstInstanceEnabled =
                    enabled.features.drawIndirectFirstInstance == VK_TRUE;
                desc.timelineSemaphoreEnablementReported = true;
                desc.timelineSemaphoreEnabled = true;
                runtime_ = rhi_vulkan::createVulkanRhiRuntime(desc);
                if (!runtime_)
                    std::fprintf(stderr,
                        "Native fixture createVulkanRhiRuntime failed: device=%s family=%u\n",
                        properties.deviceName,
                        family);
                std::printf("Native Vulkan device: %s\n", properties.deviceName);
                return;
            }
        }
        std::fprintf(
            stderr, "Native fixture unavailable after examining %u physical devices\n", count);
    }

    ~VulkanHeadlessRuntime() = default;

    VulkanHeadlessRuntime(const VulkanHeadlessRuntime&) = delete;
    VulkanHeadlessRuntime& operator=(const VulkanHeadlessRuntime&) = delete;

    const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime() const { return runtime_; }

    VkDevice device() const { return owner_->device; }

    std::weak_ptr<rhi_vulkan::VulkanRhiNativeRuntimeOwner> nativeOwner() const { return owner_; }

private:
    std::shared_ptr<NativeOwner> owner_;
    std::shared_ptr<rhi_vulkan::VulkanRhiRuntime> runtime_;
};

} // namespace arp::tests
