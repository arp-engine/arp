#pragma once

#include "rhi/gpu_texture_format.h"

#include <cstdint>
#include <initializer_list>

namespace arp::rhi::semantics {

enum class WebGlFormatQueryOutcome : std::uint8_t {
    NotQueried,
    Observed,
    NativeFailure,
    OutOfMemory,
    ContextLost
};

struct WebGlFormatFacts {
    std::uint32_t contractVersion = 0;
    WebGlFormatQueryOutcome rgba8Texture = WebGlFormatQueryOutcome::NotQueried;
    bool rgba8ClientUploadAccepted = false;
    WebGlFormatQueryOutcome rgba8Framebuffer = WebGlFormatQueryOutcome::NotQueried;
    bool rgba8FramebufferComplete = false;
    WebGlFormatQueryOutcome packedDepthStencil = WebGlFormatQueryOutcome::NotQueried;
    std::int32_t packedDepthBits = 0;
    std::int32_t packedStencilBits = 0;
    bool packedFramebufferComplete = false;
    WebGlFormatQueryOutcome depthTexture = WebGlFormatQueryOutcome::NotQueried;
    bool depthTextureFramebufferComplete = false;
    WebGlFormatQueryOutcome srgbTexture = WebGlFormatQueryOutcome::NotQueried;
    bool srgbClientUploadAccepted = false;
};

enum class WebGlFormatRole : std::uint8_t {
    Texture2DClientUpload,
    Texture2DSampled,
    Texture2DColorAttachment,
    DepthStencilRenderbuffer,
    DepthStencilTexture
};

struct WebGlFormatRequest {
    std::uint32_t contractVersion = 0;
    TextureFormat format = TextureFormat::Unknown;
    WebGlFormatRole role = WebGlFormatRole::Texture2DClientUpload;
    std::uint32_t sampleCount = 0;
};

class WebGlFormatContract final {
public:
    static const char* validate(const WebGlFormatFacts& facts) noexcept
    {
        if (facts.contractVersion != 1u && facts.contractVersion != 2u)
            return "RHI-WEBGL1-FORMAT-FACTS-VERSION";
        bool missing = false;
        const char* failure = nullptr;
        // Reject unknown values even when a different query is missing.
        for (auto outcome :
            {facts.rgba8Texture, facts.rgba8Framebuffer, facts.packedDepthStencil}) {
            switch (outcome) {
            case WebGlFormatQueryOutcome::NotQueried:
                missing = true;
                break;
            case WebGlFormatQueryOutcome::Observed:
                break;
            case WebGlFormatQueryOutcome::NativeFailure:
                if (!failure)
                    failure = "RHI-WEBGL1-FORMAT-NATIVE-FAILURE";
                break;
            case WebGlFormatQueryOutcome::OutOfMemory:
                if (!failure)
                    failure = "RHI-WEBGL1-FORMAT-OUT-OF-MEMORY";
                break;
            case WebGlFormatQueryOutcome::ContextLost:
                if (!failure)
                    failure = "RHI-WEBGL1-FORMAT-CONTEXT-LOST";
                break;
            default:
                return "RHI-WEBGL1-FORMAT-PROBE-INVALID";
            }
        }
        switch (facts.depthTexture) {
        case WebGlFormatQueryOutcome::NotQueried:
        case WebGlFormatQueryOutcome::Observed:
        case WebGlFormatQueryOutcome::NativeFailure:
        case WebGlFormatQueryOutcome::OutOfMemory:
        case WebGlFormatQueryOutcome::ContextLost:
            break;
        default:
            return "RHI-WEBGL1-FORMAT-PROBE-INVALID";
        }
        if (facts.depthTexture == WebGlFormatQueryOutcome::NotQueried
            && facts.depthTextureFramebufferComplete)
            return "RHI-WEBGL1-FORMAT-PROBE-INVALID";
        switch (facts.srgbTexture) {
        case WebGlFormatQueryOutcome::NotQueried:
        case WebGlFormatQueryOutcome::Observed:
        case WebGlFormatQueryOutcome::NativeFailure:
        case WebGlFormatQueryOutcome::OutOfMemory:
        case WebGlFormatQueryOutcome::ContextLost:
            break;
        default:
            return "RHI-WEBGL1-FORMAT-PROBE-INVALID";
        }
        if ((facts.srgbClientUploadAccepted
                && facts.srgbTexture != WebGlFormatQueryOutcome::Observed)
            || (facts.contractVersion == 1u
                && (facts.srgbClientUploadAccepted
                    || facts.srgbTexture != WebGlFormatQueryOutcome::NotQueried)))
            return "RHI-WEBGL1-FORMAT-PROBE-INVALID";
        if (failure)
            return failure;
        if (missing)
            return "RHI-WEBGL1-FORMAT-PROBE-INCOMPLETE";
        if (facts.packedDepthBits < 0 || facts.packedStencilBits < 0
            || (!facts.rgba8ClientUploadAccepted
                && (facts.rgba8FramebufferComplete || facts.packedFramebufferComplete)))
            return "RHI-WEBGL1-FORMAT-PROBE-INVALID";
        return nullptr;
    }

    static const char* assess(
        const WebGlFormatFacts& facts, const WebGlFormatRequest& request) noexcept
    {
        if (const auto* diagnostic = validate(facts))
            return diagnostic;
        if (request.contractVersion != 1u)
            return "RHI-WEBGL1-FORMAT-REQUEST-VERSION";
        switch (request.role) {
        case WebGlFormatRole::Texture2DClientUpload:
        case WebGlFormatRole::Texture2DSampled:
        case WebGlFormatRole::Texture2DColorAttachment:
        case WebGlFormatRole::DepthStencilRenderbuffer:
        case WebGlFormatRole::DepthStencilTexture:
            break;
        default:
            return "RHI-WEBGL1-FORMAT-ROLE";
        }
        if (request.sampleCount != 1u)
            return "RHI-WEBGL1-FORMAT-SAMPLE-COUNT";
        if (request.format == TextureFormat::Rgba8Unorm
            && request.role != WebGlFormatRole::DepthStencilRenderbuffer
            && request.role != WebGlFormatRole::DepthStencilTexture
            && facts.rgba8ClientUploadAccepted
            && (request.role != WebGlFormatRole::Texture2DColorAttachment
                || facts.rgba8FramebufferComplete))
            return nullptr;
        if (request.format == TextureFormat::Rgba8UnormSrgb && facts.contractVersion == 2u
            && facts.srgbTexture == WebGlFormatQueryOutcome::Observed
            && facts.srgbClientUploadAccepted
            && (request.role == WebGlFormatRole::Texture2DClientUpload
                || request.role == WebGlFormatRole::Texture2DSampled))
            return nullptr;
        if (request.format == TextureFormat::Depth24Stencil8
            && request.role == WebGlFormatRole::DepthStencilRenderbuffer
            && facts.packedDepthBits == 24 && facts.packedStencilBits == 8
            && facts.packedFramebufferComplete)
            return nullptr;
        if (request.format == TextureFormat::Depth24Stencil8
            && request.role == WebGlFormatRole::DepthStencilTexture
            && facts.depthTexture == WebGlFormatQueryOutcome::Observed
            && facts.depthTextureFramebufferComplete)
            return nullptr;
        return "RHI-WEBGL1-FORMAT-UNSUPPORTED";
    }
};

} // namespace arp::rhi::semantics
