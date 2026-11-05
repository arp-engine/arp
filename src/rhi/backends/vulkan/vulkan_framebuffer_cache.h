#pragma once

#include "rhi/backends/vulkan/vulkan_render_pass_resources.h"

namespace arp::rhi_vulkan {

// OrdinaryRenderPassReuse/1. The weak storage owner identifies an immutable
// physical image version; it never keeps the image/runtime alive. A swapchain
// uses its original native lifetime owner, not an ephemeral acquired wrapper.
struct VulkanRhiFramebufferKey {
    struct Attachment {
        std::weak_ptr<void> owner;
        const void* object = nullptr;
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageSubresourceRange range{};

        bool matches(const Attachment& other) const noexcept
        {
            return !owner.expired() && !other.owner.expired() && !owner.owner_before(other.owner)
                && !other.owner.owner_before(owner) && object == other.object
                && image == other.image && format == other.format
                && range.aspectMask == other.range.aspectMask
                && range.baseMipLevel == other.range.baseMipLevel
                && range.levelCount == other.range.levelCount
                && range.baseArrayLayer == other.range.baseArrayLayer
                && range.layerCount == other.range.layerCount;
        }
    };

    std::array<Attachment, VulkanRhiNativeRenderPassResources::kMaxImageViews> attachments;
    std::uint32_t count = 0u, width = 0u, height = 0u, layers = 1u;

    bool valid() const noexcept
    {
        if (!count || count > attachments.size() || !width || !height || !layers)
            return false;
        for (std::uint32_t i = 0u; i < count; ++i)
            if (attachments[i].owner.expired() || !attachments[i].image
                || attachments[i].format == VK_FORMAT_UNDEFINED || !attachments[i].range.aspectMask
                || !attachments[i].range.levelCount || !attachments[i].range.layerCount)
                return false;
        return true;
    }

    bool matches(const VulkanRhiFramebufferKey& other) const noexcept
    {
        if (count != other.count || width != other.width || height != other.height
            || layers != other.layers || count > attachments.size())
            return false;
        for (std::uint32_t i = 0u; i < count; ++i)
            if (!attachments[i].matches(other.attachments[i]))
                return false;
        return true;
    }
};

// Each entry owns one render-pass lease plus its framebuffer/views. Recording
// leases borrow that bundle. Only cancellation/real completion releases a
// recording lease; an idle bundle may then be evicted or cleared.
class VulkanRhiFramebufferCache final {
public:
    static constexpr std::uint32_t capacity = VulkanRhiRenderPassCache::capacity;

    bool acquire(const VulkanRhiFramebufferKey& key,
        VkRenderPass pass,
        VulkanRhiNativeRenderPassResources& output) noexcept
    {
        for (std::uint32_t i = 0u; i < capacity; ++i) {
            auto& entry = entries_[i];
            if (entry.resources.framebuffer && entry.resources.renderPass == pass
                && entry.users < std::numeric_limits<std::uint32_t>::max()
                && entry.key.matches(key)) {
                ++entry.users;
                output = entry.resources;
                output.framebufferCacheSlot = i;
                output.currentSubpassIndex = 0u;
                return true;
            }
        }
        return false;
    }

    template <class Destroy>
    void admit(const VulkanRhiFramebufferKey& key,
        VulkanRhiNativeRenderPassResources& resources,
        Destroy&& destroy) noexcept
    {
        if (!key.valid() || resources.originalContent || !resources.framebuffer
            || resources.framebufferCacheSlot != VulkanRhiRenderPassCache::uncached)
            return;
        std::uint32_t available = VulkanRhiRenderPassCache::uncached;
        for (std::uint32_t i = 0u; i < capacity; ++i) {
            if (entries_[i].users)
                continue;
            if (available == VulkanRhiRenderPassCache::uncached)
                available = i;
            if (!entries_[i].resources.framebuffer) {
                available = i;
                break;
            }
        }
        if (available == VulkanRhiRenderPassCache::uncached)
            return; // Existing per-command native ownership remains in force.
        auto& entry = entries_[available];
        destroy(entry.resources);
        entry.key = key;
        entry.resources = resources;
        entry.users = 1u;
        resources.framebufferCacheSlot = available;
    }

    bool release(std::uint32_t slot, VkFramebuffer framebuffer) noexcept
    {
        if (slot >= capacity || !entries_[slot].users
            || entries_[slot].resources.framebuffer != framebuffer)
            return false;
        --entries_[slot].users;
        return true;
    }

    template <class Destroy> void clearIdle(Destroy&& destroy) noexcept
    {
        for (auto& entry : entries_)
            if (!entry.users) {
                destroy(entry.resources);
                entry.key = {};
            }
    }

private:
    struct Entry {
        VulkanRhiFramebufferKey key;
        VulkanRhiNativeRenderPassResources resources;
        std::uint32_t users = 0u;
    };

    std::array<Entry, capacity> entries_;
};
} // namespace arp::rhi_vulkan
