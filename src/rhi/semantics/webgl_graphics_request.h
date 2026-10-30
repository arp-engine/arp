#pragma once

#include "core/container/array_view.h"
#include "core/math/unsigned_range.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/webgl_runtime_profile.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::rhi::semantics {

// Contract planning only. These requests never certify native resource identity,
// initialization, shader ABI, completion or permission to issue commands.
enum class WebGlGraphicsOperation {
    Draw,
    DrawIndexed,
    Uniform,
    Texture2D,
    ColorAttachment,
    Canvas,
    Uint32Indices,
    Instancing,
    VertexArrayObjects,
    DepthTexture,
    Derivatives,
    Compute,
    StorageBuffer,
    StorageImage,
    Indirect,
    Bindless,
    AliasHeap,
    GpuCompletion,
    Readback,
    MultipleColorAttachments,
    Multisample
};

struct WebGlSampledBindingRequest {
    GpuResourceId textureIdentity;
    const TextureDesc* texture = nullptr;
    const SamplerDesc* sampler = nullptr;
};

struct WebGlDrawRequest {
    std::uint32_t contractVersion = 0u;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    bool indexed = false;
    IndexFormat indexFormat = IndexFormat::Uint16;
    std::uint32_t count = 0u, instanceCount = 0u, firstVertex = 0u, firstInstance = 0u;
    std::int32_t baseVertex = 0;
    std::uint64_t indexBufferSize = 0u, indexOffset = 0u;
    bool primitiveRestart = false;
};

struct WebGlCanvasRequest {
    std::uint32_t contractVersion = 0u;
    PresentationMode mode = PresentationMode::Auto;
    // Zero explicitly requests no controllable native swapchain image count.
    std::uint32_t nativeImageCount = 0u;
    bool loadPreviousDrawingBuffer = false;
};

class WebGlGraphicsRequestContract final {
public:
    static WebGlProfileAdmission operation(const WebGlRuntimeProfile& profile,
        std::uint32_t version,
        WebGlGraphicsOperation operation,
        const WebGlExtensionOptIn& optIn = {}) noexcept
    {
        if (const auto* id = validate(profile, version))
            return reject(id);
        WebGlGraphicsRequirements requirements;
        switch (operation) {
        case WebGlGraphicsOperation::Draw:
        case WebGlGraphicsOperation::DrawIndexed:
        case WebGlGraphicsOperation::Uniform:
        case WebGlGraphicsOperation::Texture2D:
        case WebGlGraphicsOperation::ColorAttachment:
        case WebGlGraphicsOperation::Canvas:
            break;
        case WebGlGraphicsOperation::Uint32Indices:
            requirements.uint32Indices = true;
            break;
        case WebGlGraphicsOperation::Instancing:
            requirements.instancing = true;
            break;
        case WebGlGraphicsOperation::VertexArrayObjects:
            requirements.vertexArrayObjects = true;
            break;
        case WebGlGraphicsOperation::DepthTexture:
            requirements.depthTextures = true;
            break;
        case WebGlGraphicsOperation::Derivatives:
            requirements.derivatives = true;
            break;
        case WebGlGraphicsOperation::Compute:
        case WebGlGraphicsOperation::StorageBuffer:
        case WebGlGraphicsOperation::StorageImage:
        case WebGlGraphicsOperation::Indirect:
        case WebGlGraphicsOperation::Bindless:
        case WebGlGraphicsOperation::AliasHeap:
        case WebGlGraphicsOperation::Readback:
        case WebGlGraphicsOperation::MultipleColorAttachments:
        case WebGlGraphicsOperation::Multisample:
            return reject("RHI-WEBGL1-GRAPHICS-FEATURE-UNSUPPORTED");
        case WebGlGraphicsOperation::GpuCompletion:
            return reject("RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE");
        default:
            return reject("RHI-WEBGL1-GRAPHICS-OPERATION-UNKNOWN");
        }
        return profile.admit(requirements, optIn);
    }

