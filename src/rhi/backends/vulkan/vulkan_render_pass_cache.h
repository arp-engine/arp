#pragma once

#include "core/memory/cpu_allocator.h"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vulkan/vulkan.h>

namespace arp::rhi_vulkan {

// OrdinaryRenderPassReuse/1. Immutable native lowering only; no attachment,
// content journal, runtime owner or execution authority is retained here.
// Access uses the runtime's existing serialized command owner.
class VulkanRhiRenderPassCache final {
public:
    static constexpr std::uint32_t capacity = 64u;
    static constexpr std::uint32_t uncached = std::numeric_limits<std::uint32_t>::max();

    VkResult acquire(VkDevice device,
        PFN_vkCreateRenderPass create,
        PFN_vkDestroyRenderPass destroy,
        const VkRenderPassCreateInfo& info,
        VkRenderPass& handle,
        std::uint32_t& slot,
        bool& created)
    {
        handle = VK_NULL_HANDLE;
        slot = uncached;
        created = false;
        if (!create || !destroy)
            return VK_ERROR_INITIALIZATION_FAILED;
        if (info.pNext)
            return createUncached(device, create, info, handle, created);
        std::uint32_t available = uncached;
        for (std::uint32_t i = 0; i < capacity; ++i) {
            auto& entry = entries_[i];
            if (entry.handle && entry.key.matches(info)
                && entry.users < std::numeric_limits<std::uint32_t>::max()) {
                ++entry.users;
                handle = entry.handle;
                slot = i;
                return VK_SUCCESS;
            }
            if (!entry.users
                && (available == uncached || (!entry.handle && entries_[available].handle)))
                available = i;
        }
        if (available == uncached)
            return createUncached(device, create, info, handle, created);
        Key key;
        key.assign(info);
        const auto result = createUncached(device, create, info, handle, created);
        if (result != VK_SUCCESS)
            return result;
        auto& entry = entries_[available];
        if (entry.handle)
            destroy(device, entry.handle, nullptr);
        entry.key = std::move(key);
        entry.handle = handle;
        entry.users = 1u;
        slot = available;
        return VK_SUCCESS;
    }

    bool release(std::uint32_t slot, VkRenderPass handle) noexcept
    {
        if (slot >= capacity || entries_[slot].handle != handle || !entries_[slot].users)
            return false;
        --entries_[slot].users;
        return true;
    }

    void clearIdle(VkDevice device, PFN_vkDestroyRenderPass destroy) noexcept
    {
        if (!destroy)
            return;
        for (auto& entry : entries_)
            if (entry.handle && !entry.users) {
                destroy(device, entry.handle, nullptr);
                entry.handle = VK_NULL_HANDLE;
                entry.key.words.clear();
            }
    }

private:
    // Field-wise normalization deliberately excludes C struct padding and all
    // pointer addresses. Lookup walks the caller's lowering without a scratch
    // vector, while cold admission owns its exact normalized words.
    struct Key {
        core::pmr::Vector<std::uint32_t> words;

        template <class Emit> static void visit(const VkRenderPassCreateInfo& info, Emit&& emit)
        {
            const auto reference = [&](const VkAttachmentReference& ref) {
                emit(ref.attachment);
                emit(ref.layout);
            };
            emit(info.flags);
            emit(info.attachmentCount);
            for (std::uint32_t i = 0; i < info.attachmentCount; ++i) {
                const auto& a = info.pAttachments[i];
                emit(a.flags);
                emit(a.format);
                emit(a.samples);
                emit(a.loadOp);
                emit(a.storeOp);
                emit(a.stencilLoadOp);
                emit(a.stencilStoreOp);
                emit(a.initialLayout);
                emit(a.finalLayout);
            }
            emit(info.subpassCount);
            for (std::uint32_t i = 0; i < info.subpassCount; ++i) {
                const auto& s = info.pSubpasses[i];
                emit(s.flags);
                emit(s.pipelineBindPoint);
                emit(s.inputAttachmentCount);
                for (std::uint32_t j = 0; j < s.inputAttachmentCount; ++j)
                    reference(s.pInputAttachments[j]);
                emit(s.colorAttachmentCount);
                for (std::uint32_t j = 0; j < s.colorAttachmentCount; ++j)
                    reference(s.pColorAttachments[j]);
                emit(s.pResolveAttachments != nullptr);
                if (s.pResolveAttachments)
                    for (std::uint32_t j = 0; j < s.colorAttachmentCount; ++j)
                        reference(s.pResolveAttachments[j]);
                emit(s.pDepthStencilAttachment != nullptr);
                if (s.pDepthStencilAttachment)
                    reference(*s.pDepthStencilAttachment);
                emit(s.preserveAttachmentCount);
                for (std::uint32_t j = 0; j < s.preserveAttachmentCount; ++j)
                    emit(s.pPreserveAttachments[j]);
            }
            emit(info.dependencyCount);
            for (std::uint32_t i = 0; i < info.dependencyCount; ++i) {
                const auto& d = info.pDependencies[i];
                emit(d.srcSubpass);
                emit(d.dstSubpass);
                emit(d.srcStageMask);
                emit(d.dstStageMask);
                emit(d.srcAccessMask);
                emit(d.dstAccessMask);
                emit(d.dependencyFlags);
            }
        }

        void assign(const VkRenderPassCreateInfo& info)
        {
            visit(info, [&](std::uint32_t word) {
                words.push_back(word);
            });
        }

        bool matches(const VkRenderPassCreateInfo& info) const noexcept
        {
            std::size_t position = 0u;
            bool equal = true;
            visit(info, [&](std::uint32_t word) {
                equal = equal && position < words.size() && words[position] == word;
                ++position;
            });
            return equal && position == words.size();
        }
    };

    struct Entry {
        Key key;
        VkRenderPass handle = VK_NULL_HANDLE;
        std::uint32_t users = 0u;
    };

    static VkResult createUncached(VkDevice device,
        PFN_vkCreateRenderPass create,
        const VkRenderPassCreateInfo& info,
        VkRenderPass& handle,
        bool& created)
    {
        const auto result = create(device, &info, nullptr, &handle);
        created = result == VK_SUCCESS;
        return result;
    }

    std::array<Entry, capacity> entries_;
};

} // namespace arp::rhi_vulkan
