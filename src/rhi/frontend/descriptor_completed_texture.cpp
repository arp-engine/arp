#include "rhi/frontend/descriptor_completed_texture.h"

#include "rhi/frontend/bindless_produced_image.h"
#include "rhi/frontend/completed_color_texture_view.h"
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"
#include "rhi/gpu_device.h"

namespace arp::rhi {
const char* DescriptorSet::diagnoseOriginalCompletedDepthPublication(
    const DescriptorSetWriteObservation& observation,
    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& expected) const noexcept
{
    if (!observation.matchesObject(*this))
        return "RHI-ORIGINAL-COMPLETED-DEPTH-PUBLICATION-OBSERVATION";
    if (const auto* id = OriginalCompletedDepthTextureSource::validateOriginal(expected))
        return id;
    const auto& write = observation.write();
    const auto* binding = findDescriptorSetBinding(layout(), write.binding);
    if (!binding || binding->type != DescriptorType::SampledTexture || !binding->depthTexture
        || write.arrayElement >= binding->arrayCount
        || write.resourceKind != DescriptorResourceKind::TextureView
        || write.resource != expected->originalViewSource()->resource()
        || write.resourceState != ResourceState::ShaderRead)
        return "RHI-ORIGINAL-COMPLETED-DEPTH-PUBLICATION-BINDING";
    const auto published = originalCompletedDepthPublication(write.binding, write.arrayElement);
    return published && published.get() == expected.get()
            && core::SharedOwnerProjection::sameOwner(published, expected)
        ? nullptr
        : "RHI-ORIGINAL-COMPLETED-DEPTH-PUBLICATION-OWNER";
}

const char* OriginalTextureCellDescriptorContract::validateIntent(
    const OriginalTextureCellDescriptorRequest& request) noexcept
{
    if (!controlVersion(request.contractVersion))
        return "RHI-TEXTURE-CELL-DESCRIPTOR-VERSION";
    if (!request.prototypeOptIn)
        return "RHI-TEXTURE-CELL-DESCRIPTOR-OPT-IN";
    return request.maxNativeImageViews == 1u && request.maxNativeWrites == 1u
            && request.maxRetainedSourceOwners
                >= OriginalCommandTextureViewSource::completeRetainedOwnerCount
        ? nullptr
        : "RHI-TEXTURE-CELL-DESCRIPTOR-BUDGET";
}

const char* OriginalTextureCellDescriptorContract::validateSource(
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentRangeExpectation& identity,
    std::uint32_t version) noexcept
{
    if (!controlVersion(version) || identity.controlVersion() != controlVersion(version))
        return "RHI-TEXTURE-CELL-DESCRIPTOR-CONTROL";
    if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(source))
        return id;
    const auto& texture = *source->originalParentLease()->originalObject();
    if (const auto* id = texture.validateOriginalContentIdentity(identity))
        return id;
    const auto& view = *source->originalObject();
    const auto registration = view.originalResourceRegistration();
    if (!registration.matchesIdentity(
            &view, view.id().value, OriginalRegisteredResourceKind::TextureView)
        || !registration.matchesParent(texture.originalResourceRegistration()))
        return "RHI-TEXTURE-CELL-DESCRIPTOR-PARENT";
    const auto& vd = view.desc();
    const bool storage = vd.usage == TextureUsage::Storage;
    if (identity.range()
        != TextureSubresourceRange{
            vd.baseMipLevel, vd.mipLevelCount, vd.baseArrayLayer, vd.arrayLayerCount, vd.aspect})
        return "RHI-TEXTURE-CELL-DESCRIPTOR-RANGE";
    if (isProducedImageRead(version))
        return OriginalBindlessProducedImageReadContract::inspectView(texture.desc(), vd);
    return OriginalStorageTextureContentPlanContract::inspectViewBinding(texture.desc(),
        vd,
        storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead,
        PipelineStage::ComputeShader,
        storage ? ResourceAccess::ShaderWrite : ResourceAccess::ShaderRead,
        storage);
}

