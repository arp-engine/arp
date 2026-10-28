#include "renderer/pipeline/scene_pbr_texture_publication.h"

#include "rhi/frontend/ordinary_sampled_color.h"

#include <cstring>

namespace arp::renderer {

const char* ScenePbrTexturePublication::compare(
    const resource::TextureResource& texture) const noexcept
{
    using namespace rhi;
    const auto& input = *upload_->originalPacking()->originalSource()->originalInput();
    UploadTextureDesc fields;
    input.scalarFields().writeScalarFields(fields);
    const auto& target = view_->originalParentLease()->originalObject();
    const auto& desc = target->desc();
    const auto expectedFormat = srgb_ ? TextureFormat::Rgba8UnormSrgb : TextureFormat::Rgba8Unorm;
    const auto pixels = std::uint64_t(texture.decodedWidth) * texture.decodedHeight;
    if (texture.reference().id != id_
        || texture.reference().type != resource::TextureResource::resourceTypeName()
        || !texture.decodedWidth || !texture.decodedHeight || pixels > budget_.maxPixelBytes / 4u
        || texture.decodedRgba8.size() != pixels * 4u
        || texture.sourceGeneration != sourceGeneration_ || !sourceGeneration_
        || texture.sourceResourceId != sourceId_ || texture.sourcePath != sourcePath_
        || texture.usage != usage_ || texture.srgb != srgb_ || texture.decodedSrgb != srgb_
        || texture.alpha != alpha_ || texture.decodedHasAlpha != alpha_
        || fields.destinationTexture != target->id() || fields.extent != desc.extent
        || fields.extent.width != texture.decodedWidth
        || fields.extent.height != texture.decodedHeight || fields.extent.depth != 1u
        || fields.destinationOffset != Extent3D{0u, 0u, 0u} || fields.format != expectedFormat
        || desc.format != expectedFormat || fields.mipLevel || fields.arrayLayer
        || fields.subresources != defaultTextureSubresourceRange(desc)
        || fields.bytesPerRow != std::uint64_t(texture.decodedWidth) * 4u
        || fields.rowsPerImage != texture.decodedHeight || input.mipLayouts().size() != 1u
        || input.pixelBytes().size() != texture.decodedRgba8.size())
        return "SCENE-PBR-DECODED-UPLOAD-METADATA";
    const auto& mip = input.mipLayouts()[0];
    if (mip.mipLevel || mip.extent != fields.extent || mip.bytesPerRow != fields.bytesPerRow
        || mip.rowsPerImage != fields.rowsPerImage || mip.bufferOffsetBytes
        || std::memcmp(
            input.pixelBytes().data(), texture.decodedRgba8.data(), input.pixelBytes().size()))
        return "SCENE-PBR-DECODED-UPLOAD-BYTES";
    return nullptr;
}

const char* ScenePbrTexturePublication::create(const resource::ResourceManager& resources,
    std::string_view resourceId,
    const std::shared_ptr<const rhi::OriginalTextureUploadPublication>& upload,
    const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& view,
    const Budget& budget,
    std::shared_ptr<const ScenePbrTexturePublication>& output)
try {
    if (budget.contractVersion != 1u || !budget.prototypeOptIn)
        return "SCENE-PBR-TEXTURE-PUBLICATION-OPT-IN";
    if (!budget.maxReferenceBytes || !budget.maxPixelBytes || !budget.maxCpuBytes || !budget.quota
        || budget.quota->limitBytes() != budget.maxCpuBytes)
        return "SCENE-PBR-TEXTURE-PUBLICATION-QUOTA";
    if (!resource::isResourceUuid(resourceId) || resourceId.size() > budget.maxReferenceBytes)
        return "SCENE-PBR-TEXTURE-IDENTITY";
    if (const auto* id = rhi::OriginalTextureUploadPublication::validateOriginal(upload))
        return id;
    if (const auto* id = upload->validateView(view))
        return id;
    if (const auto* id = upload->validateReadVisibility(rhi::PipelineStage::FragmentShader))
        return id;
    rhi::OriginalOrdinarySampledColorRead::Snapshot completed;
    if (const auto* id = rhi::OriginalOrdinarySampledColorRead::capture(*view, completed))
        return id;
    core::CpuMemoryQuotaScope scope(budget.quota);
    if (resources.stateOf(resourceId) != resource::ResourceState::Ready)
        return "SCENE-PBR-TEXTURE-NOT-READY";
    const auto handle = resources.find<resource::TextureResource>(resourceId);
    if (!handle.ready() || !handle.generation())
        return "SCENE-PBR-TEXTURE-TYPE";
    const auto& texture = *handle.get();
    if (texture.reference().id != resourceId
        || texture.reference().type != resource::TextureResource::resourceTypeName()
        || !resource::isResourceUuid(texture.sourceResourceId) || texture.sourcePath.empty()
        || texture.sourcePath.size() > budget.maxReferenceBytes
        || texture.sourceResourceId.size() > budget.maxReferenceBytes || texture.usage.empty()
        || texture.usage.size() > budget.maxReferenceBytes)
        return "SCENE-PBR-DECODED-SOURCE-METADATA";
    auto next = core::makeShared<ScenePbrTexturePublication>(Key{});
    next->budget_ = budget;
    next->owner_ = std::this_thread::get_id();
    next->id_ = resourceId;
    next->sourceId_ = texture.sourceResourceId;
    next->sourcePath_ = texture.sourcePath;
    next->usage_ = texture.usage;
    next->identity_ = handle.get();
    next->generation_ = handle.generation();
    next->sourceGeneration_ = texture.sourceGeneration;
    next->srgb_ = texture.srgb;
    next->alpha_ = texture.alpha;
    next->upload_ = upload;
    next->view_ = view;
    if (const auto* id = next->compare(texture))
        return id;
    output = std::move(next);
    return nullptr;
} catch (const std::bad_alloc&) {
    return "SCENE-PBR-TEXTURE-PUBLICATION-BUDGET";
}

const char* ScenePbrTexturePublication::validate() const noexcept
{
    if (owner_ != std::this_thread::get_id())
        return "SCENE-PBR-TEXTURE-PUBLICATION-THREAD";
    if (!budget_.quota || budget_.quota->limitBytes() != budget_.maxCpuBytes || !identity_
        || !generation_)
        return "SCENE-PBR-TEXTURE-PUBLICATION-OWNER";
    if (const auto* id = rhi::OriginalTextureUploadPublication::validateOriginal(upload_))
        return id;
    return upload_->validateView(view_);
}

const char* ScenePbrTexturePublication::validateCurrent(
    const resource::ResourceManager& resources) const
try {
    if (const auto* id = validate())
        return id;
    core::CpuMemoryQuotaScope scope(budget_.quota);
    if (resources.stateOf(id_) != resource::ResourceState::Ready)
        return "SCENE-PBR-TEXTURE-NOT-READY";
    const auto handle = resources.find<resource::TextureResource>(id_);
    if (!handle.ready())
        return "SCENE-PBR-TEXTURE-TYPE";
    if (handle.generation() != generation_ || handle.get() != identity_)
        return "SCENE-PBR-TEXTURE-GENERATION-OWNER";
    return compare(*handle.get());
} catch (const std::bad_alloc&) {
    return "SCENE-PBR-TEXTURE-PUBLICATION-BUDGET";
}

} // namespace arp::renderer
