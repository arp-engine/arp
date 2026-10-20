#pragma once
#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/finite_graphics_content_validation.h"
#include "rhi/semantics/bindless_ordinary_depth_domain.h"

namespace arp::rhi {
// ContentPlan18 is an independent composition schema, not a new meaning of /9
// or the Compute-only /12. All controls still belong to the original inventory,
// state/content journal and real completion transaction. These allocation-free
// value checks cannot manufacture those owners or issue a prepared transaction.
class OriginalBindlessGraphicsDepthContentPlanContract final {
public:
    static constexpr std::uint32_t version = 18u;
    static constexpr std::uint32_t canonicalVersion = 19u;

    static constexpr bool isVersion(std::uint32_t v) noexcept
    {
        return v == version || v == canonicalVersion;
    }

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        // /9 validates only the color reads and independent attachments. Its
        // visit ceilings cover the combined traversal, including depth reads.
        OriginalFiniteGraphicsContentPlanBudget graphics;
        std::uint64_t maxDepthControls = 0u, maxDepthCells = 0u, maxDepthReadSteps = 0u;
    };

    struct Counts {
        OriginalFiniteGraphicsContentPlanContract::Counts graphics;
        std::uint64_t depthControls = 0u, depthCells = 0u, depthReadSteps = 0u,
                      depthCommandVisits = 0u, depthProjectionVisits = 0u;
    };

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (!isVersion(b.contractVersion))
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-OPT-IN";
        if (const auto* id =
                OriginalBindlessGraphicsContentPlanContract::validateIntent(b.graphics))
            return id;
        // Canonical color control1/Input1 and native depth control4/Input3 have
        // distinct completion authorities. This first native schema must not
        // silently mix them. RGBA8/control3 and HDR/control3 are composable.
        if (b.contractVersion == canonicalVersion
                ? b.graphics.bindlessCompletedReadContractVersion != 3u
                : b.graphics.bindlessCompletedReadContractVersion == 1u
                    || b.graphics.bindlessCompletedReadContractVersion == 3u)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-COLOR-PROFILE";
        if (!b.maxDepthControls || !b.maxDepthCells || !b.maxDepthReadSteps
            || b.maxDepthControls > SIZE_MAX || b.maxDepthCells > SIZE_MAX
            || b.maxDepthCells < b.maxDepthControls || b.maxDepthReadSteps < b.maxDepthControls)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateCounts(const Budget& b, const Counts& c) noexcept
    {
        if (const auto* id = validateIntent(b))
            return id;
        if (const auto* id =
                OriginalBindlessGraphicsContentPlanContract::validateCounts(b.graphics, c.graphics))
            return id;
        if (!c.depthControls || c.depthCells < c.depthControls
            || (b.contractVersion == version && c.depthCells != c.depthControls)
            || c.depthReadSteps < c.depthCells || !c.depthCommandVisits || !c.depthProjectionVisits)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-INCOMPLETE";
        if (c.depthControls > b.maxDepthControls || c.depthCells > b.maxDepthCells
            || c.depthReadSteps > b.maxDepthReadSteps)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-CAPACITY";
        // The color inspector already proves its own visits fit these limits.
        // Subtraction preserves that proof under hostile UINT64_MAX counts.
        if (c.depthCommandVisits > b.graphics.maxCommandVisits - c.graphics.commandVisits
            || c.depthProjectionVisits
                > b.graphics.maxProjectionVisits - c.graphics.projectionVisits)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-VISIT-CAPACITY";
        return nullptr;
    }

    static constexpr const char* validateDepthControl(std::uint32_t descriptorSourceVersion,
        std::uint32_t inputVersion,
        std::uint32_t controlVersion,
        bool completed,
        std::uint64_t generation,
        std::uint64_t revision,
        std::uint64_t cells,
        std::uint64_t mutations,
        std::uint32_t planVersion = version) noexcept
    {
        const bool canonical = planVersion == canonicalVersion;
        return isVersion(planVersion) && descriptorSourceVersion == 6u
                && inputVersion == (canonical ? 1u : 3u) && controlVersion == (canonical ? 2u : 4u)
                && completed && generation && revision && cells && (canonical || cells == 1u)
                && !mutations
            ? nullptr
            : "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-CONTROL";
    }

    static const char* inspectDepthRead(const TextureDesc& texture,
        const TextureViewDesc& view,
        ResourceState state,
        PipelineStage stages,
        ResourceAccess access,
        std::uint32_t planVersion = version) noexcept
    {
        if (!isVersion(planVersion))
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-VERSION";
        // Native completed-depth Source2/control4 observes one original D32
        // cell. Additional mip/layer/format domains need their own evidence.
        if (texture.format != TextureFormat::D32Float || texture.sampleCount != 1u
            || !texture.extent.width || !texture.extent.height || texture.extent.depth != 1u
            || !texture.mipLevelCount || (planVersion == version && texture.mipLevelCount != 1u)
            || texture.arrayLayerCount != 1u || texture.cubeCompatible
            || (texture.usage & TextureUsage::Sampled) != TextureUsage::Sampled
            || view.format != texture.format || view.dimension != TextureViewDimension::Texture2D
            || view.usage != TextureUsage::Sampled || view.aspect != TextureAspect::Depth
            || view.baseMipLevel || view.mipLevelCount != texture.mipLevelCount
            || view.baseArrayLayer || view.arrayLayerCount != 1u)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-VIEW";
        if (stages != PipelineStage::FragmentShader)
            return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-STAGE";
        return OriginalCompletedDepthReadInputContract::validateAccess(state, stages, access, 3u);
    }

    static const char* validateIndependentAttachment(GpuResourceId depth,
        std::uint64_t depthGeneration,
        GpuResourceId attachment,
        std::uint64_t attachmentGeneration) noexcept
    {
        return OriginalFiniteGraphicsContentPlanContract::validateIndependentResources(
            depth, depthGeneration, attachment, attachmentGeneration);
    }

    static constexpr auto lowering(Backend backend, std::uint32_t v = version) noexcept
    {
        return semantics::OriginalCommandStateAdmissionLowering::
            bindlessGraphicsDepthContentPlanLowering(backend, v);
    }
};
} // namespace arp::rhi