const char* OriginalTextureCellDescriptorContract::validateBinding(
    const DescriptorBindingDesc& binding,
    const DescriptorBindingWrite& write,
    std::uint32_t version) noexcept
{
    if (!controlVersion(version))
        return "RHI-TEXTURE-CELL-DESCRIPTOR-VERSION";
    const bool storage = write.type == DescriptorType::StorageTexture;
    if ((isProducedImageRead(version) && storage)
        || (!storage && write.type != DescriptorType::SampledTexture) || binding.type != write.type
        || binding.binding != write.binding || write.arrayElement >= binding.arrayCount
        || binding.stages != ShaderStage::Compute || binding.dynamicOffset || binding.tableBacked
        || binding.depthTexture || write.resourceKind != DescriptorResourceKind::TextureView
        || !write.resource.isValid() || write.physicalLowering.enabled
        || write.resourceState != (storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead)
        || (binding.textureViewDimensionConstrained
            && binding.textureViewDimension != TextureViewDimension::Texture2D)
        || (storage
            && (binding.storageTextureFormat != TextureFormat::Rgba16Float
                || binding.storageTextureAccess != StorageTextureAccess::WriteOnly))
        || DescriptorBufferBindingRangeContract::validateSchema(write.bufferBindingRange, false))
        return "RHI-TEXTURE-CELL-DESCRIPTOR-BINDING";
    return nullptr;
}

ValidationResult DescriptorSet::writeOriginalTextureCell(const GpuDevice& device,
    const DescriptorBindingWrite& write,
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentRangeExpectation& identity,
    const OriginalTextureCellDescriptorRequest& request)
{
    if (!isCurrentAllocation())
        return {false, "RHI-DESCRIPTOR-ALLOCATION-STALE"};
    if (const auto* id = contentWriteDiagnostic())
        return {false, id};
    if (const auto* id = ordinaryWriteAdmissionDiagnostic())
        return {false, id};
    if (const auto* id = OriginalTextureCellDescriptorContract::validateIntent(request))
        return {false, id};
    if (const auto* id = OriginalTextureCellDescriptorContract::validateSource(
            source, identity, request.contractVersion))
        return {false, id};
    const DescriptorBindingDesc* binding = nullptr;
    for (const auto& candidate : layout().bindings)
        if (candidate.binding == write.binding)
            binding = &candidate;
    if (!binding)
        return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-BINDING"};
    if (const auto* id = OriginalTextureCellDescriptorContract::validateBinding(
            *binding, write, request.contractVersion))
        return {false, id};
    if (write.resource != source->resource()
        || source->originalObject()->desc().usage
            != (write.type == DescriptorType::StorageTexture ? TextureUsage::Storage
                                                             : TextureUsage::Sampled))
        return {false, "RHI-TEXTURE-CELL-DESCRIPTOR-WRITE"};
    for (const auto registration : {originalResourceRegistration(),
             source->originalObject()->originalResourceRegistration(),
             source->originalParentLease()->originalObject()->originalResourceRegistration()})
        if (const auto* id = device.validateOriginalResourceRegistration(registration))
            return {false, id};
    const auto decision =
        OriginalTextureCellDescriptorContract::lowering(device.backend(), request.contractVersion);
    if (!decision.publicationAvailable || decision.diagnostic)
        return {false, decision.diagnostic};
    auto result = writeOriginalTextureCellImpl(write, source, identity, request);
    if (result.success)
        commitContentWrite();
    return result;
}