    static WebGlProfileAdmission texture(const WebGlRuntimeProfile& profile,
        std::uint32_t version,
        const TextureDesc& desc,
        const WebGlExtensionOptIn& optIn = {}) noexcept
    {
        if (const auto* id = validate(profile, version))
            return reject(id);
        if (!desc.extent.width || !desc.extent.height || desc.extent.depth != 1u
            || desc.arrayLayerCount != 1u || desc.sampleCount != 1u || desc.cubeCompatible
            || !desc.mipLevelCount)
            return reject("RHI-WEBGL1-TEXTURE-SHAPE");
        constexpr auto allowed = static_cast<std::uint32_t>(TextureUsage::Sampled)
            | static_cast<std::uint32_t>(TextureUsage::RenderAttachment);
        const auto usage = static_cast<std::uint32_t>(desc.usage);
        if (!usage || (usage & ~allowed))
            return reject("RHI-WEBGL1-TEXTURE-USAGE");
        if (desc.mipLevelCount
            > core::unsignedBitWidth(std::max(desc.extent.width, desc.extent.height)))
            return reject("RHI-WEBGL1-TEXTURE-MIP-CHAIN");
        WebGlGraphicsRequirements requirements;
        requirements.textureWidth = desc.extent.width;
        requirements.textureHeight = desc.extent.height;
        const auto dimensions = profile.admit(requirements);
        if (!dimensions.compatible)
            return dimensions;
        if (desc.format == TextureFormat::Depth24Stencil8) {
            if (desc.usage != (TextureUsage::Sampled | TextureUsage::RenderAttachment)
                || desc.mipLevelCount != 1 || desc.memory != MemoryUsage::GpuOnly)
                return reject("RHI-WEBGL1-DEPTH-TEXTURE-SHAPE");
            const auto extension =
                operation(profile, version, WebGlGraphicsOperation::DepthTexture, optIn);
            if (!extension.compatible)
                return extension;
            return profile.admitFormat({1, desc.format, WebGlFormatRole::DepthStencilTexture, 1});
        }
        if (desc.format == TextureFormat::Rgba8UnormSrgb) {
            WebGlGraphicsRequirements required;
            required.srgbTextures = true;
            const auto extension = profile.admit(required, optIn);
            if (!extension.compatible)
                return extension;
        }
        // Each requested role consumes its own queried format facts.
        for (const auto role :
            {WebGlFormatRole::Texture2DSampled, WebGlFormatRole::Texture2DColorAttachment}) {
            const auto bit = role == WebGlFormatRole::Texture2DSampled
                ? TextureUsage::Sampled
                : TextureUsage::RenderAttachment;
            if ((usage & static_cast<std::uint32_t>(bit)) != 0u) {
                const auto format = profile.admitFormat({1u, desc.format, role, desc.sampleCount});
                if (!format.compatible)
                    return format;
            }
        }
        return dimensions;
    }

    static WebGlProfileAdmission sampledBindings(const WebGlRuntimeProfile& profile,
        std::uint32_t version,
        core::ArrayView<WebGlSampledBindingRequest> bindings,
        const WebGlExtensionOptIn& optIn = {}) noexcept
    {
        if (const auto* id = validate(profile, version))
            return reject(id);
        // Bound the work before traversing or comparing any caller storage.
        if (bindings.size() > profile.limits().maxCombinedTextureUnits)
            return reject("RHI-WEBGL1-WORKLOAD-LIMIT");
        if (!bindings.empty() && !bindings.data())
            return reject("RHI-WEBGL1-SAMPLED-BINDING-MISSING");
        for (std::size_t i = 0u; i < bindings.size(); ++i) {
            const auto& binding = bindings[i];
            if (!binding.textureIdentity.isValid() || !binding.texture || !binding.sampler)
                return reject("RHI-WEBGL1-SAMPLED-BINDING-MISSING");
            const auto shape = texture(profile, version, *binding.texture, optIn);
            if (!shape.compatible)
                return shape;
            if (!(static_cast<std::uint32_t>(binding.texture->usage)
                    & static_cast<std::uint32_t>(TextureUsage::Sampled)))
                return reject("RHI-WEBGL1-TEXTURE-USAGE");
            if (const auto* id = sampler(*binding.texture, *binding.sampler))
                return reject(id);
            for (std::size_t j = 0u; j < i; ++j) {
                if (bindings[j].textureIdentity == binding.textureIdentity) {
                    const auto& previous = *bindings[j].texture;
                    const auto& current = *binding.texture;
                    if (previous.extent != current.extent || previous.format != current.format
                        || previous.usage != current.usage
                        || previous.mipLevelCount != current.mipLevelCount)
                        return reject("RHI-WEBGL1-SAMPLED-BINDING-IDENTITY");
                    if (!sameSampler(*bindings[j].sampler, *binding.sampler))
                        return reject("RHI-WEBGL1-SAMPLER-CONFLICT");
                }
            }
        }
        return profile.admit({});
    }

