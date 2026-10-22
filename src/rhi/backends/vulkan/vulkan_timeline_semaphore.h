#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"

namespace arp::rhi_vulkan {

// NativeTimelineOwnership/1. Immutable native identity, shared by the public
// wrapper and at most two fixed command submission references. No owner cycle.
class VulkanRhiTimelineSemaphorePayload final {
public:
    explicit VulkanRhiTimelineSemaphorePayload(std::shared_ptr<VulkanRhiRuntime> runtime);
    ~VulkanRhiTimelineSemaphorePayload();
    VulkanRhiTimelineSemaphorePayload(const VulkanRhiTimelineSemaphorePayload&) = delete;
    VulkanRhiTimelineSemaphorePayload& operator=(const VulkanRhiTimelineSemaphorePayload&) = delete;

    static std::shared_ptr<VulkanRhiTimelineSemaphorePayload> create(
        std::shared_ptr<VulkanRhiRuntime> runtime,
        std::uint64_t initialValue,
        core::pmr::String& diagnostic);
    VkSemaphore nativeHandle() const noexcept;
    bool belongsTo(const VulkanRhiRuntime& runtime) const noexcept;
    VkResult hostSignal(std::uint64_t value) const;
    VkResult hostWait(std::uint64_t value, std::uint64_t timeoutNs) const;
    VkResult queryCounter(std::uint64_t& value) const;

private:
    std::shared_ptr<VulkanRhiRuntime> runtime_;
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
    PFN_vkDestroySemaphore destroy_ = nullptr;
    PFN_vkSignalSemaphore signal_ = nullptr;
    PFN_vkWaitSemaphores wait_ = nullptr;
    PFN_vkGetSemaphoreCounterValue query_ = nullptr;
};

} // namespace arp::rhi_vulkan