const char* OriginalCompletedDepthTextureSource::inspect(
    const OriginalCommandTextureViewSource& source,
    const Budget& budget,
    std::uint64_t& cellCount) noexcept
{
    cellCount = 0u;
    if ((budget.contractVersion != 1u && budget.contractVersion != 2u) || !budget.prototypeOptIn)
        return "RHI-COMPLETED-DEPTH-SOURCE-VERSION-OPT-IN";
    if (!budget.maxCells || !budget.maxCpuMetadataBytes
        || budget.maxRetainedOwners < completeRetainedOwnerCount)
        return "RHI-COMPLETED-DEPTH-SOURCE-BUDGET";
    if (const auto* id = source.validate())
        return id;
    const auto& texture = *source.originalParentLease()->originalObject();
    const auto& view = source.originalObject()->desc();
    const auto& desc = texture.desc();
    const auto registration = source.originalObject()->originalResourceRegistration();
    if (!registration.matchesIdentity(source.originalObject(),
            source.resource().value,
            OriginalRegisteredResourceKind::TextureView)
        || !registration.matchesParent(texture.originalResourceRegistration()))
        return "RHI-COMPLETED-DEPTH-SOURCE-PARENT";
    if (desc.format != TextureFormat::D32Float || desc.extent.depth != 1u || desc.sampleCount != 1u
        || desc.cubeCompatible || (desc.usage & TextureUsage::Sampled) == TextureUsage::None
        || view.format != desc.format || view.dimension != TextureViewDimension::Texture2D
        || view.usage != TextureUsage::Sampled || view.aspect != TextureAspect::Depth
        || view.arrayLayerCount != 1u)
        return "RHI-COMPLETED-DEPTH-SOURCE-SHAPE";
    const char* id = nullptr;
    const auto geometry = semantics::OriginalTextureStateRange::capture(
        desc, std::uint64_t(desc.mipLevelCount) * desc.arrayLayerCount, id);
    if (!geometry)
        return id;
    const TextureSubresourceRange range{view.baseMipLevel,
        view.mipLevelCount,
        view.baseArrayLayer,
        view.arrayLayerCount,
        view.aspect};
    if ((id = geometry->validate(range)))
        return id;
    cellCount = geometry->rangeCellCount(range);
    if (budget.contractVersion == 2u && (geometry->cellCount() != 1u || cellCount != 1u))
        return "RHI-COMPLETED-DEPTH-SOURCE-NATIVE-SINGLE-CELL";
    return cellCount && cellCount <= budget.maxCells && cellCount <= SIZE_MAX
        ? nullptr
        : "RHI-COMPLETED-DEPTH-SOURCE-CELL-CAPACITY";
}

std::shared_ptr<const OriginalCompletedDepthTextureSource>
OriginalCompletedDepthTextureSource::prepareOriginal(
    std::shared_ptr<const OriginalCommandTextureViewSource> source,
    Budget budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic)
{
    diagnostic = OriginalCommandTextureViewSource::validateOriginal(source);
    if (diagnostic)
        return {};
    std::uint64_t count = 0u;
    if ((diagnostic = inspect(*source, budget, count)))
        return {};
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes
        || !core::SharedOwnerProjection::sameOwner(quota, source->originalMetadataQuota())) {
        diagnostic = "RHI-COMPLETED-DEPTH-SOURCE-QUOTA";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<OriginalCompletedDepthTextureSource>(
            ConstructionKey{}, source, budget);
        result->cells_.reserve(static_cast<std::size_t>(count));
        const auto& view = source->originalObject()->desc();
        const auto& texture = *source->originalParentLease()->originalObject();
        for (std::uint32_t mip = 0u; mip < view.mipLevelCount; ++mip) {
            const auto observation = texture.observeOriginalContent(
                {view.baseMipLevel + mip, 1u, view.baseArrayLayer, 1u, TextureAspect::Depth});
            if (!observation
                || validateControl(budget.contractVersion, observation->controlVersion())
                || observation->state() != common::OriginalTextureContentControl::State::Completed
                || !observation->contentGeneration() || !observation->revision()) {
                diagnostic = "RHI-COMPLETED-DEPTH-SOURCE-CONTENT";
                return {};
            }
            result->cells_.push_back(*observation);
        }
        if ((diagnostic = result->validate()))
            return {};
        return shareOriginal(std::move(result));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-COMPLETED-DEPTH-SOURCE-ALLOCATION";
        return {};
    }
}

