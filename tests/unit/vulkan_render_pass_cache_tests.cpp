#include "rhi/backends/vulkan/vulkan_framebuffer_cache.h"
#include "rhi/backends/vulkan/vulkan_render_pass_cache.h"

#include <cassert>
#include <type_traits>

namespace {
using Cache = arp::rhi_vulkan::VulkanRhiRenderPassCache;
std::uint32_t creations = 0u, destructions = 0u;
bool failCreation = false;

VKAPI_ATTR VkResult VKAPI_CALL createPass(
    VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass* pass)
{
    if (failCreation)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *pass = reinterpret_cast<VkRenderPass>(static_cast<std::uintptr_t>(++creations));
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice, VkRenderPass pass, const VkAllocationCallbacks*)
{
    assert(pass);
    ++destructions;
}

struct Input {
    VkAttachmentDescription attachment{};
    VkAttachmentReference color{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference input{0u, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkAttachmentReference resolve{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth{
        VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    std::uint32_t preserve = 0u;
    VkSubpassDescription subpass{};
    VkSubpassDependency dependency{};
    VkRenderPassCreateInfo info{};

    Input()
    {
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = attachment.finalLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = subpass.inputAttachmentCount =
            subpass.preserveAttachmentCount = 1u;
        subpass.pColorAttachments = &color;
        subpass.pInputAttachments = &input;
        subpass.pResolveAttachments = &resolve;
        subpass.pDepthStencilAttachment = &depth;
        subpass.pPreserveAttachments = &preserve;
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0u;
        dependency.srcStageMask = dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = info.subpassCount = info.dependencyCount = 1u;
        info.pAttachments = &attachment;
        info.pSubpasses = &subpass;
        info.pDependencies = &dependency;
    }
};

struct Lease {
    VkRenderPass handle = VK_NULL_HANDLE;
    std::uint32_t slot = Cache::uncached;
    bool created = false;
};

Lease acquire(Cache& cache, const Input& input)
{
    Lease lease;
    assert(cache.acquire(VK_NULL_HANDLE,
               createPass,
               destroyPass,
               input.info,
               lease.handle,
               lease.slot,
               lease.created)
        == VK_SUCCESS);
    return lease;
}

void release(Cache& cache, const Lease& lease)
{
    if (lease.slot == Cache::uncached)
        destroyPass(VK_NULL_HANDLE, lease.handle, nullptr);
    else
        assert(cache.release(lease.slot, lease.handle));
}

std::uint32_t framebufferDestroys = 0u, viewDestroys = 0u;

VKAPI_ATTR void VKAPI_CALL destroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks*)
{
    ++framebufferDestroys;
}

VKAPI_ATTR void VKAPI_CALL destroyView(VkDevice, VkImageView, const VkAllocationCallbacks*)
{
    ++viewDestroys;
}

void verifyFramebufferCache()
{
    using namespace arp::rhi_vulkan;
    Cache passes;
    VulkanRhiFramebufferCache frames;
    Input input;
    auto owner = arp::core::makeShared<int>(1);
    VulkanRhiFramebufferKey key;
    key.count = 1u;
    key.width = key.height = 8u;
    key.attachments[0].owner = owner;
    key.attachments[0].object = owner.get();
    key.attachments[0].image = reinterpret_cast<VkImage>(1u);
    key.attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    key.attachments[0].range = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    const auto destroy = [&](auto& native) {
        assert(destroyVulkanRhiUncachedRenderPassResources(
            VK_NULL_HANDLE, destroyFramebuffer, destroyView, destroyPass, passes, native));
    };
    const auto native = [&](std::uintptr_t identity) {
        VulkanRhiNativeRenderPassResources result;
        bool created = false;
        assert(passes.acquire(VK_NULL_HANDLE,
                   createPass,
                   destroyPass,
                   input.info,
                   result.renderPass,
                   result.renderPassCacheSlot,
                   created)
            == VK_SUCCESS);
        result.framebuffer = reinterpret_cast<VkFramebuffer>(identity);
        result.imageViewCount = 1u;
        result.imageViews[0] = reinterpret_cast<VkImageView>(identity);
        return result;
    };
    auto first = native(100u);
    frames.admit(key, first, destroy);
    assert(first.framebufferCacheSlot != Cache::uncached);
    const auto initialDestructions = framebufferDestroys;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0u; i < 100u; ++i) {
        VulkanRhiNativeRenderPassResources reused;
        assert(frames.acquire(key, first.renderPass, reused));
        assert(reused.framebuffer == first.framebuffer && reused.imageViews == first.imageViews);
        frames.clearIdle(destroy);
        assert(framebufferDestroys == initialDestructions);
        assert(frames.release(reused.framebufferCacheSlot, reused.framebuffer));
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    passes.clearIdle(VK_NULL_HANDLE, destroyPass); // The bundle still owns its pass lease.
    assert(!frames.release(first.framebufferCacheSlot, reinterpret_cast<VkFramebuffer>(101u)));
    const auto miss = [&](auto change) {
        auto changed = key;
        change(changed);
        VulkanRhiNativeRenderPassResources output;
        assert(!frames.acquire(changed, first.renderPass, output));
    };
    miss([](auto& x) {
        ++x.width;
    });
    miss([](auto& x) {
        ++x.layers;
    });
    miss([](auto& x) {
        ++x.attachments[0].range.baseMipLevel;
    });
    miss([](auto& x) {
        ++x.attachments[0].range.baseArrayLayer;
    });
    miss([](auto& x) {
        x.attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    });
    miss([](auto& x) {
        x.attachments[0].image = reinterpret_cast<VkImage>(2u);
    });
    auto otherOwner = arp::core::makeShared<int>(2);
    miss([&](auto& x) {
        x.attachments[0].owner = otherOwner;
    });
    assert(frames.release(first.framebufferCacheSlot, first.framebuffer));
    owner.reset();
    VulkanRhiNativeRenderPassResources rejected;
    assert(!frames.acquire(key, first.renderPass, rejected)); // No live storage proof.
    // Same native handles, new storage owner: never an ABA cache hit.
    key.attachments[0].owner = otherOwner;
    assert(!frames.acquire(key, first.renderPass, rejected));
    frames.clearIdle(destroy);
    passes.clearIdle(VK_NULL_HANDLE, destroyPass);
    assert(creations == destructions);

    // Different acquired wrappers can share the explicitly identified original
    // swapchain storage epoch; its image handle still distinguishes each image.
    key.attachments[0].object = nullptr;
    first = native(200u);
    frames.admit(key, first, destroy);
    assert(frames.acquire(key, first.renderPass, rejected));
    assert(frames.release(rejected.framebufferCacheSlot, rejected.framebuffer));
    assert(frames.release(first.framebufferCacheSlot, first.framebuffer));
    frames.clearIdle(destroy);
    passes.clearIdle(VK_NULL_HANDLE, destroyPass);
    assert(creations == destructions);

    std::array<VulkanRhiNativeRenderPassResources, VulkanRhiFramebufferCache::capacity> held;
    for (std::uint32_t i = 0; i < held.size(); ++i) {
        held[i] = native(300u + i);
        key.width = i + 1u;
        frames.admit(key, held[i], destroy);
        assert(held[i].framebufferCacheSlot != Cache::uncached);
    }
    auto overflow = native(500u);
    frames.admit(key, overflow, destroy);
    assert(overflow.framebufferCacheSlot == Cache::uncached);
    destroy(overflow); // Full in-flight cache preserves ordinary native ownership.
    const auto before = framebufferDestroys;
    frames.clearIdle(destroy);
    assert(framebufferDestroys == before);
    for (const auto& resource : held)
        assert(frames.release(resource.framebufferCacheSlot, resource.framebuffer));
    frames.clearIdle(destroy);
    passes.clearIdle(VK_NULL_HANDLE, destroyPass);
    assert(creations == destructions && framebufferDestroys == viewDestroys);
}
} // namespace

int main()
{
    Cache cache;
    Input input;
    auto first = acquire(cache, input);
    assert(first.created && first.slot != Cache::uncached);
    // Equal lowering at different addresses must share one immutable object.
    Input equivalent;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0; i < 100u; ++i) {
        const auto next = acquire(cache, equivalent);
        assert(!next.created && next.handle == first.handle && next.slot == first.slot);
        release(cache, next);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
    cache.clearIdle(VK_NULL_HANDLE, destroyPass);
    assert(destructions == 0u); // An outstanding recording lease is never evicted.
    assert(!cache.release(Cache::uncached, first.handle));

    // Ordered attachment, subpass and dependency fields all participate.
    const auto different = [&](auto change) {
        Input changed;
        change(changed);
        const auto lease = acquire(cache, changed);
        assert(lease.created && lease.handle != first.handle);
        release(cache, lease);
        cache.clearIdle(VK_NULL_HANDLE, destroyPass);
    };
    different([](Input& x) {
        x.info.flags = VK_RENDER_PASS_CREATE_TRANSFORM_BIT_QCOM;
    });
    different([](Input& x) {
        x.attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    });
    different([](Input& x) {
        x.attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    });
    different([](Input& x) {
        x.attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    });
    different([](Input& x) {
        x.attachment.samples = VK_SAMPLE_COUNT_4_BIT;
    });
    different([](Input& x) {
        x.attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    });
    different([](Input& x) {
        x.attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    });
    different([](Input& x) {
        x.subpass.flags = VK_SUBPASS_DESCRIPTION_PER_VIEW_ATTRIBUTES_BIT_NVX;
    });
    different([](Input& x) {
        x.color.layout = VK_IMAGE_LAYOUT_GENERAL;
    });
    different([](Input& x) {
        x.input.attachment = VK_ATTACHMENT_UNUSED;
    });
    different([](Input& x) {
        x.subpass.pResolveAttachments = nullptr;
    });
    different([](Input& x) {
        x.subpass.pDepthStencilAttachment = nullptr;
    });
    different([](Input& x) {
        x.depth.layout = VK_IMAGE_LAYOUT_GENERAL;
    });
    different([](Input& x) {
        x.preserve = 1u;
    });
    different([](Input& x) {
        x.dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    });
    different([](Input& x) {
        x.dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    });
    // Driver failure must leave existing leases and future creation intact.
    input.attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    failCreation = true;
    Lease failed;
    assert(cache.acquire(VK_NULL_HANDLE,
               createPass,
               destroyPass,
               input.info,
               failed.handle,
               failed.slot,
               failed.created)
        == VK_ERROR_OUT_OF_DEVICE_MEMORY);
    assert(!failed.handle && !failed.created && failed.slot == Cache::uncached);
    failCreation = false;
    release(cache, acquire(cache, input));
    release(cache, first);
    cache.clearIdle(VK_NULL_HANDLE, destroyPass);
    assert(creations == destructions);

    // Full in-flight cache: overflow uses original native creation, never
    // steals a lease, expands the cache, or pretends allocation succeeded.
    std::array<Lease, Cache::capacity> held;
    for (std::uint32_t i = 0; i < Cache::capacity; ++i) {
        input.dependency.srcAccessMask = i;
        held[i] = acquire(cache, input);
        assert(held[i].slot != Cache::uncached);
    }
    input.dependency.srcAccessMask = Cache::capacity;
    auto overflow = acquire(cache, input);
    assert(overflow.created && overflow.slot == Cache::uncached);
    release(cache, overflow);
    // Only the completed/cancelled entry is eligible for replacement.
    release(cache, held[0]);
    overflow = acquire(cache, input);
    assert(overflow.slot == held[0].slot && overflow.created);
    release(cache, overflow);
    for (std::size_t i = 1; i < held.size(); ++i)
        release(cache, held[i]);
    cache.clearIdle(VK_NULL_HANDLE, destroyPass);
    assert(creations == destructions);

    VkBaseInStructure extension{};
    input.info.pNext = &extension;
    const auto extended = acquire(cache, input);
    assert(extended.slot == Cache::uncached && extended.created);
    release(cache, extended);
    assert(creations == destructions);
    verifyFramebufferCache();
}
