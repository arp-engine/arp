#pragma once

#include "rhi/frontend/bindless_attachment_clear.h"
#include "rhi/frontend/bindless_produced_image.h"
#include "rhi/frontend/bindless_snapshot_scope.h"
#include "rhi/frontend/resource_texture_validation.h"
#include "rhi/gpu_commands.h"

namespace arp::rhi {
// ContentPlan6 is a distinct mixed-content schema. These value checks do not
// authenticate original controls, receipts, owners, journals or native work.
// Only the original transaction's exact-type composition authenticates /6.
// Value-schema validation does not grant native or public execution authority.
struct OriginalFiniteGraphicsContentPlanBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCompletedControls = 0u, maxAttachmentControls = 0u, maxAttachmentCells = 0u,
                  maxSampledSteps = 0u, maxAttachmentSteps = 0u, maxCommandVisits = 0u,
                  maxProjectionVisits = 0u;
    // /1 is initialized direct geometry; /2 composes Inputs8/RootState6
    // producer and indexed-indirect consumers in the same original journal.
    std::uint32_t bindlessGeometryContractVersion = 0u;
    bool bindlessGeometryOptIn = false;
    OriginalBindlessAttachmentClearIntent attachmentClear;
    // Independent /1: canonical whole-upload control1 through this same Root9.
    // Default retains control3/read8; /1 selects control1/read10/Pending6.
    // /2 selects control3/read17/Pending7 with RGBA16F color views.
    std::uint32_t bindlessCompletedReadContractVersion = 0u;
    OriginalBindlessProducedImageReadContract::Intent producedImageRead;
    OriginalBindlessSnapshotScopeContract::Intent snapshotScope;
};

class OriginalFiniteGraphicsContentPlanContract final {
public:
    static constexpr bool isMixed(std::uint32_t version) noexcept
    {
        return version == 6u || version == 9u;
    }

    struct Profile {
        std::uint32_t sources = 0u, inputs = 0u, state = 0u, sequence = 0u, usage = 0u,
                      dependency = 0u, consumption = 0u, renderUsage = 0u, generalResources = 0u;
        bool generalResourcesOptIn = false, graphicsPipeline = false, computePipeline = false;
    };

    struct Counts {
        std::uint64_t completedControls = 0u, attachmentControls = 0u, attachmentCells = 0u,
                      renderBindingReads = 0u, drawReads = 0u, attachmentSteps = 0u,
                      commandVisits = 0u, projectionVisits = 0u;
        std::uint64_t producedImageControls = 0u, producedImageCells = 0u, producedImageSteps = 0u;
    };

    enum class SampledOrigin { RenderBinding, ShaderConsumption };

    static constexpr auto lowering(Backend backend, std::uint32_t version = 6u) noexcept
    {
        return semantics::OriginalCommandStateAdmissionLowering::finiteGraphicsContentPlanLowering(
            backend, version);
    }