const char* OriginalCompletedDepthTextureSource::validateOriginal(
    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& source) noexcept
{
    if (!source || !source->hasQuotaBoundSharedOwner(source))
        return "RHI-COMPLETED-DEPTH-SOURCE-OWNER";
    return source->validate();
}

const char* OriginalCompletedDepthTextureSource::validate() const noexcept
{
    if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(source_))
        return id;
    if (!originalMetadataQuota()
        || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes
        || !core::SharedOwnerProjection::sameOwner(
            originalMetadataQuota(), source_->originalMetadataQuota()))
        return "RHI-COMPLETED-DEPTH-SOURCE-QUOTA";
    std::uint64_t count = 0u;
    if (const auto* id = inspect(*source_, budget_, count))
        return id;
    if (cells_.size() != count)
        return "RHI-COMPLETED-DEPTH-SOURCE-CELL-CAPACITY";
    const auto& texture = *source_->originalParentLease()->originalObject();
    const auto& view = source_->originalObject()->desc();
    std::uint64_t revision = 0u;
    for (std::size_t i = 0u; i < cells_.size(); ++i) {
        const auto& cell = cells_[i];
        if (const auto* id = texture.validateOriginalContent(cell))
            return id;
        if (cell.range()
                != TextureSubresourceRange{view.baseMipLevel + static_cast<std::uint32_t>(i),
                    1u,
                    view.baseArrayLayer,
                    1u,
                    TextureAspect::Depth}
            || validateControl(budget_.contractVersion, cell.controlVersion())
            || cell.state() != common::OriginalTextureContentControl::State::Completed
            || !cell.contentGeneration() || !cell.revision()
            || (revision && cell.revision() != revision))
            return "RHI-COMPLETED-DEPTH-SOURCE-CONTENT";
        revision = cell.revision();
    }
    return nullptr;
}

OriginalCompletedDepthReadInputContract::Inspection
OriginalCompletedDepthReadInputContract::inspect(const Input& input,
    const std::shared_ptr<const OriginalCommandTextureViewSource>& canonicalView,
    const Budget& budget) noexcept
{
    if (const auto* id = validateIntent(budget))
        return {id};
    if (const auto* id =
            validateAccess(input.state, input.stages, input.access, budget.contractVersion))
        return {id};
    if (const auto* id = OriginalCompletedDepthTextureSource::validateOriginal(input.source))
        return {id};
    const auto sourceVersion = budget.contractVersion == 3u ? 2u : budget.contractVersion;
    if (input.source->budget().contractVersion != sourceVersion)
        return {"RHI-COMPLETED-DEPTH-READ-INPUT-SOURCE-PROFILE"};
    if (budget.contractVersion == 2u && input.stages != PipelineStage::ComputeShader)
        return {"RHI-COMPLETED-DEPTH-READ-INPUT-NATIVE-STAGE"};
    if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(canonicalView))
        return {id};
    const auto& originalView = input.source->originalViewSource();
    if (canonicalView.get() != originalView.get()
        || !core::SharedOwnerProjection::sameOwner(canonicalView, originalView))
        return {"RHI-COMPLETED-DEPTH-READ-INPUT-CANONICAL-VIEW"};
    if (input.source->cells().size() > budget.maxCells)
        return {"RHI-COMPLETED-DEPTH-READ-INPUT-CELL-CAPACITY"};
    std::uint64_t bytes = 0u;
    if (const auto* id = semantics::TextureUploadDestinationValidation::footprint(
            canonicalView->originalParentLease()->originalObject()->desc(), bytes))
        return {id};
    if (bytes > budget.maxTextureTexelBytes)
        return {"RHI-COMPLETED-DEPTH-READ-INPUT-TEXEL-CAPACITY"};
    return {nullptr, input.source->cells().size(), bytes};
}

