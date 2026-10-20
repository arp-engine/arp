#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_conformance_clear.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/command_attachment_validation.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace arp::rhi_vulkan {

namespace {

constexpr std::uint32_t kScratchIndexCapacityBytes = 4u * 1024u * 1024u;
constexpr std::uint32_t kScratchIndexCapacity =
    kScratchIndexCapacityBytes / static_cast<std::uint32_t>(sizeof(std::uint32_t));

template <typename Fn> Fn functionFrom(PFN_vkVoidFunction value)
{
    Fn result = nullptr;
    static_assert(sizeof(result) == sizeof(value), "unexpected Vulkan function pointer size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::pmr::string vkFailure(const char* operation, VkResult result)
{
    std::ostringstream message;
    message << operation << " failed: " << static_cast<int>(result);
    return arp::core::toPmrString(message.str());
}

VkAttachmentLoadOp loadOpFor(rhi::LoadOp op)
{
    switch (op) {
    case rhi::LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case rhi::LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case rhi::LoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

VkAttachmentStoreOp storeOpFor(rhi::StoreOp op)
{
    switch (op) {
    case rhi::StoreOp::Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case rhi::StoreOp::DontCare:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

VkShaderStageFlags shaderStagesFor(rhi::ShaderStage stages)
{
    VkShaderStageFlags flags = 0;
    if ((stages & rhi::ShaderStage::Vertex) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if ((stages & rhi::ShaderStage::Fragment) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if ((stages & rhi::ShaderStage::Compute) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

VkIndexType indexTypeFor(rhi::IndexFormat format)
{
    switch (format) {
    case rhi::IndexFormat::Uint16:
        return VK_INDEX_TYPE_UINT16;
    case rhi::IndexFormat::Uint32:
        return VK_INDEX_TYPE_UINT32;
    }
    return VK_INDEX_TYPE_UINT32;
}

VkImageAspectFlags depthStencilAspectMaskFor(VkFormat format)
{
    if (format == VK_FORMAT_D24_UNORM_S8_UINT) {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_DEPTH_BIT;
}

bool ensureScratchIndexBuffer(VulkanRhiNativeCommandPayload::Impl& impl,
    std::uint32_t indexCount,
    std::pmr::string& diagnostics)
{
    if (indexCount > kScratchIndexCapacity) {
        diagnostics = "Vulkan RHI indexed draw exceeds bounded scratch index capacity";
        return false;
    }
    if (impl.scratchIndexBuffer && impl.scratchIndexCapacity >= indexCount) {
        return true;
    }
    if (!impl.allocator) {
        diagnostics = "Vulkan RHI indexed draw requires a backend VMA allocator";
        return false;
    }

    const auto allocated = impl.allocator->createBuffer(
        rhi::BufferDesc{
            "NativeIndexedDrawScratch",
            kScratchIndexCapacityBytes,
            rhi::ResourceUsage::IndexBuffer,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::Undefined,
        },
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!allocated.success) {
        diagnostics = allocated.diagnostics;
        return false;
    }
    if (!allocated.buffer || !allocated.mappedData || !allocated.payload) {
        diagnostics = "Vulkan RHI indexed draw VMA scratch allocation is incomplete";
        return false;
    }

    auto* indices = static_cast<std::uint32_t*>(allocated.mappedData);
    for (std::uint32_t index = 0; index < kScratchIndexCapacity; ++index) {
        indices[index] = index;
    }

    impl.scratchIndexBuffer = allocated.buffer;
    impl.scratchIndexAllocationPayload = allocated.payload;
    impl.scratchIndexCapacity = kScratchIndexCapacity;
    return true;
}

bool adoptSwapchainFrameSync(VulkanRhiNativeCommandPayload::Impl& impl,
    const VulkanRhiNativeResourcePayload& colorTarget,
    std::pmr::string& diagnostics)
{
    if (!colorTarget.backendVulkanSwapchainFrameSyncAvailable()) {
        return true;
    }

    const auto waitSemaphore = colorTarget.backendVulkanAcquireSemaphore();
    const auto signalSemaphore = colorTarget.backendVulkanRenderFinishedSemaphore();
    if (!waitSemaphore || !signalSemaphore) {
        diagnostics =
            "Vulkan RHI swapchain frame sync requires acquire and render-finished semaphores";
        return false;
    }
    if (impl.hasSwapchainFrameSync
        && (impl.waitSemaphore != waitSemaphore || impl.signalSemaphore != signalSemaphore)) {
        diagnostics = "Vulkan RHI command buffer cannot mix multiple swapchain frame sync payloads";
        return false;
    }

    impl.hasSwapchainFrameSync = true;
    impl.waitSemaphore = waitSemaphore;
    impl.signalSemaphore = signalSemaphore;
    impl.swapchainFrameSyncState = colorTarget.backendVulkanSwapchainFrameSyncState();
    impl.frameSerial = std::max(impl.frameSerial, colorTarget.backendVulkanSwapchainFrameSerial());
    return true;
}

} // namespace

void destroyVulkanRhiNativeRenderPassResources(
    const VulkanRhiRuntime& device, VulkanRhiNativeRenderPassResources& resources)
{
    auto* destroyFramebuffer =
        functionFrom<PFN_vkDestroyFramebuffer>(device.getDeviceProcAddr("vkDestroyFramebuffer"));
    auto* destroyImageView =
        functionFrom<PFN_vkDestroyImageView>(device.getDeviceProcAddr("vkDestroyImageView"));
    auto* destroyRenderPass =
        functionFrom<PFN_vkDestroyRenderPass>(device.getDeviceProcAddr("vkDestroyRenderPass"));
    if (destroyFramebuffer && resources.framebuffer) {
        destroyFramebuffer(device.logicalDevice(), resources.framebuffer, nullptr);
    }
    if (destroyImageView) {
        for (std::uint32_t index = 0; index < resources.imageViewCount; ++index) {
            destroyImageView(device.logicalDevice(), resources.imageViews[index], nullptr);
        }
    }
    if (destroyRenderPass && resources.renderPass) {
        destroyRenderPass(device.logicalDevice(), resources.renderPass, nullptr);
    }
    resources = {};
}

bool VulkanRhiNativeCommandPayload::recordRenderPassBegin(const rhi::RenderPassDesc& desc,
    const std::pmr::vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>& colorTargets,
    const VulkanRhiNativeResourcePayload* depthTarget,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->begin(desc, colorTargets, depthTarget, diagnostics))
        return false;
    const auto operations = rhi::semantics::validateRenderPassAttachmentOperations(desc);
    if (!operations.success) {
        diagnostics = operations.diagnostics;
        return false;
    }
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native render pass is already active";
        return false;
    }
    const bool depthOnlyRenderPass =
        desc.colorAttachments.empty() && desc.hasDepthStencilAttachment;
    if (desc.colorAttachments.empty() && !depthOnlyRenderPass) {
        diagnostics = "Vulkan RHI native render pass requires color or depth attachment";
        return false;
    }
    if (!depthOnlyRenderPass && colorTargets.size() != desc.colorAttachments.size()) {
        diagnostics = "Vulkan RHI native render pass requires a tracked color target";
        return false;
    }
    if (depthOnlyRenderPass && !depthTarget) {
        diagnostics = "Vulkan RHI native depth-only render pass requires a tracked depth target";
        return false;
    }
    for (const auto& colorTarget : colorTargets) {
        if (colorTarget && colorTarget->backendVulkanOriginalContentExclusive()) {
            diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
            return false;
        }
        if (colorTarget && !adoptSwapchainFrameSync(*impl_, *colorTarget, diagnostics))
            return false;
    }
    if (depthTarget && depthTarget->backendVulkanOriginalContentExclusive()) {
        diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        impl_->activeRenderPass.renderPass = syntheticHandle<VkRenderPass>(1u);
        impl_->activeRenderPass.framebuffer = syntheticHandle<VkFramebuffer>(1u);
        impl_->activeRenderPass.imageViews[0] = syntheticHandle<VkImageView>(1u);
        if (!depthOnlyRenderPass && desc.hasDepthStencilAttachment) {
            impl_->activeRenderPass.imageViews[1] = syntheticHandle<VkImageView>(2u);
        }
        impl_->activeRenderPass.imageViewCount =
            depthOnlyRenderPass ? 1u : (desc.hasDepthStencilAttachment ? 2u : 1u);
        impl_->activeRenderPass.subpassCount =
            desc.subpasses.empty() ? 1u : static_cast<std::uint32_t>(desc.subpasses.size());
        impl_->activeRenderPass.currentSubpassIndex = 0;
        if (impl_->stats) {
            ++impl_->stats->nativeRenderPassCreateCount;
            impl_->stats->nativeRenderAttachmentViewCreateCount +=
                impl_->activeRenderPass.imageViewCount;
            ++impl_->stats->nativeFramebufferCreateCount;
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeRenderPassBeginRecordCount;
        }
        return true;
    }
    const auto* colorTarget = colorTargets.empty() ? nullptr : colorTargets.front().get();
    const auto image = colorTarget ? colorTarget->backendVulkanImageHandle() : VK_NULL_HANDLE;
    const auto format = colorTarget ? colorTarget->backendVulkanImageFormat() : VK_FORMAT_UNDEFINED;
    const auto colorExtent =
        colorTarget ? colorTarget->backendVulkanImageExtent() : rhi::Extent3D{};
    if (!depthOnlyRenderPass
        && (!image || format == VK_FORMAT_UNDEFINED || colorExtent.width == 0
            || colorExtent.height == 0)) {
        diagnostics = "Vulkan RHI native render pass requires a native color image";
        return false;
    }
    if (desc.hasDepthStencilAttachment && !depthTarget) {
        diagnostics = "Vulkan RHI native render pass requires a tracked depth target";
        return false;
    }
    const auto depthImage = depthTarget ? depthTarget->backendVulkanImageHandle() : VK_NULL_HANDLE;
    const auto depthFormat =
        depthTarget ? depthTarget->backendVulkanImageFormat() : VK_FORMAT_UNDEFINED;
    const auto depthExtent =
        depthTarget ? depthTarget->backendVulkanImageExtent() : rhi::Extent3D{};
    const auto extent = depthOnlyRenderPass ? depthExtent : colorExtent;
    if (desc.hasDepthStencilAttachment
        && (!depthImage || depthFormat == VK_FORMAT_UNDEFINED
            || (!depthOnlyRenderPass
                && (depthExtent.width != extent.width || depthExtent.height != extent.height)))) {
        diagnostics =
            "Vulkan RHI native render pass depth target is incompatible with color target";
        return false;
    }

    auto* createRenderPass =
        functionFrom<PFN_vkCreateRenderPass>(impl_->device.getDeviceProcAddr("vkCreateRenderPass"));
    auto* createImageView =
        functionFrom<PFN_vkCreateImageView>(impl_->device.getDeviceProcAddr("vkCreateImageView"));
    auto* createFramebuffer = functionFrom<PFN_vkCreateFramebuffer>(
        impl_->device.getDeviceProcAddr("vkCreateFramebuffer"));
    auto* cmdBeginRenderPass = functionFrom<PFN_vkCmdBeginRenderPass>(
        impl_->device.getDeviceProcAddr("vkCmdBeginRenderPass"));
    if (!createRenderPass || !createImageView || !createFramebuffer || !cmdBeginRenderPass) {
        diagnostics = "Vulkan RHI native render pass missing Vulkan entrypoints";
        return false;
    }

    // --- Multi-subpass path ---
    // When desc.subpasses is non-empty, build a VkRenderPass with one
    // VkSubpassDescription per entry, input attachment references, and
    // VkSubpassDependency entries (BY_REGION). This is the native lowering
    // target for FrameGraph SubpassGraph groups. The single-subpass path
    // below handles the legacy empty-subpasses case.
    if (!desc.subpasses.empty()) {
        // Attachment layout: color attachments first (indices 0..C-1), then
        // depth at index C (when present). This matches RenderPassDesc's
        // colorAttachments + depthStencilAttachment ordering and the
        // SubpassDesc index semantics (indices into colorAttachments; depth
        // is addressed via useDepthStencilAttachment / readDepthInputAttachment).
        const auto colorCount = static_cast<std::uint32_t>(desc.colorAttachments.size());
        const bool hasDepth = desc.hasDepthStencilAttachment;
        const std::uint32_t depthAttachmentIndex = colorCount; // depth slot
        const std::uint32_t totalAttachments = hasDepth ? colorCount + 1 : colorCount;
        const auto subpassCount = static_cast<std::uint32_t>(desc.subpasses.size());
        if (totalAttachments > VulkanRhiNativeRenderPassResources::kMaxImageViews) {
            diagnostics = "Vulkan RHI native multi-subpass render pass exceeds max attachments";
            return false;
        }
        if (colorCount == 0 && !hasDepth) {
            diagnostics = "Vulkan RHI native multi-subpass render pass requires attachments";
            return false;
        }
        // The per-subpass reference/description arrays below are sized
        // kMaxImageViews and indexed by subpass index. Subpass count is
        // independent of attachment count (a SubpassGraph can have many
        // subpasses over few attachments), so it must be bounds-checked
        // separately. AGENTS.md: deterministic diagnostic failure instead
        // of a silent out-of-bounds write.
        if (subpassCount > VulkanRhiNativeRenderPassResources::kMaxImageViews) {
            diagnostics = "Vulkan RHI native multi-subpass render pass exceeds max subpasses";
            return false;
        }
        std::array<VkAttachmentDescription, VulkanRhiNativeRenderPassResources::kMaxImageViews>
            attachments{};
        for (std::uint32_t i = 0; i < colorCount; ++i) {
            const auto& target = colorTargets[i];
            const auto targetFormat =
                target ? target->backendVulkanImageFormat() : VK_FORMAT_UNDEFINED;
            const auto targetExtent = target ? target->backendVulkanImageExtent() : rhi::Extent3D{};
            if (!target || !target->backendVulkanImageHandle()
                || targetFormat == VK_FORMAT_UNDEFINED || targetExtent.width != colorExtent.width
                || targetExtent.height != colorExtent.height) {
                diagnostics = "Vulkan RHI native MRT color targets are incompatible";
                return false;
            }
            attachments[i].format = targetFormat;
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = loadOpFor(desc.colorAttachments[i].loadOp);
            attachments[i].storeOp = storeOpFor(desc.colorAttachments[i].storeOp);
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            // Render-pass attachment layouts lower the L2 ColorAttachment
            // state only.  Present is a separate, explicit FrameGraph exit
            // transition (ColorAttachment -> Present), shared by all RHI
            // backends; inferring it from a swapchain-native payload breaks
            // multi-pass composition on Vulkan.
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        if (hasDepth) {
            attachments[depthAttachmentIndex].format = depthFormat;
            attachments[depthAttachmentIndex].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[depthAttachmentIndex].loadOp =
                loadOpFor(desc.depthStencilAttachment.depthLoadOp);
            attachments[depthAttachmentIndex].storeOp =
                storeOpFor(desc.depthStencilAttachment.depthStoreOp);
            attachments[depthAttachmentIndex].stencilLoadOp =
                loadOpFor(desc.depthStencilAttachment.stencilLoadOp);
            attachments[depthAttachmentIndex].stencilStoreOp =
                storeOpFor(desc.depthStencilAttachment.stencilStoreOp);
            attachments[depthAttachmentIndex].initialLayout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments[depthAttachmentIndex].finalLayout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        // Build per-subpass VkSubpassDescription with color, input, and depth
        // references. The reference vectors must outlive the
        // VkSubpassDescription array, so they are stored in persistent vectors.
        std::array<core::pmr::Vector<VkAttachmentReference>,
            VulkanRhiNativeRenderPassResources::kMaxImageViews>
            subpassColorRefs;
        std::array<core::pmr::Vector<VkAttachmentReference>,
            VulkanRhiNativeRenderPassResources::kMaxImageViews>
            subpassInputRefs;
        std::array<VkAttachmentReference, VulkanRhiNativeRenderPassResources::kMaxImageViews>
            subpassDepthRefs;
        std::array<VkSubpassDescription, VulkanRhiNativeRenderPassResources::kMaxImageViews>
            subpassesArr{};

        for (std::uint32_t sp = 0; sp < subpassCount; ++sp) {
            const auto& sd = desc.subpasses[sp];
            auto& colorRefs = subpassColorRefs[sp];
            auto& inputRefs = subpassInputRefs[sp];
            for (const auto ci : sd.colorAttachmentIndices) {
                if (ci >= colorCount) {
                    diagnostics =
                        "Vulkan RHI native multi-subpass color attachment index out of range";
                    return false;
                }
                VkAttachmentReference ref{};
                ref.attachment = ci;
                ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorRefs.push_back(ref);
            }
            for (const auto ii : sd.inputAttachmentIndices) {
                if (ii >= colorCount) {
                    diagnostics =
                        "Vulkan RHI native multi-subpass input attachment index out of range";
                    return false;
                }
                VkAttachmentReference ref{};
                ref.attachment = ii;
                // Input attachment read of a color attachment. Use
                // SHADER_READ_ONLY_OPTIMAL so the tile-local write from an
                // earlier subpass is visible as a read-only input.
                ref.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                inputRefs.push_back(ref);
            }
            subpassesArr[sp].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassesArr[sp].colorAttachmentCount = static_cast<std::uint32_t>(colorRefs.size());
            subpassesArr[sp].pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
            subpassesArr[sp].inputAttachmentCount = static_cast<std::uint32_t>(inputRefs.size());
            subpassesArr[sp].pInputAttachments = inputRefs.empty() ? nullptr : inputRefs.data();
            if (sd.useDepthStencilAttachment && hasDepth) {
                subpassDepthRefs[sp].attachment = depthAttachmentIndex;
                subpassDepthRefs[sp].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                subpassesArr[sp].pDepthStencilAttachment = &subpassDepthRefs[sp];
            }
            // Depth framebuffer fetch (readDepthInputAttachment) requires an
            // input attachment ref with DEPTH_STENCIL_READ_ONLY_OPTIMAL. This
            // is not yet implemented. AGENTS.md: deterministic diagnostic
            // failure instead of silently dropping the read (which would
            // produce a render pass that ignores the depth input, causing
            // silent mis-rendering). Prototype TODO: implement depth input
            // attachment ref + layout transition.
            if (sd.readDepthInputAttachment) {
                diagnostics = "Vulkan RHI native multi-subpass does not yet support depth input "
                              "attachment reads";
                return false;
            }
        }

        // Subpass dependencies from desc.subpassDependencies, with BY_REGION
        // flag and appropriate stage/access masks for tile-local data flow.
        core::pmr::Vector<VkSubpassDependency> deps;
        deps.reserve(desc.subpassDependencies.size());
        for (const auto& d : desc.subpassDependencies) {
            VkSubpassDependency dep{};
            dep.srcSubpass = d.srcSubpass;
            dep.dstSubpass = d.dstSubpass;
            dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            // Conservative masks: src produces (color/depth write + early
            // fragment tests), dst consumes (fragment shader reads input
            // attachment / color/depth read-write).
            dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep.srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
                | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps.push_back(dep);
        }

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = totalAttachments;
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = subpassCount;
        renderPassInfo.pSubpasses = subpassesArr.data();
        renderPassInfo.dependencyCount = static_cast<std::uint32_t>(deps.size());
        renderPassInfo.pDependencies = deps.empty() ? nullptr : deps.data();

        VulkanRhiNativeRenderPassResources resources;
        resources.subpassCount = subpassCount;
        resources.currentSubpassIndex = 0;
        const auto renderPassCreated = createRenderPass(
            impl_->device.logicalDevice(), &renderPassInfo, nullptr, &resources.renderPass);
        if (renderPassCreated != VK_SUCCESS) {
            diagnostics = vkFailure("vkCreateRenderPass", renderPassCreated);
            return false;
        }

        // Create image views for all color attachments + depth.
        for (std::uint32_t i = 0; i < colorCount; ++i) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = colorTargets[i]->backendVulkanImageHandle();
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = colorTargets[i]->backendVulkanImageFormat();
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            VkImageView imageView = VK_NULL_HANDLE;
            const auto viewCreated =
                createImageView(impl_->device.logicalDevice(), &viewInfo, nullptr, &imageView);
            if (viewCreated != VK_SUCCESS) {
                destroyVulkanRhiNativeRenderPassResources(impl_->device, resources);
                diagnostics = vkFailure("vkCreateImageView", viewCreated);
                return false;
            }
            resources.imageViews[resources.imageViewCount] = imageView;
            ++resources.imageViewCount;
        }
        if (hasDepth) {
            VkImageViewCreateInfo depthViewInfo{};
            depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            depthViewInfo.image = depthImage;
            depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depthViewInfo.format = depthFormat;
            depthViewInfo.subresourceRange.aspectMask = depthStencilAspectMaskFor(depthFormat);
            depthViewInfo.subresourceRange.levelCount = 1;
            depthViewInfo.subresourceRange.layerCount = 1;
            VkImageView depthImageView = VK_NULL_HANDLE;
            const auto depthViewCreated = createImageView(
                impl_->device.logicalDevice(), &depthViewInfo, nullptr, &depthImageView);
            if (depthViewCreated != VK_SUCCESS) {
                destroyVulkanRhiNativeRenderPassResources(impl_->device, resources);
                diagnostics = vkFailure("vkCreateImageView", depthViewCreated);
                return false;
            }
            resources.imageViews[resources.imageViewCount] = depthImageView;
            ++resources.imageViewCount;
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = resources.renderPass;
        framebufferInfo.attachmentCount = totalAttachments;
        framebufferInfo.pAttachments = resources.imageViews.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        const auto framebufferCreated = createFramebuffer(
            impl_->device.logicalDevice(), &framebufferInfo, nullptr, &resources.framebuffer);
        if (framebufferCreated != VK_SUCCESS) {
            destroyVulkanRhiNativeRenderPassResources(impl_->device, resources);
            diagnostics = vkFailure("vkCreateFramebuffer", framebufferCreated);
            return false;
        }

        // Clear values: one per attachment, copied from complete L2 intent.
        std::array<VkClearValue, VulkanRhiNativeRenderPassResources::kMaxImageViews> clearValues{};
        for (std::uint32_t i = 0; i < colorCount; ++i) {
            clearValues[i].color = {{
                desc.colorAttachments[i].clearColor.r,
                desc.colorAttachments[i].clearColor.g,
                desc.colorAttachments[i].clearColor.b,
                desc.colorAttachments[i].clearColor.a,
            }};
        }
        if (hasDepth) {
            clearValues[depthAttachmentIndex].depthStencil = {
                desc.depthStencilAttachment.depthClearValue,
                desc.depthStencilAttachment.stencilClearValue};
        }
        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = resources.renderPass;
        beginInfo.framebuffer = resources.framebuffer;
        beginInfo.renderArea.extent = {extent.width, extent.height};
        beginInfo.clearValueCount = totalAttachments;
        beginInfo.pClearValues = clearValues.data();
        cmdBeginRenderPass(impl_->commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

        impl_->activeRenderPass = std::move(resources);
        if (impl_->stats) {
            ++impl_->stats->nativeRenderPassCreateCount;
            impl_->stats->nativeRenderAttachmentViewCreateCount += totalAttachments;
            ++impl_->stats->nativeFramebufferCreateCount;
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeRenderPassBeginRecordCount;
        }
        return true;
    }

    const auto colorCount = static_cast<std::uint32_t>(desc.colorAttachments.size());
    const auto attachmentCount = colorCount + (desc.hasDepthStencilAttachment ? 1u : 0u);
    std::array<VkAttachmentDescription, VulkanRhiNativeRenderPassResources::kMaxImageViews>
        attachments{};
    std::array<VkAttachmentReference, VulkanRhiNativeRenderPassResources::kMaxImageViews>
        colorAttachmentRefs{};
    for (std::uint32_t index = 0; index < colorCount; ++index) {
        const auto& target = colorTargets[index];
        attachments[index].format = target->backendVulkanImageFormat();
        attachments[index].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[index].loadOp = loadOpFor(desc.colorAttachments[index].loadOp);
        attachments[index].storeOp = storeOpFor(desc.colorAttachments[index].storeOp);
        attachments[index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // Keep render-pass lowering at ColorAttachment. The graph's explicit
        // frame-end Present transition performs the native present layout
        // lowering after the final writer, rather than guessing per pass.
        attachments[index].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[index].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentRefs[index] = {index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }

    VkAttachmentReference depthAttachmentRef{};
    if (desc.hasDepthStencilAttachment) {
        const auto depthAttachmentIndex = colorCount;
        const auto& depthAttachment = desc.depthStencilAttachment;
        attachments[depthAttachmentIndex].format = depthFormat;
        attachments[depthAttachmentIndex].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[depthAttachmentIndex].loadOp = loadOpFor(depthAttachment.depthLoadOp);
        attachments[depthAttachmentIndex].storeOp = storeOpFor(depthAttachment.depthStoreOp);
        attachments[depthAttachmentIndex].stencilLoadOp = loadOpFor(depthAttachment.stencilLoadOp);
        attachments[depthAttachmentIndex].stencilStoreOp =
            storeOpFor(depthAttachment.stencilStoreOp);
        attachments[depthAttachmentIndex].initialLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[depthAttachmentIndex].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachmentRef.attachment = depthAttachmentIndex;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorCount;
    subpass.pColorAttachments = depthOnlyRenderPass ? nullptr : colorAttachmentRefs.data();
    if (desc.hasDepthStencilAttachment) {
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
    }

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    VulkanRhiNativeRenderPassResources resources;
    const auto renderPassCreated = createRenderPass(
        impl_->device.logicalDevice(), &renderPassInfo, nullptr, &resources.renderPass);
    if (renderPassCreated != VK_SUCCESS) {
        diagnostics = vkFailure("vkCreateRenderPass", renderPassCreated);
        return false;
    }

    for (std::uint32_t index = 0; index < colorCount; ++index) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = colorTargets[index]->backendVulkanImageHandle();
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorTargets[index]->backendVulkanImageFormat();
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView imageView = VK_NULL_HANDLE;
        const auto viewCreated =
            createImageView(impl_->device.logicalDevice(), &viewInfo, nullptr, &imageView);
        if (viewCreated != VK_SUCCESS) {
            destroyVulkanRhiNativeRenderPassResources(impl_->device, resources);
            diagnostics = vkFailure("vkCreateImageView", viewCreated);
            return false;
        }
        resources.imageViews[resources.imageViewCount++] = imageView;
    }

    if (desc.hasDepthStencilAttachment) {
        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = depthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = depthFormat;
        depthViewInfo.subresourceRange.aspectMask = depthStencilAspectMaskFor(depthFormat);
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.layerCount = 1;
        VkImageView depthImageView = VK_NULL_HANDLE;
        const auto depthViewCreated = createImageView(
            impl_->device.logicalDevice(), &depthViewInfo, nullptr, &depthImageView);
        if (depthViewCreated != VK_SUCCESS) {
            destroyVulkanRhiNativeRenderPassResources(impl_->device, resources);
            diagnostics = vkFailure("vkCreateImageView", depthViewCreated);
            return false;
        }
        resources.imageViews[resources.imageViewCount] = depthImageView;
        ++resources.imageViewCount;
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = resources.renderPass;
    framebufferInfo.attachmentCount = attachmentCount;
    framebufferInfo.pAttachments = resources.imageViews.data();
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;
    const auto framebufferCreated = createFramebuffer(
        impl_->device.logicalDevice(), &framebufferInfo, nullptr, &resources.framebuffer);
    if (framebufferCreated != VK_SUCCESS) {
        destroyVulkanRhiNativeRenderPassResources(impl_->device, resources);
        diagnostics = vkFailure("vkCreateFramebuffer", framebufferCreated);
        return false;
    }

    std::array<VkClearValue, VulkanRhiNativeRenderPassResources::kMaxImageViews> clearValues{};
    for (std::uint32_t index = 0; index < colorCount; ++index) {
        clearValues[index].color = {
            {
                desc.colorAttachments[index].clearColor.r,
                desc.colorAttachments[index].clearColor.g,
                desc.colorAttachments[index].clearColor.b,
                desc.colorAttachments[index].clearColor.a,
            },
        };
    }
    if (desc.hasDepthStencilAttachment)
        clearValues[colorCount].depthStencil = {desc.depthStencilAttachment.depthClearValue,
            desc.depthStencilAttachment.stencilClearValue};
    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = resources.renderPass;
    beginInfo.framebuffer = resources.framebuffer;
    beginInfo.renderArea.extent = {extent.width, extent.height};
    beginInfo.clearValueCount = attachmentCount;
    beginInfo.pClearValues = clearValues.data();
    cmdBeginRenderPass(impl_->commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

    impl_->activeRenderPass = std::move(resources);
    if (impl_->stats) {
        ++impl_->stats->nativeRenderPassCreateCount;
        impl_->stats->nativeRenderAttachmentViewCreateCount += attachmentCount;
        ++impl_->stats->nativeFramebufferCreateCount;
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeRenderPassBeginRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordGraphicsPipelineBind(
    const VulkanRhiNativeResourcePayload& pipeline, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->pipeline(pipeline, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native graphics bind requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeGraphicsPipelineBindRecordCount;
        }
        return true;
    }
    const auto handle = pipeline.backendVulkanRenderPipelineHandle();
    if (pipeline.backendVulkanDeviceHandle() != impl_->device.logicalDevice()) {
        diagnostics = "RHI-VULKAN-NATIVE-PIPELINE-DEVICE-MISMATCH";
        return false;
    }
    if (!handle) {
        diagnostics = "Vulkan RHI native graphics bind requires a native pipeline payload";
        return false;
    }
    auto* cmdBindPipeline =
        functionFrom<PFN_vkCmdBindPipeline>(impl_->device.getDeviceProcAddr("vkCmdBindPipeline"));
    if (!cmdBindPipeline) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdBindPipeline";
        return false;
    }
    cmdBindPipeline(impl_->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, handle);
    impl_->boundGraphicsPipelineLayout = pipeline.backendVulkanRenderPipelineLayoutHandle();
    impl_->boundGraphicsPushConstantStages = pipeline.backendVulkanPushConstantStageFlags();
    if (std::getenv("ARP_FORWARD_PLUS_DESCRIPTOR_TRACE") != nullptr) {
        std::cerr << "[vulkan-graphics-pipeline-trace] pipeline=" << handle
                  << " layout=" << impl_->boundGraphicsPipelineLayout << '\n';
    }
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeGraphicsPipelineBindRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordSetViewport(
    const rhi::ViewportState& viewport, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->viewport(viewport, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native setViewport requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
        }
        return true;
    }
    auto* cmdSetViewport =
        functionFrom<PFN_vkCmdSetViewport>(impl_->device.getDeviceProcAddr("vkCmdSetViewport"));
    if (!cmdSetViewport) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdSetViewport";
        return false;
    }
    VkViewport nativeViewport{};
    nativeViewport.x = viewport.x;
    nativeViewport.y = viewport.y;
    nativeViewport.width = viewport.width;
    nativeViewport.height = viewport.height;
    nativeViewport.minDepth = viewport.minDepth;
    nativeViewport.maxDepth = viewport.maxDepth;
    cmdSetViewport(impl_->commandBuffer, 0u, 1u, &nativeViewport);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordSetScissor(
    const rhi::ScissorState& scissor, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear && !impl_->conformanceClear->scissor(scissor, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native setScissor requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
        }
        return true;
    }
    auto* cmdSetScissor =
        functionFrom<PFN_vkCmdSetScissor>(impl_->device.getDeviceProcAddr("vkCmdSetScissor"));
    if (!cmdSetScissor) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdSetScissor";
        return false;
    }
    VkRect2D nativeScissor{};
    nativeScissor.offset.x = scissor.x;
    nativeScissor.offset.y = scissor.y;
    nativeScissor.extent.width = scissor.width;
    nativeScissor.extent.height = scissor.height;
    cmdSetScissor(impl_->commandBuffer, 0u, 1u, &nativeScissor);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordVertexBufferBind(std::uint32_t slot,
    const VulkanRhiNativeResourcePayload& buffer,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->vertex(slot, buffer, offsetBytes, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native vertex buffer bind requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
        }
        return true;
    }
    const auto handle = buffer.backendVulkanBufferHandle();
    if (!handle) {
        diagnostics = "Vulkan RHI native vertex buffer bind requires a native buffer payload";
        return false;
    }
    auto* cmdBindVertexBuffers = functionFrom<PFN_vkCmdBindVertexBuffers>(
        impl_->device.getDeviceProcAddr("vkCmdBindVertexBuffers"));
    if (!cmdBindVertexBuffers) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdBindVertexBuffers";
        return false;
    }
    const VkDeviceSize offset = offsetBytes;
    cmdBindVertexBuffers(impl_->commandBuffer, slot, 1, &handle, &offset);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordIndexBufferBind(
    const VulkanRhiNativeResourcePayload& buffer,
    rhi::IndexFormat format,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->index(buffer, format, offsetBytes, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native index buffer bind requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        impl_->indexBufferBound = true;
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
        }
        return true;
    }
    const auto handle = buffer.backendVulkanBufferHandle();
    if (!handle) {
        diagnostics = "Vulkan RHI native index buffer bind requires a native buffer payload";
        return false;
    }
    auto* cmdBindIndexBuffer = functionFrom<PFN_vkCmdBindIndexBuffer>(
        impl_->device.getDeviceProcAddr("vkCmdBindIndexBuffer"));
    if (!cmdBindIndexBuffer) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdBindIndexBuffer";
        return false;
    }
    cmdBindIndexBuffer(impl_->commandBuffer, handle, offsetBytes, indexTypeFor(format));
    impl_->indexBufferBound = true;
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordPushConstants(rhi::ShaderStage stages,
    std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const void* data,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->push(stages, offsetBytes, sizeBytes, data, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native push constants require an active render pass";
        return false;
    }
    if (sizeBytes == 0u || !data) {
        diagnostics = "Vulkan RHI native push constants require explicit data";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
        }
        return true;
    }
    if (!impl_->boundGraphicsPipelineLayout) {
        diagnostics = "Vulkan RHI native push constants require a bound graphics pipeline layout";
        return false;
    }
    auto* cmdPushConstants =
        functionFrom<PFN_vkCmdPushConstants>(impl_->device.getDeviceProcAddr("vkCmdPushConstants"));
    if (!cmdPushConstants) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdPushConstants";
        return false;
    }
    // Vulkan requires stageFlags to contain every stage in an overlapping
    // VkPushConstantRange.  The L2 graphics encoder intentionally permits a
    // consuming-stage-only request (for example Fragment); use the bound
    // pipeline's complete range mask for the native command while preserving
    // the stricter encoder validation above.
    const auto nativeStages = impl_->boundGraphicsPushConstantStages != 0
        ? impl_->boundGraphicsPushConstantStages
        : shaderStagesFor(stages);
    cmdPushConstants(impl_->commandBuffer,
        impl_->boundGraphicsPipelineLayout,
        nativeStages,
        offsetBytes,
        sizeBytes,
        data);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordDrawIndexed(std::uint32_t indexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstIndex,
    std::int32_t vertexOffset,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->draw(
            true, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native indexed draw requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeIndexedDrawRecordCount;
        }
        return true;
    }
    if (!impl_->indexBufferBound && !ensureScratchIndexBuffer(*impl_, indexCount, diagnostics)) {
        return false;
    }
    auto* cmdBindIndexBuffer = functionFrom<PFN_vkCmdBindIndexBuffer>(
        impl_->device.getDeviceProcAddr("vkCmdBindIndexBuffer"));
    auto* cmdDrawIndexed =
        functionFrom<PFN_vkCmdDrawIndexed>(impl_->device.getDeviceProcAddr("vkCmdDrawIndexed"));
    if (!cmdBindIndexBuffer || !cmdDrawIndexed) {
        diagnostics = "Vulkan RHI native indexed draw missing Vulkan entrypoints";
        return false;
    }
    if (!impl_->indexBufferBound) {
        cmdBindIndexBuffer(
            impl_->commandBuffer, impl_->scratchIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }
    cmdDrawIndexed(
        impl_->commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeIndexedDrawRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordDrawIndexedIndirect(
    const VulkanRhiNativeResourcePayload& argsBuffer,
    std::uint64_t argsOffset,
    std::uint32_t drawCount,
    std::uint32_t stride,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native indirect draw requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeIndexedDrawRecordCount;
        }
        return true;
    }
    const auto argsHandle = argsBuffer.backendVulkanBufferHandle();
    if (!argsHandle) {
        diagnostics = "Vulkan RHI native indirect draw requires a native args buffer";
        return false;
    }
    if (!impl_->indexBufferBound) {
        diagnostics = "Vulkan RHI native indirect draw requires a bound index buffer";
        return false;
    }
    auto* cmdDrawIndexedIndirect = functionFrom<PFN_vkCmdDrawIndexedIndirect>(
        impl_->device.getDeviceProcAddr("vkCmdDrawIndexedIndirect"));
    if (!cmdDrawIndexedIndirect) {
        diagnostics = "Vulkan RHI native indirect draw missing vkCmdDrawIndexedIndirect entrypoint";
        return false;
    }
    cmdDrawIndexedIndirect(impl_->commandBuffer, argsHandle, argsOffset, drawCount, stride);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeIndexedDrawRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordDrawIndexedIndirectCount(
    const VulkanRhiNativeResourcePayload& argsBuffer,
    std::uint64_t argsOffset,
    const VulkanRhiNativeResourcePayload& countBuffer,
    std::uint64_t countOffset,
    std::uint32_t maxDrawCount,
    std::uint32_t stride,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native indirect draw count requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeIndexedDrawRecordCount;
        }
        return true;
    }
    const auto argsHandle = argsBuffer.backendVulkanBufferHandle();
    const auto countHandle = countBuffer.backendVulkanBufferHandle();
    if (!argsHandle || !countHandle) {
        diagnostics = "Vulkan RHI native indirect draw count requires native args + count buffers";
        return false;
    }
    if (!impl_->indexBufferBound) {
        diagnostics = "Vulkan RHI native indirect draw count requires a bound index buffer";
        return false;
    }
    auto* cmdDrawIndexedIndirectCount = functionFrom<PFN_vkCmdDrawIndexedIndirectCount>(
        impl_->device.getDeviceProcAddr("vkCmdDrawIndexedIndirectCount"));
    if (!cmdDrawIndexedIndirectCount) {
        diagnostics = "Vulkan RHI native indirect draw count missing vkCmdDrawIndexedIndirectCount "
                      "entrypoint (requires Vulkan 1.2+ or VK_KHR_draw_indirect_count)";
        return false;
    }
    cmdDrawIndexedIndirectCount(impl_->commandBuffer,
        argsHandle,
        argsOffset,
        countHandle,
        countOffset,
        maxDrawCount,
        stride);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeIndexedDrawRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordDraw(std::uint32_t vertexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->draw(
            false, vertexCount, instanceCount, firstVertex, 0, firstInstance, diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native draw requires an active render pass";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeDrawRecordCount;
        }
        return true;
    }
    auto* cmdDraw = functionFrom<PFN_vkCmdDraw>(impl_->device.getDeviceProcAddr("vkCmdDraw"));
    if (!cmdDraw) {
        diagnostics = "Vulkan RHI native draw missing vkCmdDraw entrypoint";
        return false;
    }
    cmdDraw(impl_->commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeDrawRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordRenderPassEnd(std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear && !impl_->conformanceClear->end(diagnostics))
        return false;
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native render pass end requires an active render pass";
        return false;
    }
    if (impl_->retainedRenderPassCount >= impl_->retainedRenderPasses.size()) {
        diagnostics = "Vulkan RHI native command buffer exceeded retained render-pass capacity";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        impl_->activeRenderPass = {};
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeRenderPassEndRecordCount;
        }
        return true;
    }
    auto* cmdEndRenderPass =
        functionFrom<PFN_vkCmdEndRenderPass>(impl_->device.getDeviceProcAddr("vkCmdEndRenderPass"));
    if (!cmdEndRenderPass) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdEndRenderPass";
        return false;
    }
    cmdEndRenderPass(impl_->commandBuffer);
    impl_->retainedRenderPasses[impl_->retainedRenderPassCount++] =
        std::move(impl_->activeRenderPass);
    impl_->activeRenderPass = {};
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeRenderPassEndRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordNextSubpass(std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording || !impl_->activeRenderPass.renderPass) {
        diagnostics = "Vulkan RHI native next subpass requires an active render pass";
        return false;
    }
    auto& rp = impl_->activeRenderPass;
    // No-op for single-subpass render passes (contract per header comment).
    // This keeps the legacy single-subpass path backward compatible when the
    // executor unconditionally calls nextSubpass between subpass recordings.
    if (rp.subpassCount <= 1) {
        return true;
    }
    // Bounds check: do not advance past the last subpass.
    if (rp.currentSubpassIndex + 1 >= rp.subpassCount) {
        diagnostics = "Vulkan RHI native nextSubpass exceeds subpass count";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        ++rp.currentSubpassIndex;
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
        }
        return true;
    }
    auto* cmdNextSubpass =
        functionFrom<PFN_vkCmdNextSubpass>(impl_->device.getDeviceProcAddr("vkCmdNextSubpass"));
    if (!cmdNextSubpass) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdNextSubpass";
        return false;
    }
    cmdNextSubpass(impl_->commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
    ++rp.currentSubpassIndex;
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

} // namespace arp::rhi_vulkan
