#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_retained_command.h"
#include "rhi/backends/vulkan/vulkan_texture_descriptor.h"
#include "rhi/frontend/resource_access_usage.h"
#include "rhi/frontend/resource_state_sequence.h"
#include "rhi/frontend/transfer_validation.h"

namespace arp::rhi_vulkan {

bool VulkanRhiRetainedCommand::transition(
    rhi::GpuBuffer& buffer, const rhi::ResourceBarrierDesc& desc, core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto owner = resolve(buffer, diagnostic);
    if (!owner)
        return false;
    const auto supported = [](rhi::ResourceState state) noexcept {
        return state == rhi::ResourceState::CopySrc || state == rhi::ResourceState::CopyDst
            || state == rhi::ResourceState::ShaderRead || state == rhi::ResourceState::ShaderWrite;
    };
    if (desc.resource != buffer.id() || buffer.state() != desc.before || !supported(desc.before)
        || !supported(desc.after))
        return reject("RHI-VULKAN-RETENTION-STATE-DESCRIPTOR");
    if (const auto* id = rhi::semantics::validateResourceAccessScope(
            {desc.before, desc.sourceStages, desc.sourceAccess}))
        return reject(id);
    if (const auto* id = rhi::semantics::validateResourceAccessScope(
            {desc.after, desc.destinationStages, desc.destinationAccess}))
        return reject(id);
    // Private shader buffer transitions cover Compute and read-only Fragment use.
    const auto allowed = rhi::PipelineStage::Transfer | rhi::PipelineStage::ComputeShader
        | rhi::PipelineStage::FragmentShader;
    if ((desc.sourceStages & allowed) != desc.sourceStages
        || (desc.destinationStages & allowed) != desc.destinationStages)
        return reject("RHI-VULKAN-RETENTION-STATE-STAGE-DOMAIN");
    const auto fragmentReadOnly = [](rhi::PipelineStage stages,
                                      rhi::ResourceState state,
                                      rhi::ResourceAccess access) noexcept {
        return (stages & rhi::PipelineStage::FragmentShader) == rhi::PipelineStage::None
            || (state == rhi::ResourceState::ShaderRead
                && access == rhi::ResourceAccess::ShaderRead);
    };
    if (!fragmentReadOnly(desc.sourceStages, desc.before, desc.sourceAccess)
        || !fragmentReadOnly(desc.destinationStages, desc.after, desc.destinationAccess))
        return reject("RHI-VULKAN-RETENTION-STATE-FRAGMENT-READ-ONLY");
    if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validate(
            buffer.desc(), desc.sourceAccess | desc.destinationAccess))
        return reject(id);
    const auto accesses = desc.sourceAccess | desc.destinationAccess;
    if (((accesses & rhi::ResourceAccess::ShaderWrite) != rhi::ResourceAccess::None
            && (buffer.gpuWritesSealed() || registry_->bufferGpuWritesSealed(buffer.id())))
        || ((accesses & rhi::ResourceAccess::TransferWrite) != rhi::ResourceAccess::None
            && (buffer.gpuTransferWritesSealed()
                || registry_->bufferGpuTransferWritesSealed(buffer.id()))))
        return reject("RHI-BUFFER-GPU-WRITES-SEALED");
    auto& journal = *command_->impl_->retainedResourceStates;
    if (!vulkanRhiBufferCanTransition(buffer, journal))
        return reject("RHI-VULKAN-RETENTION-STATE-CONFLICT-OR-EXHAUSTED");
    auto& storage = *command_->impl_->recordingRetention;
    const std::shared_ptr<const void> owners[]{owner};
    if (const auto* id = storage.preflightResources(owners, 1u))
        return reject(id);
    if (!command_->impl_->device.getDeviceProcAddr("vkCmdPipelineBarrier"))
        return reject("RHI-VULKAN-RETENTION-BARRIER-ENTRYPOINT");
    const auto transitioned = transitionVulkanRhiBuffer(buffer, desc, &journal);
    if (!transitioned.success) {
        failed_ = true;
        diagnostic = transitioned.diagnostics;
        return false;
    }
    (void)storage.retainResources(owners, 1u);
    if (!command_->recordBufferBarrier(*owner, desc, diagnostic)
        || !command_->flushPendingBarriers(diagnostic)) {
        failed_ = true;
        return false;
    }
    diagnostic.clear();
    return true;
}

