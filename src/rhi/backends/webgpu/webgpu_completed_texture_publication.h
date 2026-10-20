#pragma once
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/descriptor_completed_texture.h"
#include "rhi/frontend/resource_leases.h"

namespace arp::rhi_webgpu {
// CompletedTextureDescriptor2 authorizes retained metadata only. The original
// control/stamp and wrapper registry remain authoritative at every observation.
inline const char* validateWebGpuCompletedTexturePublication(
    const WebGpuCompletedTexturePublication& publication,
    const WebGpuNativeDescriptorBindingPayload& binding) noexcept
{
    const bool depth = bool(publication.depthContent);
    if (!publication.source || publication.cellIdentity || depth == bool(publication.content)
        || (publication.colorPublicationVersion != 2u
            && (depth || publication.colorPublicationVersion != 6u))
        || !rhi::descriptorBindingWriteMatches(publication.write, binding.write)
        || binding.write.type != rhi::DescriptorType::SampledTexture
        || binding.write.resourceKind != rhi::DescriptorResourceKind::TextureView
        || binding.write.resource != publication.source->resource()
        || binding.write.resourceState != rhi::ResourceState::ShaderRead
        || binding.write.physicalLowering.enabled || binding.buffer || binding.sampler
        || binding.binding.type != rhi::DescriptorType::SampledTexture
        || binding.binding.binding != binding.write.binding
        || binding.arrayElement != binding.write.arrayElement
        || binding.arrayElement >= binding.binding.arrayCount || binding.binding.dynamicOffset
        || binding.binding.depthTexture != depth
        || rhi::DescriptorBufferBindingRangeContract::validateSchema(
            binding.write.bufferBindingRange, false))
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PROVENANCE";
    if (depth) {
        if (const auto* id = rhi::OriginalCompletedTextureDescriptorContract::validateSource(
                publication.depthContent, 3u))
            return id;
        if (const auto* id =
                rhi::OriginalCommandTextureViewSource::validateOriginal(publication.source))
            return id;
        if (publication.source.get() != publication.depthContent->originalViewSource().get()
            || !core::SharedOwnerProjection::sameOwner(
                publication.source, publication.depthContent->originalViewSource()))
            return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PROVENANCE";
    } else if (const auto* id = rhi::OriginalCompletedTextureDescriptorContract::validateSource(
                   publication.source, *publication.content, publication.colorPublicationVersion))
        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& view = *publication.source->originalObject();
    const auto& texture = *publication.source->originalParentLease()->originalObject();
    if (binding.binding.textureViewDimensionConstrained
        && binding.binding.textureViewDimension != view.desc().dimension)
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-SHAPE";
    const auto parent = webGpuNativePayloadFor(texture);
    WebGpuOrdinaryResourceObservation registered;
    if (const auto* id = observeOriginalWebGpuResource(view, registered))
        return id;
    if (!webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost() || !parent
        || !core::SharedOwnerProjection::sameOwner(binding.textureView, registered.view())
        || !core::SharedOwnerProjection::sameOwner(parent, registered.backing().texture)
        || !core::SharedOwnerProjection::sameOwner(
            binding.textureView, webGpuNativePayloadFor(view))
        || !core::SharedOwnerProjection::sameOwner(binding.textureView->parentTexture, parent)
        || !binding.textureView->matchesDescription(view.desc())
        || (depth ? !parent->contentAccessPolicy().originalAttachmentExclusive()
                  : !parent->contentAccessPolicy().originalUploadExclusive()))
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-NATIVE-IDENTITY";
    return depth ? WebGpuNativeTextureFactory::validateOriginalAttachment(*parent, texture.desc())
                 : WebGpuNativeTextureFactory::validateOriginalUpload(*parent, texture.desc());
#else
    return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-NATIVE-UNAVAILABLE";
#endif
}

// Descriptor metadata only. Legacy shader/root preparation deliberately keeps
// using the completed validator above and refuses cell publications.
inline const char* validateWebGpuOriginalTexturePublication(
    const WebGpuCompletedTexturePublication& publication,
    const WebGpuNativeDescriptorBindingPayload& binding) noexcept
{
    if (!publication.cellIdentity)
        return validateWebGpuCompletedTexturePublication(publication, binding);
    if (publication.content || publication.depthContent || publication.colorPublicationVersion != 2u
        || !rhi::descriptorBindingWriteMatches(publication.write, binding.write)
        || binding.arrayElement != binding.write.arrayElement || binding.buffer || binding.sampler
        || !publication.source || binding.write.resource != publication.source->resource())
        return "RHI-TEXTURE-CELL-DESCRIPTOR-PROVENANCE";
    if (const auto* id =
            rhi::OriginalTextureCellDescriptorContract::validateIntent(publication.cellRequest))
        return id;
    if (const auto* id = rhi::OriginalTextureCellDescriptorContract::validateBinding(
            binding.binding, binding.write, publication.cellRequest.contractVersion))
        return id;
    if (const auto* id = rhi::OriginalTextureCellDescriptorContract::validateSource(
            publication.source, *publication.cellIdentity, publication.cellRequest.contractVersion))
        return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    const auto& view = *publication.source->originalObject();
    const auto& texture = *publication.source->originalParentLease()->originalObject();
    if (view.desc().usage
        != (binding.write.type == rhi::DescriptorType::StorageTexture ? rhi::TextureUsage::Storage
                                                                      : rhi::TextureUsage::Sampled))
        return "RHI-TEXTURE-CELL-DESCRIPTOR-WRITE";
    const auto parent = webGpuNativePayloadFor(texture);
    WebGpuOrdinaryResourceObservation registered;
    if (const auto* id = observeOriginalWebGpuResource(view, registered))
        return id;
    if (!webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost() || !parent
        || !binding.textureView
        || !core::SharedOwnerProjection::sameOwner(binding.textureView, registered.view())
        || !core::SharedOwnerProjection::sameOwner(parent, registered.backing().texture)
        || !core::SharedOwnerProjection::sameOwner(
            binding.textureView, webGpuNativePayloadFor(view))
        || !core::SharedOwnerProjection::sameOwner(binding.textureView->parentTexture, parent)
        || !binding.textureView->matchesDescription(view.desc())
        || !parent->contentAccessPolicy().originalAttachmentExclusive())
        return "RHI-TEXTURE-CELL-DESCRIPTOR-NATIVE-IDENTITY";
    return WebGpuNativeTextureFactory::validateOriginalAttachment(*parent, texture.desc());
#else
    return "RHI-TEXTURE-CELL-DESCRIPTOR-NATIVE-UNAVAILABLE";
#endif
}
} // namespace arp::rhi_webgpu
