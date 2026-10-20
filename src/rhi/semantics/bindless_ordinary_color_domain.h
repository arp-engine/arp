#pragma once
#include "rhi/frontend/completed_color_texture_view.h"
#include "rhi/gpu_bindless.h"

namespace arp::rhi::semantics {
// OrdinaryColorDomain/1 describes scalar read-only ordinary color declarations.
// Original completed upload/control3, whole-view ReadPlan8, exact native
// descriptors, the same Root2 journal and real completion are still mandatory.
// This declaration check neither issues an executable plan nor proves a format.
class OriginalBindlessOrdinaryColorDomain final {
public:
    static bool acceptsStages(ShaderStage stages) noexcept
    {
        const auto supported = ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute;
        return stages != ShaderStage::None && (stages & supported) == stages;
    }

    static bool acceptsBinding(const DescriptorBindingDesc& binding) noexcept
    {
        if (binding.arrayCount != 1u || binding.dynamicOffset || binding.tableBacked
            || !acceptsStages(binding.stages))
            return false;
        if (binding.type == DescriptorType::Sampler)
            return !binding.comparisonSampler;
        return binding.type == DescriptorType::SampledTexture && !binding.depthTexture
            && binding.textureViewDimensionConstrained
            && (binding.textureViewDimension == TextureViewDimension::Texture2D
                || binding.textureViewDimension == TextureViewDimension::Texture2DArray
                || binding.textureViewDimension == TextureViewDimension::TextureCube);
    }

    static bool acceptsGraphicsBinding(const DescriptorBindingDesc& binding) noexcept
    {
        return acceptsBinding(binding)
            && (binding.stages & (ShaderStage::Vertex | ShaderStage::Fragment)) == binding.stages;
    }

    // The ordinary whole-upload profiles are independent of the indexed
    // CompletedImage scopes. This check grants neither publication nor execution;
    // exact Source/publication ownership and the original journal remain required.
    static const char* completedViewDiagnostic(const TextureDesc& texture,
        const TextureViewDesc& view,
        std::uint32_t contentControlVersion,
        std::uint32_t completedReadContractVersion = 0u) noexcept
    {
        const bool canonical = completedReadContractVersion == 3u;
        if ((completedReadContractVersion != 0u && completedReadContractVersion != 2u && !canonical)
            || contentControlVersion != (canonical ? 1u : 3u)
            || (texture.format != TextureFormat::Rgba8Unorm
                && ((completedReadContractVersion != 2u && !canonical)
                    || texture.format != TextureFormat::Rgba16Float))
            || view.format != texture.format || texture.sampleCount != 1u
            || (texture.usage & TextureUsage::Sampled) == TextureUsage::None
            || !texture.extent.width || !texture.extent.height || texture.extent.depth != 1u
            || !texture.mipLevelCount || !texture.arrayLayerCount
            || view.usage != TextureUsage::Sampled || view.aspect != TextureAspect::Color
            || view.baseMipLevel || view.baseArrayLayer
            || view.mipLevelCount != texture.mipLevelCount
            || view.arrayLayerCount != texture.arrayLayerCount)
            return "RHI-BINDLESS-ORDINARY-COLOR-COMPLETED-VIEW";
        const bool dimensional = view.dimension == TextureViewDimension::Texture2D
            ? (!texture.cubeCompatible && texture.arrayLayerCount == 1u)
            : view.dimension == TextureViewDimension::Texture2DArray
            ? !texture.cubeCompatible
            : OriginalCompletedColorTextureViewContract::isSingleCube(texture, view);
        return dimensional ? nullptr : "RHI-BINDLESS-ORDINARY-COLOR-COMPLETED-DIMENSION";
    }

    struct Decision {
        bool inspectionAvailable = false, validatorOnly = false, nativeClosureAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORDINARY-COLOR-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-ORDINARY-COLOR-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
            return {true, false, true, nullptr};
        case Backend::Metal:
            return {true, false, true, nullptr};
        case Backend::WebGPU:
            return {true, false, false, "RHI-WEBGPU-BINDLESS-ORDINARY-COLOR-CLOSURE-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ORDINARY-COLOR-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
