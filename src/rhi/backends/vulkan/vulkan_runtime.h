#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_common.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <vulkan/vulkan.h>

// region Vulkan runtime
namespace arp::rhi_vulkan {

// Backend-private capture of the feature bits actually passed to vkCreateDevice.
// SampledImage also covers separately indexed samplers. RO/RW buffers share one
// indexing feature. No runtime array/update-after-bind feature is requested.
enum class VulkanIndexedResource : std::size_t {
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
    Count
};

struct VulkanDescriptorIndexingFeatures {
    static constexpr std::size_t kResourceClasses =
        static_cast<std::size_t>(VulkanIndexedResource::Count);
    std::array<bool, kResourceClasses> dynamicallyUniform{};
    std::array<bool, kResourceClasses> nonUniform{};
    bool descriptorIndexing = false;
    bool vertexStoresAndAtomics = false;
    bool fragmentStoresAndAtomics = false;
    static VulkanDescriptorIndexingFeatures capture(
        const VkPhysicalDeviceFeatures&, const VkPhysicalDeviceVulkan12Features&) noexcept;
    void applyTo(VkPhysicalDeviceFeatures&, VkPhysicalDeviceVulkan12Features&) const noexcept;
    bool subsetOf(const VulkanDescriptorIndexingFeatures&) const noexcept;
};

struct VulkanDescriptorIndexingInspection {
    bool queried = false;
    bool enablementReported = false;
    bool enabledFeaturesSupported = false;
    VulkanDescriptorIndexingFeatures supported;
    VulkanDescriptorIndexingFeatures enabled;
    VkPhysicalDeviceLimits limits{};
    // Physical maintenance4 query, independent of storage binding range.
    bool maintenance4Queried = false;
    VkDeviceSize maxBufferSize = 0u;
    const char* diagnosticId = "RHI-VULKAN-INDEXING-NOT-QUERIED";

    // Report validity, not a capability bit: a valid report may contain only
    // disabled features and never establishes executable bindless support.
    bool enablementVerified() const noexcept
    {
        return queried && enablementReported && enabledFeaturesSupported;
    }
};

// NativeRuntimeOwner/1. Implemented by the actual native bootstrap; retaining
// this object must retain the immutable instance/device identity and teardown.
// It must never own a runtime, which would introduce an ownership cycle.
class VulkanRhiNativeRuntimeOwner {
public:
    virtual ~VulkanRhiNativeRuntimeOwner() = default;
    virtual VkInstance nativeInstance() const noexcept = 0;
    virtual VkPhysicalDevice nativePhysicalDevice() const noexcept = 0;
    virtual VkDevice nativeDevice() const noexcept = 0;

    VulkanRhiNativeRuntimeOwner(const VulkanRhiNativeRuntimeOwner&) = delete;
    VulkanRhiNativeRuntimeOwner& operator=(const VulkanRhiNativeRuntimeOwner&) = delete;

protected:
    VulkanRhiNativeRuntimeOwner() = default;
};

struct VulkanRhiRuntimeDesc {
    std::pmr::string label;
    std::shared_ptr<VulkanRhiNativeRuntimeOwner> nativeOwner;
    // Explicit validator opt-in only. Production defaults require a real owner.
    bool enableSyntheticRuntime = false;
    bool nativeHandlesPrivate = true;
    std::uint32_t queueFamilyCount = 2;
    bool hasDeviceLocalMemory = true;
    bool hasHostVisibleMemory = true;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice logicalDevice = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    std::uint32_t graphicsQueueFamilyIndex = 0;
    std::uint32_t presentQueueFamilyIndex = 0;
    bool descriptorIndexingEnablementReported = false;
    VulkanDescriptorIndexingFeatures descriptorIndexingEnabled;
    bool indirectFirstInstanceEnablementReported = false;
    bool indirectFirstInstanceEnabled = false;
    bool timelineSemaphoreEnablementReported = false;
    bool timelineSemaphoreEnabled = false;
    bool swapchainMaintenance1Enabled = false;
};

// Backend-owned finite present-completion domain. One slot is reserved before
// a native present, so accepted queue work never depends on a later allocation.
inline constexpr std::size_t kVulkanMaxPendingPresentFences = 8u;

struct VulkanRhiRuntimeInspection {
    bool success = false;
    bool runtimeOwned = false;
    bool nativeHandlesPrivate = true;
    bool nativeDeviceAvailable = false;
    bool nativeSurfaceAvailable = false;
    bool syntheticRuntime = false;
    std::uint32_t queueFamilyCount = 0;
    std::pmr::string diagnostics;
};

class VulkanRhiRuntime final {
public:
    explicit VulkanRhiRuntime(const VulkanRhiRuntimeDesc& desc);

