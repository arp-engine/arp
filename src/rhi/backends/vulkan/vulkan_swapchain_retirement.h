#pragma once

#include <cstddef>
#include <memory>
#include <vulkan/vulkan.h>

namespace arp::rhi_vulkan {
class VulkanRhiRuntime;
class VulkanRhiSwapchainFrameSyncState;

enum class VulkanPresentRetirementStatus { Pending, Ready, Failed };

struct VulkanPresentRetirementResult {
    VulkanPresentRetirementStatus status = VulkanPresentRetirementStatus::Pending;
    const char* diagnosticId = "";
    VkResult nativeResult = VK_NOT_READY;
};

// Backend-private ScopedPresentRetirement/1. Ready proves the existing legacy
// present queue idle, separately from exact graphics completion. May block.
VulkanPresentRetirementResult pollVulkanLegacyPresentRetirement(
    const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const std::shared_ptr<VulkanRhiSwapchainFrameSyncState>& frame);

// Cold SwapchainFailureRetention/1 pump. A nonzero result means native owners
// remain retained; diagnostics never converts a failed idle into completion.
std::size_t retireVulkanRhiSwapchainLifetimes(
    const std::shared_ptr<VulkanRhiRuntime>& runtime, const char*& diagnostic) noexcept;
std::size_t pendingVulkanRhiSwapchainLifetimes(
    const std::shared_ptr<VulkanRhiRuntime>& runtime) noexcept;
} // namespace arp::rhi_vulkan
