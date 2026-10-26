#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_retained_command.h"
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/transfer_validation.h"

#include <cmath>

namespace arp::rhi_vulkan {

bool VulkanRhiRetainedCommand::prepareClearStorePass(const rhi::RenderPassDesc& desc,
    const rhi::GpuTexture& target,
    const rhi::TextureSubresourceRange& range,
    core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    auto& impl = *command_->impl_;
    auto& storage = *impl.recordingRetention;
    if (preparedPassTarget_.isValid() || impl.retainedRenderPassCount
        || impl.activeRenderPass.renderPass || storage.resourceOwnerCount())
        return reject("RHI-VULKAN-RETENTION-CLEAR-PREPARATION-STATE");
    const auto owner = resolve(target, diagnostic);
    if (!owner)
        return false;
    const auto& td = target.desc();
    const bool depth = td.format == rhi::TextureFormat::D32Float;
    if (!rhi::semantics::TextureTransferValidation::textureRangeValid(td, range)
        || range.mipLevelCount != 1u || range.arrayLayerCount != 1u || desc.layeredRendering
        || !desc.subpasses.empty() || !desc.subpassDependencies.empty()
        || (td.usage & rhi::TextureUsage::RenderAttachment) == rhi::TextureUsage::None
        || desc.hasDepthStencilAttachment != depth
        || desc.colorAttachments.size() != (depth ? 0u : 1u))
        return reject("RHI-VULKAN-RETENTION-CLEAR-DESCRIPTOR");
    const auto operations = rhi::semantics::validateRenderPassAttachmentOperations(desc);
    if (!operations.success) {
        diagnostic = operations.diagnostics;
        return false;
    }
    if (depth) {
        const auto& a = desc.depthStencilAttachment;
        if (a.target != target.id() || a.format != td.format || a.depthLoadOp != rhi::LoadOp::Clear
            || a.depthStoreOp != rhi::StoreOp::Store)
            return reject("RHI-VULKAN-RETENTION-CLEAR-DESCRIPTOR");
    } else {
        const auto& a = desc.colorAttachments.front();
        const auto& c = a.clearColor;
        if (a.target != target.id() || a.format != td.format || a.loadOp != rhi::LoadOp::Clear
            || a.storeOp != rhi::StoreOp::Store || a.hasResolveTarget || a.resolveTarget.isValid()
            || !std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b)
            || !std::isfinite(c.a))
            return reject("RHI-VULKAN-RETENTION-CLEAR-DESCRIPTOR");
    }
    const auto features = depth ? depthFormatFeatures_ : colorFormatFeatures_;
    const VkFormatFeatureFlags required = depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    const rhi::Extent3D extent{
        rhi::semantics::TextureTransferValidation::mipExtent(td.extent.width, range.baseMipLevel),
        rhi::semantics::TextureTransferValidation::mipExtent(td.extent.height, range.baseMipLevel),
        1u};
    const auto& limits = impl.device.nativeLimits();
    if (!(features & required) || extent.width > limits.maxFramebufferWidth
        || extent.height > limits.maxFramebufferHeight || !limits.maxFramebufferLayers
        || (!depth && !limits.maxColorAttachments))
        return reject("RHI-VULKAN-RETENTION-CLEAR-LIMIT");
    const std::shared_ptr<const void> owners[]{owner};
    if (const auto* id = storage.preflightResources(owners, 1u))
        return reject(id);
    // Both creation and cleanup must be available before partial creation.
    for (const auto* name : {"vkCreateRenderPass",
             "vkCreateImageView",
             "vkCreateFramebuffer",
             "vkDestroyRenderPass",
             "vkDestroyImageView",
             "vkDestroyFramebuffer"})
        if (!impl.device.getDeviceProcAddr(name))
            return reject("RHI-VULKAN-RETENTION-CLEAR-PREPARATION-ENTRYPOINT");
    const VulkanRhiNativeClearStorePassFactory::Attachment input{owner->backendVulkanImageHandle(),
        owner->backendVulkanImageFormat(),
        {depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            range.baseMipLevel,
            1u,
            range.baseArrayLayer,
            1u}};
    VulkanRhiNativeRenderPassResources native;
    const bool created = VulkanRhiNativeClearStorePassFactory::create(
        impl.device, &input, depth ? 0u : 1u, depth, extent, native);
    if (impl.stats) {
        impl.stats->nativeRenderPassCreateCount += native.renderPass ? 1u : 0u;
        impl.stats->nativeRenderAttachmentViewCreateCount += native.imageViewCount;
        impl.stats->nativeFramebufferCreateCount += native.framebuffer ? 1u : 0u;
    }
    if (!created) {
        destroyVulkanRhiNativeRenderPassResources(impl.device, native);
        return reject("RHI-VULKAN-RETENTION-CLEAR-NATIVE-CREATION");
    }
    if (const auto* id = storage.retainResources(owners, 1u)) {
        destroyVulkanRhiNativeRenderPassResources(impl.device, native);
        return reject(id);
    }
    impl.retainedRenderPasses[0] = native;
    impl.retainedRenderPassCount = 1u;
    preparedPassTarget_ = target.id();
    preparedPassRange_ = range;
    preparedPassExtent_ = extent;
    if (depth)
        preparedPassDepth_ = desc.depthStencilAttachment.depthClearValue;
    else
        preparedPassColor_ = desc.colorAttachments.front().clearColor;
    diagnostic.clear();
    return true;
}