    bool valid() const;
    const char* admissionDiagnostic() const noexcept;
    bool syntheticRuntime() const;
    bool nativeHandlesPrivate() const;
    // Cached native queue-family facts; synthetic and lost runtimes are closed.
    rhi::CoreCapabilities coreQueueCapabilities() const noexcept;
    const char* coreQueueCapabilityDiagnostic() const noexcept;
    const std::pmr::string& label() const;
    VkInstance instance() const;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr() const;
    PFN_vkVoidFunction getDeviceProcAddr(const char* name) const;
    VkPhysicalDevice physicalDevice() const;
    VkDevice logicalDevice() const;
    VkQueue graphicsQueue() const;
    VkQueue presentQueue() const;
    VkSurfaceKHR surface() const;
    bool presentFenceEnabled() const noexcept;
    bool reservePresentFence(VkFence& fence, std::uint64_t& token, std::pmr::string& diagnostics);
    bool presentFenceCompleted(std::uint64_t token) const;
    bool waitPresentFenceUntil(std::uint64_t token,
        std::chrono::steady_clock::time_point deadline,
        std::pmr::string& diagnostics);
    void abandonPresentFence(std::uint64_t token) const;
    bool drainPresentFencesUntil(
        std::chrono::steady_clock::time_point deadline, std::pmr::string& diagnostics);
    std::uint32_t graphicsQueueFamilyIndex() const;
    std::uint32_t presentQueueFamilyIndex() const;
    std::uint32_t queueFamilyCount() const;
    bool refreshSurfaceDetails();
    const VkPhysicalDeviceMemoryProperties& memoryProperties() const;
    const VkPhysicalDeviceLimits& nativeLimits() const noexcept;
    /// Device upper bound for anisotropic filtering
    /// (VkPhysicalDeviceLimits::maxSamplerAnisotropy). Returns 1.0 when the
    /// properties were not queried (synthetic runtime).
    float maxSamplerAnisotropy() const;
    const VulkanDescriptorIndexingInspection& descriptorIndexing() const;
    bool indirectFirstInstanceEnabled() const noexcept;
    bool timelineSemaphoreEnabled() const noexcept;
    const VkSurfaceCapabilitiesKHR& surfaceCapabilities() const;
    const std::pmr::vector<VkSurfaceFormatKHR>& surfaceFormats() const;
    const std::pmr::vector<VkPresentModeKHR>& presentModes() const;

private:
    friend class VulkanOriginalDeviceNativeResultLoss;
    friend class VulkanRhiNativeCommandPayload;
    friend class VulkanRhiAllocator;
    friend class VulkanRhiNativeResourceAllocator;
    friend class VulkanRhiTimelineSemaphorePayload;
    friend class VulkanRhiHeapPlacement;
    friend const char* waitIdleAndRetireVulkanRhiSubmittedCommands(
        const std::shared_ptr<VulkanRhiRuntime>&);
    const char* attachOriginalResultLoss(
        const std::shared_ptr<class VulkanOriginalDeviceNativeResultLoss>&) noexcept;
    const char* detachOriginalResultLoss(
        const class VulkanOriginalDeviceNativeResultLoss*) noexcept;
    const char* validateOriginalResultLoss(
        const class VulkanOriginalDeviceNativeResultLoss*) const noexcept;
    // Closed native-call consumers only. An arbitrary caller cannot inject a
    // VkResult to invalidate the original logical device or its content journal.
    void captureOriginalDeviceLoss(VkResult) const noexcept;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

std::shared_ptr<VulkanRhiRuntime> createVulkanRhiRuntime(const VulkanRhiRuntimeDesc& desc);

VulkanRhiRuntimeInspection inspectVulkanRhiRuntime(const VulkanRhiRuntime& runtime);

// endregion Vulkan runtime

// region Vulkan handle utils

// Vulkan permits dispatchable handles to be pointers and non-dispatchable
// handles to be either pointers or integer types, depending on the platform
// headers. Keep synthetic handles and diagnostics valid for both definitions.
template <typename Handle> std::uintptr_t handleToUint(Handle handle) noexcept
{
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<std::uintptr_t>(handle);
    } else {
        return static_cast<std::uintptr_t>(handle);
    }
}

template <typename Handle> Handle syntheticHandle(std::uintptr_t value) noexcept
{
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

// endregion Vulkan handle utils

// region Vulkan wait policy

class VulkanRhiFrameWaitPolicy final {
public:
    static constexpr std::uint64_t frameFenceTimeoutNanoseconds = 1000000000ull;
    static constexpr std::uint64_t acquireNextImageTimeoutNanoseconds = 1000000000ull;
};

} // namespace arp::rhi_vulkan

// endregion Vulkan wait policy
