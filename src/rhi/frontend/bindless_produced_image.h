#pragma once
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/resource_texture_validation.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/bindless_closure_extensions.h"

namespace arp::rhi {
// ProducedImageRead/1 composes a previously completed RGBA16F mip chain with
// Content9/Geometry2. Range identities authenticate descriptors only. The
// original transaction must inspect every completed cell and retain its stamp
// in the same state journal. No writer or completion authority is introduced.
class OriginalBindlessProducedImageReadContract final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxControls = 0u, maxCells = 0u, maxSteps = 0u;

        constexpr bool empty() const noexcept
        {
            return !contractVersion && !optIn && !maxControls && !maxCells && !maxSteps;
        }
    };

    struct Decision {
        bool preparationAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-PRODUCED-IMAGE-BACKEND";
    };

    static constexpr const char* validateIntent(const Intent& intent) noexcept
    {
        if (intent.empty())
            return nullptr;
        if (intent.contractVersion != 1u)
            return "RHI-BINDLESS-PRODUCED-IMAGE-VERSION";
        if (!intent.optIn)
            return "RHI-BINDLESS-PRODUCED-IMAGE-OPT-IN";
        return intent.maxControls && intent.maxCells && intent.maxSteps
                && intent.maxControls <= intent.maxCells && intent.maxCells <= SIZE_MAX
            ? nullptr
            : "RHI-BINDLESS-PRODUCED-IMAGE-BUDGET";
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        return semantics::BindlessClosureExtensionLowering::produced_image<Decision>(
            backend, version);
    }

    static constexpr bool acceptsControl(std::uint32_t version) noexcept
    {
        return version == 6u || version == 7u;
    }

    static const char* inspectView(const TextureDesc& texture, const TextureViewDesc& view) noexcept
    {
        if (texture.format != TextureFormat::Rgba16Float || view.format != texture.format
            || !texture.extent.width || !texture.extent.height || texture.extent.depth != 1u
            || texture.sampleCount != 1u || texture.arrayLayerCount != 1u || !texture.mipLevelCount
            || texture.cubeCompatible
            || (texture.usage & (TextureUsage::Sampled | TextureUsage::Storage))
                != (TextureUsage::Sampled | TextureUsage::Storage)
            || view.usage != TextureUsage::Sampled
            || view.dimension != TextureViewDimension::Texture2D
            || view.aspect != TextureAspect::Color || view.baseMipLevel
            || view.mipLevelCount != texture.mipLevelCount || view.baseArrayLayer
            || view.arrayLayerCount != 1u)
            return "RHI-BINDLESS-PRODUCED-IMAGE-VIEW";
        return nullptr;
    }

    static constexpr const char* inspectAccess(ResourceState state,
        PipelineStage stages,
        ResourceAccess access,
        CommandUsageKind kind) noexcept
    {
        return state == ResourceState::ShaderRead && stages == PipelineStage::ComputeShader
                && access == ResourceAccess::ShaderRead && kind == CommandUsageKind::Dispatch
            ? nullptr
            : "RHI-BINDLESS-PRODUCED-IMAGE-ACCESS";
    }
};
} // namespace arp::rhi