std::shared_ptr<VulkanRhiNativeResourcePayload> VulkanRhiRetainedCommand::resolve(
    const rhi::GpuTexture& texture, core::pmr::String& diagnostic) const
{
    const auto native = vulkanRhiNativePayload(texture);
    const auto registered = registry_->payload(texture.id());
    if (!vulkanRhiTextureRegistryMatches(texture, registry_)
        || !registry_->textureObjectMatches(texture) || !native || native != registered
        || !core::SharedOwnerProjection::sameOwner(native, registered)
        || native->backendVulkanDeviceHandle() != command_->impl_->device.logicalDevice()
        || !native->backendVulkanImageHandle() || native->backendVulkanSwapchainFrameSyncAvailable()
        || native->backendVulkanOriginalContentExclusive() || texture.originalContentExclusive()) {
        diagnostic = "RHI-VULKAN-RETENTION-TEXTURE-REGISTRATION";
        return {};
    }
    const auto& desc = texture.desc();
    if ((desc.format != rhi::TextureFormat::D32Float
            && desc.format != rhi::TextureFormat::Rgba16Float)
        || !desc.extent.width || !desc.extent.height || desc.extent.depth != 1u
        || desc.sampleCount != 1u || desc.cubeCompatible
        || native->backendVulkanImageFormat() != VulkanTextureDescriptor::format(desc.format)
        || native->backendVulkanImageExtent() != desc.extent) {
        diagnostic = "RHI-VULKAN-RETENTION-TEXTURE-SHAPE";
        return {};
    }
    return native;
}

bool VulkanRhiRetainedCommand::transition(
    rhi::GpuTexture& texture, const rhi::ResourceBarrierDesc& desc, core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto owner = resolve(texture, diagnostic);
    if (!owner)
        return false;
    if (desc.resource != texture.id())
        return reject("RHI-VULKAN-RETENTION-TEXTURE-DESCRIPTOR");
    if (!vulkanRhiTextureStateAvailable(
            texture, desc.textureSubresources, command_->impl_->retainedResourceStates.get()))
        return reject("RHI-VULKAN-RETENTION-TEXTURE-STATE-UNAVAILABLE");
    if (const auto* id = rhi::semantics::RecordedResourceStateSequence::validateBarrier(
            desc, texture.subresourceState(desc.textureSubresources)))
        return reject(id);
    const auto flags = texture.desc().format == rhi::TextureFormat::D32Float ? depthFormatFeatures_
                                                                             : colorFormatFeatures_;
    const auto required = [&](rhi::ResourceState state) -> VkFormatFeatureFlags {
        switch (state) {
        case rhi::ResourceState::Undefined:
            return 0u;
        case rhi::ResourceState::CopySrc:
            return VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        case rhi::ResourceState::CopyDst:
            return VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        case rhi::ResourceState::ShaderRead:
            return VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        case rhi::ResourceState::ColorAttachment:
            return texture.desc().format == rhi::TextureFormat::Rgba16Float
                ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                : VkFormatFeatureFlags{};
        case rhi::ResourceState::DepthAttachment:
            return texture.desc().format == rhi::TextureFormat::D32Float
                ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                : VkFormatFeatureFlags{};
        default:
            return 0u;
        }
    };
    const auto before = required(desc.before), after = required(desc.after);
    if ((!before && desc.before != rhi::ResourceState::Undefined) || !after
        || (flags & (before | after)) != (before | after)
        || (desc.before == rhi::ResourceState::Undefined
            && !rhi::semantics::resourceAccessWrites(desc.destinationAccess)))
        return reject("RHI-VULKAN-RETENTION-TEXTURE-STATE-OR-FORMAT");
    const auto allowed = rhi::PipelineStage::TopOfPipe | rhi::PipelineStage::Transfer
        | rhi::PipelineStage::ComputeShader | rhi::PipelineStage::FragmentShader
        | rhi::PipelineStage::ColorAttachmentOutput | rhi::PipelineStage::DepthStencil;
    if ((desc.sourceStages & allowed) != desc.sourceStages
        || (desc.destinationStages & allowed) != desc.destinationStages)
        return reject("RHI-VULKAN-RETENTION-TEXTURE-STAGE-DOMAIN");
    if (const auto* id = rhi::semantics::ResourceAccessUsageValidation::validate(
            texture.desc(), desc.sourceAccess | desc.destinationAccess))
        return reject(id);
    auto& journal = *command_->impl_->retainedResourceStates;
    if (!vulkanRhiTextureCanTransition(texture, journal))
        return reject("RHI-VULKAN-RETENTION-TEXTURE-STATE-CONFLICT-OR-EXHAUSTED");
    const std::shared_ptr<const void> owners[]{owner};
    auto& storage = *command_->impl_->recordingRetention;
    if (const auto* id = storage.preflightResources(owners, 1u))
        return reject(id);
    if (!command_->impl_->device.getDeviceProcAddr("vkCmdPipelineBarrier"))
        return reject("RHI-VULKAN-RETENTION-BARRIER-ENTRYPOINT");
    const auto changed = transitionVulkanRhiTexture(texture, desc, &journal);
    if (!changed.success) {
        failed_ = true;
        diagnostic = changed.diagnostics;
        return false;
    }
    (void)storage.retainResources(owners, 1u);
    if (!command_->recordImageBarrier(*owner, desc, diagnostic)
        || !command_->flushPendingBarriers(diagnostic)) {
        failed_ = true;
        return false;
    }
    diagnostic.clear();
    return true;
}