    static constexpr const char* validateIntent(
        const OriginalFiniteGraphicsContentPlanBudget& b) noexcept
    {
        if (b.contractVersion != 6u)
            return "RHI-FINITE-GRAPHICS-CONTENT-VERSION";
        if (!b.producedImageRead.empty())
            return "RHI-FINITE-GRAPHICS-CONTENT-PRODUCED-IMAGE-SCHEMA";
        if (!b.snapshotScope.empty())
            return "RHI-FINITE-GRAPHICS-CONTENT-SNAPSHOT-SCOPE-SCHEMA";
        if (b.bindlessGeometryContractVersion || b.bindlessGeometryOptIn)
            return "RHI-FINITE-GRAPHICS-CONTENT-BINDLESS-GEOMETRY-SCHEMA";
        if (b.bindlessCompletedReadContractVersion)
            return "RHI-FINITE-GRAPHICS-CONTENT-BINDLESS-COMPLETED-SCHEMA";
        if (b.attachmentClear.contractVersion || b.attachmentClear.prototypeOptIn)
            return "RHI-FINITE-GRAPHICS-CONTENT-BINDLESS-ATTACHMENT-SCHEMA";
        if (!b.prototypeOptIn)
            return "RHI-FINITE-GRAPHICS-CONTENT-OPT-IN";
        if (!b.maxCompletedControls || !b.maxAttachmentControls || !b.maxAttachmentCells
            || !b.maxSampledSteps || !b.maxAttachmentSteps || !b.maxCommandVisits
            || !b.maxProjectionVisits || b.maxAttachmentCells > SIZE_MAX)
            return "RHI-FINITE-GRAPHICS-CONTENT-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateRootProfile(const Profile& p) noexcept
    {
        return p.sources == 5u && p.inputs == 6u && p.state == 5u && p.sequence == 5u
                && p.usage == 6u && p.dependency == 6u && p.consumption == 4u && p.renderUsage == 4u
                && p.generalResources == 2u && p.generalResourcesOptIn && p.graphicsPipeline
                && !p.computePipeline
            ? nullptr
            : "RHI-FINITE-GRAPHICS-CONTENT-ROOT-PROFILE";
    }

    static constexpr const char* validateCounts(
        const OriginalFiniteGraphicsContentPlanBudget& b, const Counts& c) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (!c.completedControls || !c.attachmentControls || !c.attachmentCells
            || !c.renderBindingReads || !c.drawReads || !c.attachmentSteps || !c.commandVisits
            || !c.projectionVisits || c.renderBindingReads < c.completedControls
            || c.drawReads < c.completedControls || c.attachmentCells != c.attachmentControls
            || c.attachmentCells > c.attachmentSteps / 2u)
            return "RHI-FINITE-GRAPHICS-CONTENT-INCOMPLETE";
        if (c.producedImageControls || c.producedImageCells || c.producedImageSteps)
            return "RHI-FINITE-GRAPHICS-CONTENT-PRODUCED-IMAGE-SCHEMA";
        if (c.completedControls > b.maxCompletedControls
            || c.attachmentControls > b.maxAttachmentControls)
            return "RHI-FINITE-GRAPHICS-CONTENT-CONTROL-CAPACITY";
        if (c.attachmentCells > b.maxAttachmentCells)
            return "RHI-FINITE-GRAPHICS-CONTENT-CELL-CAPACITY";
        // Subtract before adding, so hostile UINT64_MAX counts cannot wrap.
        if (c.renderBindingReads > b.maxSampledSteps
            || c.drawReads > b.maxSampledSteps - c.renderBindingReads
            || c.attachmentSteps > b.maxAttachmentSteps)
            return "RHI-FINITE-GRAPHICS-CONTENT-STEP-CAPACITY";
        if (c.commandVisits > b.maxCommandVisits || c.projectionVisits > b.maxProjectionVisits)
            return "RHI-FINITE-GRAPHICS-CONTENT-VISIT-CAPACITY";
        return nullptr;
    }

    static const char* validateSampledUse(const TextureDesc& texture,
        const TextureViewDesc& view,
        ResourceState state,
        PipelineStage stages,
        ResourceAccess access,
        SampledOrigin origin) noexcept
    {
        if (origin != SampledOrigin::RenderBinding && origin != SampledOrigin::ShaderConsumption)
            return "RHI-FINITE-GRAPHICS-CONTENT-SAMPLED-ORIGIN";
        if (stages != PipelineStage::FragmentShader)
            return "RHI-FINITE-GRAPHICS-CONTENT-SAMPLED-STAGE";
        return OriginalCompletedTextureReadPlanContract::inspectViewBinding(
            texture, view, state, stages, access);
    }

    static const char* validateIndependentResources(GpuResourceId sampled,
        std::uint64_t sampledGeneration,
        GpuResourceId attachment,
        std::uint64_t attachmentGeneration) noexcept
    {
        return sampled.isValid() && attachment.isValid() && sampledGeneration
                && attachmentGeneration && sampled != attachment
            ? nullptr
            : "RHI-FINITE-GRAPHICS-CONTENT-RESOURCE-IDENTITY";
    }

    static const char* inspectAttachment(const TextureDesc& texture,
        const TextureViewDesc& view,
        const OriginalTextureContentCreationBudget& creation,
        LoadOp load,
        StoreOp store,
        bool resolve,
        bool layered,
        std::uint64_t& texelBytes) noexcept
    {
        texelBytes = 0u;
        if (creation.contractVersion != 2u || load != LoadOp::Clear || store != StoreOp::Store
            || resolve || layered)
            return "RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-INTENT";
        if (texture.format != TextureFormat::Rgba16Float || texture.mipLevelCount != 1u
            || texture.arrayLayerCount != 1u
            || texture.usage != (TextureUsage::RenderAttachment | TextureUsage::CopySrc)
            || view.dimension != TextureViewDimension::Texture2D || view.format != texture.format
            || view.usage != TextureUsage::RenderAttachment || view.aspect != TextureAspect::Color
            || view.baseMipLevel || view.mipLevelCount != 1u || view.baseArrayLayer
            || view.arrayLayerCount != 1u)
            return "RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-PROFILE";
        return OriginalTextureContentCreationContract::inspect(texture, creation, texelBytes);
    }
};