const char* OriginalCompletedTextureDescriptorContract::validateIntent(
    const OriginalCompletedTextureDescriptorRequest& request) noexcept
{
    if (request.contractVersion != 1u && request.contractVersion != 2u
        && request.contractVersion != 3u && request.contractVersion != 4u
        && request.contractVersion != 5u && request.contractVersion != 6u)
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-VERSION";
    if (!request.prototypeOptIn)
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-OPT-IN";
    if (request.maxNativeImageViews != 1u || request.maxNativeWrites != 1u
        || request.maxRetainedSourceOwners
            < ((request.contractVersion == 3u || request.contractVersion == 4u)
                    ? OriginalCompletedDepthTextureSource::completeRetainedOwnerCount
                    : OriginalCommandTextureViewSource::completeRetainedOwnerCount))
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-BUDGET";
    return nullptr;
}

const char* OriginalCompletedTextureDescriptorContract::validateSource(
    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& source,
    std::uint32_t publicationVersion) noexcept
{
    if (publicationVersion != 3u && publicationVersion != 4u)
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-VERSION";
    if (const auto* id = OriginalCompletedDepthTextureSource::validateOriginal(source))
        return id;
    return source->budget().contractVersion == (publicationVersion == 3u ? 1u : 2u)
        ? nullptr
        : "RHI-COMPLETED-TEXTURE-DESCRIPTOR-DEPTH-PROFILE";
}

const char* OriginalCompletedTextureDescriptorContract::validateSource(
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentExpectation& content) noexcept
{
    return validateSource(source, content, 1u);
}

const char* OriginalCompletedTextureDescriptorContract::validateSource(
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentExpectation& content,
    std::uint32_t publicationVersion) noexcept
{
    if (const auto* id = OriginalCommandTextureViewSource::validateOriginal(source))
        return id;
    const auto& texture = *source->originalParentLease()->originalObject();
    const auto& view = *source->originalObject();
    if (const auto* id = texture.validateOriginalContent(content))
        return id;
    if (validateControl(content.controlVersion(), publicationVersion)
        || content.state() != common::OriginalTextureContentControl::State::Completed
        || !content.contentGeneration() || !content.revision())
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-CONTENT";
    const auto registration = view.originalResourceRegistration();
    if (!registration.matchesIdentity(
            &view, view.id().value, OriginalRegisteredResourceKind::TextureView)
        || !registration.matchesParent(texture.originalResourceRegistration()))
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-PARENT";
    return validateView(texture.desc(), view.desc(), publicationVersion);
}

const char* OriginalCompletedTextureDescriptorContract::validateView(
    const TextureDesc& td, const TextureViewDesc& vd, std::uint32_t publicationVersion) noexcept
{
    if (publicationVersion != 1u && publicationVersion != 2u && publicationVersion != 5u
        && publicationVersion != 6u)
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-VERSION";
    const bool nativeCube =
        (publicationVersion == 1u || publicationVersion == 5u || publicationVersion == 6u)
        && OriginalCompletedColorTextureViewContract::isSingleCube(td, vd);
    if ((td.format != TextureFormat::Rgba8Unorm
            && ((publicationVersion != 5u && publicationVersion != 6u)
                || td.format != TextureFormat::Rgba16Float))
        || !td.extent.width || !td.extent.height || !td.mipLevelCount || td.extent.depth != 1u
        || td.sampleCount != 1u || (td.cubeCompatible && !nativeCube) || !td.arrayLayerCount
        || (td.usage & TextureUsage::Sampled) == TextureUsage::None || vd.format != td.format
        || vd.usage != TextureUsage::Sampled || vd.aspect != TextureAspect::Color
        || (vd.dimension != TextureViewDimension::Texture2D
            && vd.dimension != TextureViewDimension::Texture2DArray && !nativeCube)
        || (vd.dimension == TextureViewDimension::Texture2D && td.arrayLayerCount != 1u))
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-SHAPE";
    if (vd.baseMipLevel || vd.baseArrayLayer || vd.mipLevelCount != td.mipLevelCount
        || vd.arrayLayerCount != td.arrayLayerCount)
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-RANGE";
    return nullptr;
}