bool VulkanRhiRetainedCommand::copy(const rhi::GpuTexture& source,
    const rhi::GpuBuffer& destination,
    const rhi::TextureToBufferCopyDesc& desc,
    core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto src = resolve(source, diagnostic);
    if (!src)
        return false;
    const auto dst = resolve(destination, diagnostic);
    if (!dst)
        return false;
    if (desc.sourceTexture != source.id() || desc.destinationBuffer != destination.id()
        || !desc.mipLayouts.empty()
        || desc.bufferOffsetBytes % rhi::bytesPerPixel(source.desc().format)
        || !rhi::semantics::TextureTransferValidation::copyLayoutValid(source.desc(),
            desc.textureSubresources,
            desc.extent,
            desc.bytesPerRow,
            desc.rowsPerImage,
            desc.bufferOffsetBytes,
            destination.desc().sizeBytes)
        || (source.desc().usage & rhi::TextureUsage::CopySrc) == rhi::TextureUsage::None
        || (destination.desc().usage & rhi::ResourceUsage::CopyDst) == rhi::ResourceUsage::None
        || destination.state() != rhi::ResourceState::CopyDst
        || source.subresourceState(desc.textureSubresources) != rhi::ResourceState::CopySrc)
        return reject("RHI-VULKAN-RETENTION-TEXTURE-COPY-DESCRIPTOR");
    if (!vulkanRhiTextureStateAvailable(
            source, desc.textureSubresources, command_->impl_->retainedResourceStates.get()))
        return reject("RHI-VULKAN-RETENTION-TEXTURE-STATE-UNAVAILABLE");
    const auto flags = source.desc().format == rhi::TextureFormat::D32Float ? depthFormatFeatures_
                                                                            : colorFormatFeatures_;
    if (!(flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        return reject("RHI-VULKAN-RETENTION-TEXTURE-COPY-FORMAT");
    if (destination.gpuTransferWritesSealed()
        || registry_->bufferGpuTransferWritesSealed(destination.id()))
        return reject("RHI-BUFFER-GPU-WRITES-SEALED");
    const std::shared_ptr<const void> owners[]{src, dst};
    auto& storage = *command_->impl_->recordingRetention;
    if (const auto* id = storage.preflightResources(owners, 2u))
        return reject(id);
    if (!command_->impl_->device.getDeviceProcAddr("vkCmdCopyImageToBuffer"))
        return reject("RHI-VULKAN-RETENTION-TEXTURE-COPY-ENTRYPOINT");
    (void)storage.retainResources(owners, 2u);
    if (!command_->recordImageToBufferCopy(*src, *dst, desc, diagnostic)) {
        failed_ = true;
        return false;
    }
    diagnostic.clear();
    return true;
}

} // namespace arp::rhi_vulkan
