#pragma once

#include "rhi/backends/vulkan/vulkan_render_pass_cache.h"

#include <array>
#include <memory>

namespace arp::rhi_vulkan {
struct VulkanRhiOrdinaryRenderPassContent;

struct VulkanRhiNativeRenderPassResources {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::uint32_t renderPassCacheSlot = VulkanRhiRenderPassCache::uncached;
    std::uint32_t framebufferCacheSlot = VulkanRhiRenderPassCache::uncached;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    // Existing ordinary lowering ceiling: eight colors and one depth view.
    static constexpr std::uint32_t kMaxImageViews = 9;
    std::array<VkImageView, kMaxImageViews> imageViews{};
    std::uint32_t imageViewCount = 0;
    std::uint32_t subpassCount = 1;
    std::uint32_t currentSubpassIndex = 0;
    std::shared_ptr<VulkanRhiOrdinaryRenderPassContent> originalContent;
};

// One cleanup implementation for ordinary command retirement, cache eviction
// and runtime teardown. Cached framebuffer leases are released by their cache
// before reaching this ownership path.
inline bool destroyVulkanRhiUncachedRenderPassResources(VkDevice device,
    PFN_vkDestroyFramebuffer destroyFramebuffer,
    PFN_vkDestroyImageView destroyView,
    PFN_vkDestroyRenderPass destroyPass,
    VulkanRhiRenderPassCache& passes,
    VulkanRhiNativeRenderPassResources& resources) noexcept
{
    if (resources.framebufferCacheSlot != VulkanRhiRenderPassCache::uncached)
        return false;
    if (destroyFramebuffer && resources.framebuffer)
        destroyFramebuffer(device, resources.framebuffer, nullptr);
    if (destroyView)
        for (std::uint32_t i = 0u; i < resources.imageViewCount; ++i)
            destroyView(device, resources.imageViews[i], nullptr);
    bool released = true;
    if (resources.renderPassCacheSlot != VulkanRhiRenderPassCache::uncached)
        released = passes.release(resources.renderPassCacheSlot, resources.renderPass);
    else if (destroyPass && resources.renderPass)
        destroyPass(device, resources.renderPass, nullptr);
    resources = {};
    return released;
}
} // namespace arp::rhi_vulkan
