#pragma once

#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_resource_types.h"

namespace arp::rhi::semantics {

class OriginalOrdinarySampledColorContract final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;

        constexpr bool empty() const noexcept { return !contractVersion && !prototypeOptIn; }
    };

    struct Decision {
        bool validatorOnly = false, nativeAvailable = false;
        const char* diagnostic = "RHI-ORDINARY-SAMPLED-COLOR-BACKEND";
    };

    static constexpr const char* validate(Intent intent) noexcept
    {
        if (!intent.contractVersion && !intent.prototypeOptIn)
            return nullptr;
        if (intent.contractVersion != 1u)
            return "RHI-ORDINARY-SAMPLED-COLOR-VERSION";
        return intent.prototypeOptIn ? nullptr : "RHI-ORDINARY-SAMPLED-COLOR-OPT-IN";
    }

    static constexpr Decision lowering(Backend backend, Intent intent) noexcept
    {
        if (const auto* id = validate(intent))
            return {false, false, id};
        if (intent.contractVersion != 1u)
            return {false, false, "RHI-ORDINARY-SAMPLED-COLOR-OPT-IN"};
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-ORDINARY-SAMPLED-COLOR-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, true, nullptr};
        case Backend::Metal:
            return {false, false, "RHI-ORDINARY-SAMPLED-COLOR-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {false, false, "RHI-ORDINARY-SAMPLED-COLOR-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {false, false, "RHI-ORDINARY-SAMPLED-COLOR-WEBGL-UNSUPPORTED"};
        }
        return {};
    }

    static const char* validateView(
        const TextureDesc& texture, const TextureViewDesc& view) noexcept
    {
        if ((texture.format != TextureFormat::Rgba8Unorm
                && texture.format != TextureFormat::Rgba8UnormSrgb
                && texture.format != TextureFormat::Rgba16Float)
            || view.format != texture.format || !texture.extent.width || !texture.extent.height
            || texture.extent.depth != 1u || texture.arrayLayerCount != 1u
            || texture.mipLevelCount != 1u || texture.sampleCount != 1u || texture.cubeCompatible
            || (texture.usage & TextureUsage::Sampled) == TextureUsage::None
            || view.dimension != TextureViewDimension::Texture2D
            || view.usage != TextureUsage::Sampled || view.aspect != TextureAspect::Color
            || view.baseMipLevel || view.mipLevelCount != 1u || view.baseArrayLayer
            || view.arrayLayerCount != 1u)
            return "RHI-ORDINARY-SAMPLED-COLOR-VIEW";
        return nullptr;
    }
};

} // namespace arp::rhi::semantics