namespace {
const char* validateCompletedTextureWrite(const GpuDevice& device,
    const OriginalResourceRegistrationTicket& descriptor,
    const DescriptorBindingWrite& write,
    const OriginalCommandTextureViewSource& source,
    const OriginalCompletedTextureDescriptorRequest& request) noexcept
{
    if (write.type != DescriptorType::SampledTexture
        || write.resourceKind != DescriptorResourceKind::TextureView
        || write.resource != source.resource() || write.resourceState != ResourceState::ShaderRead
        || write.physicalLowering.enabled
        || DescriptorBufferBindingRangeContract::validateSchema(write.bufferBindingRange, false))
        return "RHI-COMPLETED-TEXTURE-DESCRIPTOR-WRITE";
    for (const auto registration : {descriptor,
             source.originalObject()->originalResourceRegistration(),
             source.originalParentLease()->originalObject()->originalResourceRegistration()})
        if (const auto* id = device.validateOriginalResourceRegistration(registration))
            return id;
    const auto decision = OriginalCompletedTextureDescriptorContract::lowering(
        device.backend(), request.contractVersion);
    if (!decision.publicationAvailable || decision.diagnostic)
        return decision.diagnostic;
    return nullptr;
}
} // namespace

ValidationResult DescriptorSet::writeOriginalCompletedTexture(const GpuDevice& device,
    const DescriptorBindingWrite& write,
    const std::shared_ptr<const OriginalCommandTextureViewSource>& source,
    const GpuTexture::ContentExpectation& content,
    const OriginalCompletedTextureDescriptorRequest& request)
{
    if (!isCurrentAllocation())
        return {false, "RHI-DESCRIPTOR-ALLOCATION-STALE"};
    if (const auto* id = contentWriteDiagnostic())
        return {false, id};
    if (const auto* id = ordinaryWriteAdmissionDiagnostic())
        return {false, id};
    if (const auto* id = OriginalCompletedTextureDescriptorContract::validateIntent(request))
        return {false, id};
    if (const auto* id = OriginalCompletedTextureDescriptorContract::validateSource(
            source, content, request.contractVersion))
        return {false, id};
    if (const auto* id = validateCompletedTextureWrite(
            device, originalResourceRegistration(), write, *source, request))
        return {false, id};
    auto result = writeOriginalCompletedTextureImpl(write, source, content, request);
    if (result.success)
        commitContentWrite();
    return result;
}

ValidationResult DescriptorSet::writeOriginalCompletedTexture(const GpuDevice& device,
    const DescriptorBindingWrite& write,
    const std::shared_ptr<const OriginalCompletedDepthTextureSource>& content,
    const OriginalCompletedTextureDescriptorRequest& request)
{
    if (!isCurrentAllocation())
        return {false, "RHI-DESCRIPTOR-ALLOCATION-STALE"};
    if (const auto* id = contentWriteDiagnostic())
        return {false, id};
    if (const auto* id = ordinaryWriteAdmissionDiagnostic())
        return {false, id};
    if (const auto* id = OriginalCompletedTextureDescriptorContract::validateIntent(request))
        return {false, id};
    if (const auto* id = OriginalCompletedTextureDescriptorContract::validateSource(
            content, request.contractVersion))
        return {false, id};
    const auto& source = content->originalViewSource();
    if (const auto* id = validateCompletedTextureWrite(
            device, originalResourceRegistration(), write, *source, request))
        return {false, id};
    auto result = writeOriginalCompletedTextureImpl(write, content);
    if (result.success)
        commitContentWrite();
    return result;
}
} // namespace arp::rhi