    static WebGlProfileAdmission draw(const WebGlRuntimeProfile& profile,
        const WebGlDrawRequest& request,
        const WebGlExtensionOptIn& optIn = {}) noexcept
    {
        if (const auto* id = validate(profile, request.contractVersion))
            return reject(id);
        constexpr auto signedLimit = std::uint32_t{std::numeric_limits<std::int32_t>::max()};
        if (!request.count || !request.instanceCount || request.count > signedLimit
            || request.instanceCount > signedLimit || request.firstVertex > signedLimit)
            return reject("RHI-WEBGL1-DRAW-RANGE");
        if (request.topology != PrimitiveTopology::TriangleList || request.baseVertex
            || request.firstInstance || request.primitiveRestart)
            return reject("RHI-WEBGL1-DRAW-SEMANTICS");
        WebGlGraphicsRequirements requirements;
        requirements.instancing = request.instanceCount > 1u;
        if (request.indexed) {
            std::uint64_t indexBytes = 0u;
            switch (request.indexFormat) {
            case IndexFormat::Uint16:
                indexBytes = sizeof(std::uint16_t);
                break;
            case IndexFormat::Uint32:
                indexBytes = sizeof(std::uint32_t);
                requirements.uint32Indices = true;
                break;
            default:
                return reject("RHI-WEBGL1-DRAW-INDEX-FORMAT");
            }
            if (request.firstVertex || request.indexOffset % indexBytes
                || !core::containsUnsignedRange(std::uint64_t{0u},
                    request.indexBufferSize,
                    request.indexOffset,
                    std::uint64_t{request.count} * indexBytes))
                return reject("RHI-WEBGL1-DRAW-RANGE");
        } else if (request.indexBufferSize || request.indexOffset
            || request.indexFormat != IndexFormat::Uint16) {
            return reject("RHI-WEBGL1-DRAW-SEMANTICS");
        }
        return profile.admit(requirements, optIn);
    }

    static WebGlProfileAdmission canvas(
        const WebGlRuntimeProfile& profile, const WebGlCanvasRequest& request) noexcept
    {
        if (const auto* id = validate(profile, request.contractVersion))
            return reject(id);
        if (request.mode != PresentationMode::Auto || request.nativeImageCount)
            return reject("RHI-WEBGL1-PRESENTATION-POLICY");
        if (request.loadPreviousDrawingBuffer)
            return reject("RHI-WEBGL1-CANVAS-CONTENT-UNAVAILABLE");
        return profile.admit({});
    }

private:
    static const char* validate(const WebGlRuntimeProfile& profile, std::uint32_t version) noexcept
    {
        return !profile.valid() ? profile.diagnostic()
            : version != 1u     ? "RHI-WEBGL1-GRAPHICS-REQUEST-VERSION"
                                : nullptr;
    }

    static WebGlProfileAdmission reject(const char* id) noexcept { return {false, false, id}; }

public:
    // Descriptor-only facts; texture-dependent completeness remains binding-time.
    static const char* samplerState(const SamplerDesc& sampler) noexcept
    {
        if ((sampler.minFilter != SamplerFilter::Nearest
                && sampler.minFilter != SamplerFilter::Linear)
            || (sampler.magFilter != SamplerFilter::Nearest
                && sampler.magFilter != SamplerFilter::Linear)
            || (sampler.addressU != SamplerAddressMode::ClampToEdge
                && sampler.addressU != SamplerAddressMode::Repeat)
            || (sampler.addressV != SamplerAddressMode::ClampToEdge
                && sampler.addressV != SamplerAddressMode::Repeat)
            || sampler.addressW != SamplerAddressMode::ClampToEdge
            || (sampler.mipFilter != SamplerMipFilter::None
                && sampler.mipFilter != SamplerMipFilter::Nearest
                && sampler.mipFilter != SamplerMipFilter::Linear)
            || sampler.compareOp != CompareOp::Never || !std::isfinite(sampler.maxAnisotropy)
            || sampler.maxAnisotropy < 0.0f || sampler.maxAnisotropy > 1.0f)
            return "RHI-WEBGL1-SAMPLER-UNSUPPORTED";
        return nullptr;
    }

private:
    static const char* sampler(const TextureDesc& desc, const SamplerDesc& sampler) noexcept
    {
        if (const auto* id = samplerState(sampler))
            return id;
        const bool npot = !core::isPowerOfTwoUnsigned(desc.extent.width)
            || !core::isPowerOfTwoUnsigned(desc.extent.height);
        if (npot
            && (sampler.addressU != SamplerAddressMode::ClampToEdge
                || sampler.addressV != SamplerAddressMode::ClampToEdge
                || sampler.mipFilter != SamplerMipFilter::None || desc.mipLevelCount != 1u))
            return "RHI-WEBGL1-INVALID-NPOT";
        if (sampler.mipFilter != SamplerMipFilter::None
            && desc.mipLevelCount
                != core::unsignedBitWidth(std::max(desc.extent.width, desc.extent.height)))
            return "RHI-WEBGL1-TEXTURE-MIP-CHAIN";
        return nullptr;
    }

    static bool sameSampler(const SamplerDesc& a, const SamplerDesc& b) noexcept
    {
        return a.minFilter == b.minFilter && a.magFilter == b.magFilter && a.addressU == b.addressU
            && a.addressV == b.addressV && a.addressW == b.addressW && a.compareOp == b.compareOp
            && a.mipFilter == b.mipFilter && a.maxAnisotropy == b.maxAnisotropy;
    }
};

} // namespace arp::rhi::semantics