// ContentPlan /9 composes the original BindlessRoot /4 with native-created
// attachment /4 cells and completed upload /3 reads. It reuses the /6 bounded
// storage/count schema; General /6 authorization never admits this domain.
class OriginalBindlessGraphicsContentPlanContract final {
public:
    using Budget = OriginalFiniteGraphicsContentPlanBudget;
    using Counts = OriginalFiniteGraphicsContentPlanContract::Counts;

    static constexpr std::uint32_t rootStateVersion(std::uint32_t geometry) noexcept
    {
        return geometry == 2u ? 6u : geometry ? 5u : 4u;
    }

    static constexpr bool consumes(CommandUsageKind kind, std::uint32_t geometry) noexcept
    {
        return geometry == 2u
            ? kind == CommandUsageKind::Dispatch || kind == CommandUsageKind::DrawIndexedIndirect
            : kind == (geometry ? CommandUsageKind::DrawIndexed : CommandUsageKind::Draw);
    }

    static constexpr PipelineStage consumptionStages(CommandUsageKind kind) noexcept
    {
        return kind == CommandUsageKind::Dispatch
            ? PipelineStage::ComputeShader
            : PipelineStage::VertexShader | PipelineStage::FragmentShader;
    }

    static constexpr auto lowering(Backend backend,
        std::uint32_t geometryVersion = 0u,
        std::uint32_t attachmentClearVersion = 0u,
        std::uint32_t completedReadVersion = 0u) noexcept
    {
        auto result =
            semantics::OriginalCommandStateAdmissionLowering::bindlessGraphicsContentPlanLowering(
                backend, geometryVersion);
        if (completedReadVersion) {
            const auto read =
                semantics::OriginalCommandStateAdmissionLowering::bindlessCompletedReadPlanLowering(
                    backend, completedReadVersion);
            if (!read.rootAvailable)
                return read;
        }
        if (attachmentClearVersion) {
            const auto clear =
                OriginalBindlessAttachmentClearContract::lowering(backend, attachmentClearVersion);
            if (!clear.preparationAvailable) {
                result.validationAvailable = false;
                result.rootAvailable = false;
                result.executionAvailable = false;
                result.mode = CapabilityMode::DiagnosticFailure;
                result.diagnostic = clear.diagnostic;
            }
        }
        return result;
    }

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 9u)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-OPT-IN";
        if (const auto* id = OriginalBindlessSnapshotScopeContract::validateIntent(b.snapshotScope))
            return id;
        if (!b.snapshotScope.empty()
            && (b.bindlessGeometryContractVersion != 2u || !b.bindlessGeometryOptIn))
            return "RHI-BINDLESS-GRAPHICS-CONTENT-SNAPSHOT-SCOPE-PROFILE";
        if (const auto* id =
                OriginalBindlessProducedImageReadContract::validateIntent(b.producedImageRead))
            return id;
        if (!b.producedImageRead.empty()
            && (b.bindlessGeometryContractVersion != 2u || !b.bindlessGeometryOptIn))
            return "RHI-BINDLESS-PRODUCED-IMAGE-CONTENT-PROFILE";
        if (b.producedImageRead.maxCells > SIZE_MAX - b.maxAttachmentCells)
            return "RHI-BINDLESS-PRODUCED-IMAGE-BUDGET";
        if (b.bindlessCompletedReadContractVersion > 3u)
            return "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-VERSION";
        if (b.bindlessCompletedReadContractVersion
            && (b.bindlessGeometryContractVersion || b.bindlessGeometryOptIn
                || !b.maxCompletedControls || !b.maxSampledSteps))
            return "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-SCHEMA";
        if (const auto* id =
                OriginalBindlessAttachmentClearContract::validateIntent(b.attachmentClear))
            return id;
        if (b.bindlessGeometryContractVersion > 2u
            || (!b.bindlessGeometryContractVersion && b.bindlessGeometryOptIn))
            return "RHI-BINDLESS-GRAPHICS-CONTENT-GEOMETRY-VERSION";
        if (b.bindlessGeometryContractVersion && !b.bindlessGeometryOptIn)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-GEOMETRY-OPT-IN";
        if (!b.maxAttachmentControls || !b.maxAttachmentCells || !b.maxAttachmentSteps
            || !b.maxCommandVisits || !b.maxProjectionVisits || b.maxAttachmentCells > SIZE_MAX
            || bool(b.maxCompletedControls) != bool(b.maxSampledSteps))
            return "RHI-BINDLESS-GRAPHICS-CONTENT-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateProfile(std::uint32_t sources,
        std::uint32_t inputs,
        std::uint32_t state,
        std::uint32_t sequence,
        std::uint32_t stream,
        std::uint32_t roots,
        bool graphics,
        bool compute,
        std::uint32_t geometryVersion = 0u) noexcept
    {
        return geometryVersion <= 2u && sources == 6u
                && inputs
                    == (geometryVersion == 2u ? 8u
                            : geometryVersion ? 7u
                                              : 1u)
                && state == 1u && sequence == 1u && stream == 1u
                && roots == rootStateVersion(geometryVersion) && graphics
                && (geometryVersion == 2u || !compute)
            ? nullptr
            : "RHI-BINDLESS-GRAPHICS-CONTENT-ROOT-PROFILE";
    }

    static constexpr const char* validateCounts(const Budget& b, const Counts& c) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (b.producedImageRead.empty()
                ? (c.producedImageControls || c.producedImageCells || c.producedImageSteps)
                : (!c.producedImageControls || c.producedImageCells < c.producedImageControls
                      || c.producedImageSteps < c.producedImageCells
                      || c.producedImageControls > b.producedImageRead.maxControls
                      || c.producedImageCells > b.producedImageRead.maxCells
                      || c.producedImageSteps > b.producedImageRead.maxSteps))
            return "RHI-BINDLESS-PRODUCED-IMAGE-CONTENT-CAPACITY";
        if (!c.attachmentControls || c.attachmentCells != c.attachmentControls
            || c.attachmentSteps / 2u != c.attachmentCells || c.attachmentSteps % 2u
            || c.renderBindingReads || !c.commandVisits || !c.projectionVisits
            || bool(c.completedControls) != bool(c.drawReads) || c.drawReads < c.completedControls)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-INCOMPLETE";
        if (c.completedControls > b.maxCompletedControls
            || c.attachmentControls > b.maxAttachmentControls
            || c.attachmentCells > b.maxAttachmentCells)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-CONTROL-CAPACITY";
        if (c.drawReads > b.maxSampledSteps || c.attachmentSteps > b.maxAttachmentSteps)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-STEP-CAPACITY";
        if (c.commandVisits > b.maxCommandVisits || c.projectionVisits > b.maxProjectionVisits)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-VISIT-CAPACITY";
        return nullptr;
    }

    static const char* inspectAttachment(const TextureDesc& texture,
        const TextureViewDesc& view,
        const OriginalTextureContentCreationBudget& creation,
        LoadOp load,
        StoreOp store,
        bool resolve,
        bool layered,
        std::uint64_t& texelBytes) noexcept
    {
        texelBytes = 0u;
        if (!OriginalTextureContentCreationContract::isBindlessCellCreation(
                creation.contractVersion)
            || load != LoadOp::Clear || store != StoreOp::Store || resolve || layered)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-ATTACHMENT-INTENT";
        if ((texture.format != TextureFormat::Rgba8Unorm
                && texture.format != TextureFormat::Rgba16Float
                && texture.format != TextureFormat::D32Float)
            || texture.mipLevelCount != 1u || texture.arrayLayerCount != 1u
            || view.dimension != TextureViewDimension::Texture2D || view.format != texture.format
            || view.usage != TextureUsage::RenderAttachment
            || view.aspect
                != (texture.format == TextureFormat::D32Float ? TextureAspect::Depth
                                                              : TextureAspect::Color)
            || view.baseMipLevel || view.mipLevelCount != 1u || view.baseArrayLayer
            || view.arrayLayerCount != 1u)
            return "RHI-BINDLESS-GRAPHICS-CONTENT-ATTACHMENT-PROFILE";
        return OriginalTextureContentCreationContract::inspect(texture, creation, texelBytes);
    }
};
} // namespace arp::rhi