bool VulkanRhiRetainedCommand::validatePreparedPass(
    const rhi::GpuTexture& target, core::pmr::String& diagnostic) const
{
    if (!validateRecording(diagnostic))
        return false;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    auto& impl = *command_->impl_;
    if (preparedPassTarget_ != target.id() || !preparedPassTarget_.isValid()
        || preparedPassRecorded_ || impl.retainedRenderPassCount != 1u
        || impl.activeRenderPass.renderPass)
        return reject("RHI-VULKAN-RETENTION-CLEAR-PREPARED-PASS");
    if (!resolve(target, diagnostic))
        return false;
    const bool depth = target.desc().format == rhi::TextureFormat::D32Float;
    if (!vulkanRhiTextureStateAvailable(
            target, preparedPassRange_, impl.retainedResourceStates.get())
        || target.subresourceState(preparedPassRange_)
            != (depth ? rhi::ResourceState::DepthAttachment : rhi::ResourceState::ColorAttachment))
        return reject("RHI-VULKAN-RETENTION-CLEAR-TARGET-STATE");
    auto beginPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(
        impl.device.getDeviceProcAddr("vkCmdBeginRenderPass"));
    auto endPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(
        impl.device.getDeviceProcAddr("vkCmdEndRenderPass"));
    if (!beginPass || !endPass)
        return reject("RHI-VULKAN-RETENTION-CLEAR-RECORD-ENTRYPOINT");
    return true;
}

bool VulkanRhiRetainedCommand::beginPreparedPass(
    const rhi::GpuTexture& target, core::pmr::String& diagnostic)
{
    if (!validatePreparedPass(target, diagnostic))
        return false;
    auto& impl = *command_->impl_;
    const auto beginPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(
        impl.device.getDeviceProcAddr("vkCmdBeginRenderPass"));
    const bool depth = target.desc().format == rhi::TextureFormat::D32Float;
    const auto& native = impl.retainedRenderPasses[0];
    VkClearValue clear{};
    if (depth)
        clear.depthStencil = {preparedPassDepth_, 0u};
    else {
        const auto& c = preparedPassColor_;
        clear.color = {{c.r, c.g, c.b, c.a}};
    }
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = native.renderPass;
    info.framebuffer = native.framebuffer;
    info.renderArea.extent = {preparedPassExtent_.width, preparedPassExtent_.height};
    info.clearValueCount = 1u;
    info.pClearValues = &clear;
    beginPass(impl.commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    impl.activeRenderPass = native;
    impl.originalRenderPassBorrowed = true;
    if (impl.stats) {
        ++impl.stats->nativeCommandBufferRecordCount;
        ++impl.stats->nativeRenderPassBeginRecordCount;
    }
    return true;
}

void VulkanRhiRetainedCommand::endPreparedPass() noexcept
{
    auto& impl = *command_->impl_;
    const auto endPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(
        impl.device.getDeviceProcAddr("vkCmdEndRenderPass"));
    endPass(impl.commandBuffer);
    impl.activeRenderPass = {};
    impl.originalRenderPassBorrowed = false;
    preparedPassRecorded_ = true;
    if (impl.stats) {
        ++impl.stats->nativeCommandBufferRecordCount;
        ++impl.stats->nativeRenderPassEndRecordCount;
    }
}

bool VulkanRhiRetainedCommand::clearPreparedPass(
    const rhi::GpuTexture& target, core::pmr::String& diagnostic)
{
    if (!validatePreparedPass(target, diagnostic))
        return false;
    rhi::CommandRecordingUsage charge;
    charge.append(rhi::CommandUsageKind::BeginRenderPass);
    charge.append(rhi::CommandUsageKind::EndRenderPass);
    charge.resourceReferences = 1u;
    if (!preflightRecordingCharge(charge, diagnostic) || !commitRecordingCharge(charge, diagnostic))
        return false;
    if (!beginPreparedPass(target, diagnostic)) {
        failed_ = true;
        return false;
    }
    endPreparedPass();
    diagnostic.clear();
    return true;
}

} // namespace arp::rhi_vulkan
