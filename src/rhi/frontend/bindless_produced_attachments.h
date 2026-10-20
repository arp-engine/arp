#pragma once
#include "rhi/gpu_resources.h"

namespace arp::rhi {
// Independent cold attachment extension for WebGPU produced streams and
// explicitly admitted SceneContent/1 direct graphics.
// Native fence paths retain attachments through their original Pass/2 objects.
// This intent changes neither their closure profile nor execution authority.
class OriginalBindlessProducedAttachmentContract final {
public:
    struct Intent {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;

        constexpr bool empty() const noexcept { return !contractVersion && !optIn; }
    };

    struct Decision {
        bool preparationAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-PRODUCED-ATTACHMENT-BACKEND";
    };

    static constexpr const char* validateIntent(const Intent& intent) noexcept
    {
        if (intent.empty())
            return nullptr;
        if (intent.contractVersion != 1u)
            return "RHI-BINDLESS-PRODUCED-ATTACHMENT-VERSION";
        return intent.optIn ? nullptr : "RHI-BINDLESS-PRODUCED-ATTACHMENT-OPT-IN";
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-PRODUCED-ATTACHMENT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, true, false, "RHI-BINDLESS-PRODUCED-ATTACHMENT-NULL-VALIDATOR"};
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
            return {false, false, false, "RHI-BINDLESS-PRODUCED-ATTACHMENT-NATIVE-PASS-REQUIRED"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-PRODUCED-ATTACHMENT-UNSUPPORTED"};
        }
        return {};
    }

    static constexpr bool acceptsContentControl(std::uint32_t version) noexcept
    {
        return version == 5u;
    }

    static const char* inspectView(const TextureDesc& texture, const TextureViewDesc& view) noexcept
    {
        const bool depth = texture.format == TextureFormat::D32Float;
        if ((!depth && texture.format != TextureFormat::Rgba8Unorm
                && texture.format != TextureFormat::Rgba16Float)
            || texture.format != view.format || texture.sampleCount != 1u || !texture.extent.width
            || !texture.extent.height || texture.extent.depth != 1u || texture.cubeCompatible
            || texture.arrayLayerCount != 1u || texture.mipLevelCount != 1u
            || view.usage != TextureUsage::RenderAttachment
            || (texture.usage & TextureUsage::RenderAttachment) == TextureUsage::None
            || view.dimension != TextureViewDimension::Texture2D || view.baseMipLevel
            || view.baseArrayLayer || view.mipLevelCount != 1u || view.arrayLayerCount != 1u
            || view.aspect != (depth ? TextureAspect::Depth : TextureAspect::Color))
            return "RHI-BINDLESS-PRODUCED-ATTACHMENT-VIEW";
        return nullptr;
    }
};
} // namespace arp::rhi
