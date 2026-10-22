#pragma once

#include "core/container/array_view.h"
#include "rhi/frontend/resource_state_validation.h"
#include "rhi/frontend/transfer_validation.h"
#include "rhi/semantics/client_texture_upload.h"
#include "rhi/semantics/webgl_extension_execution.h"
#include "rhi/semantics/webgl_graphics_request.h"

namespace arp::rhi::semantics {

struct WebGlResourceOwnershipRequest {
    std::uint32_t contractVersion = 0;
    WebGlMemoryBudget budget;
    WebGlExtensionExecutionRequest extensions;
};

class WebGlResourceCreationContract final {
public:
    static const char* ownership(Backend backend, const WebGlResourceOwnershipRequest& r) noexcept
    {
        if (r.contractVersion != 1)
            return "RHI-WEBGL1-RESOURCE-OWNERSHIP-VERSION";
        if (r.budget.contractVersion != 1)
            return "RHI-WEBGL1-MEMORY-BUDGET-VERSION";
        if (!r.budget.capacity.cpuMetadataBytes || !r.budget.capacity.cpuUploadBytes
            || !r.budget.capacity.objectCount || r.budget.capacity.objectCount > UINT32_MAX)
            return "RHI-WEBGL1-RESOURCE-OWNERSHIP-BUDGET";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-RESOURCE-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-RESOURCE-BACKEND-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-RESOURCE-BACKEND-UNKNOWN";
    }

    static const char* buffer(const BufferDesc& d) noexcept
    {
        if (!d.sizeBytes || d.sizeBytes > std::uint64_t{INT32_MAX})
            return "RHI-WEBGL1-BUFFER-SIZE";
        if (d.memory != MemoryUsage::GpuOnly && d.memory != MemoryUsage::CpuToGpu)
            return "RHI-WEBGL1-RESOURCE-MEMORY-INTENT";
        // WebGL1 fixes a buffer's native target. A uniform is an explicitly
        // shadowed CPU byte source; it is never a fabricated native UBO.
        const auto raw = static_cast<std::uint32_t>(d.usage);
        const auto role = raw & ~static_cast<std::uint32_t>(ResourceUsage::CopyDst);
        if (role != static_cast<std::uint32_t>(ResourceUsage::VertexBuffer)
            && role != static_cast<std::uint32_t>(ResourceUsage::IndexBuffer)
            && role != static_cast<std::uint32_t>(ResourceUsage::UniformBuffer))
            return "RHI-WEBGL1-BUFFER-USAGE";
        if (d.initialState != ResourceState::Undefined && d.initialState != ResourceState::CopyDst
            && d.initialState != ResourceState::ShaderRead)
            return "RHI-WEBGL1-BUFFER-STATE";
        return nullptr;
    }

    static const char* texture(const WebGlRuntimeProfile& p,
        const TextureDesc& d,
        const WebGlExtensionOptIn& optIn = {}) noexcept
    {
        if (!p.valid())
            return p.diagnostic();
        if (d.memory != MemoryUsage::GpuOnly && d.memory != MemoryUsage::CpuToGpu)
            return "RHI-WEBGL1-RESOURCE-MEMORY-INTENT";
        if (d.format == TextureFormat::Depth24Stencil8
            && (d.usage & TextureUsage::Sampled) != TextureUsage::None) {
            if (d.initialState != ResourceState::Undefined
                && d.initialState != ResourceState::DepthAttachment
                && d.initialState != ResourceState::ShaderRead)
                return "RHI-WEBGL1-TEXTURE-STATE";
            const auto admission = WebGlGraphicsRequestContract::texture(p, 1, d, optIn);
            return admission.compatible ? nullptr : admission.diagnostic;
        }
        if (d.format == TextureFormat::Depth24Stencil8) {
            if (d.memory != MemoryUsage::GpuOnly || d.usage != TextureUsage::RenderAttachment
                || !d.extent.width || !d.extent.height || d.extent.depth != 1
                || d.mipLevelCount != 1 || d.arrayLayerCount != 1 || d.sampleCount != 1
                || d.cubeCompatible)
                return "RHI-WEBGL1-PACKED-DEPTH-SHAPE";
            if (d.initialState != ResourceState::Undefined
                && d.initialState != ResourceState::DepthAttachment)
                return "RHI-WEBGL1-TEXTURE-STATE";
            WebGlGraphicsRequirements req;
            req.renderbufferWidth = d.extent.width;
            req.renderbufferHeight = d.extent.height;
            const auto admission = p.admit(req);
            if (!admission.compatible)
                return admission.diagnostic;
            const auto format =
                p.admitFormat({1, d.format, WebGlFormatRole::DepthStencilRenderbuffer, 1});
            return format.compatible ? nullptr : format.diagnostic;
        }
        if (d.initialState != ResourceState::Undefined && d.initialState != ResourceState::CopyDst
            && d.initialState != ResourceState::ShaderRead
            && d.initialState != ResourceState::ColorAttachment)
            return "RHI-WEBGL1-TEXTURE-STATE";
        // /1 adds explicit host-upload CopyDst to the P0 texture roles. It
        // grants no transfer/copy command, readback or storage-image support.
        const auto usage = static_cast<std::uint32_t>(d.usage);
        constexpr auto roles = static_cast<std::uint32_t>(TextureUsage::Sampled)
            | static_cast<std::uint32_t>(TextureUsage::RenderAttachment);
        if (!(usage & roles)
            || (usage & ~(roles | static_cast<std::uint32_t>(TextureUsage::CopyDst))))
            return "RHI-WEBGL1-TEXTURE-USAGE";
        TextureDesc shape;
        shape.extent = d.extent;
        shape.format = d.format;
        shape.usage = static_cast<TextureUsage>(usage & roles);
        shape.mipLevelCount = d.mipLevelCount;
        shape.arrayLayerCount = d.arrayLayerCount;
        shape.sampleCount = d.sampleCount;
        shape.cubeCompatible = d.cubeCompatible;
        const auto a = WebGlGraphicsRequestContract::texture(p, 1, shape);
        return a.compatible ? nullptr : a.diagnostic;
    }

    static const char* upload(const TextureDesc& d, const WebGlColdTextureUpload& u) noexcept
    {
        if (u.contractVersion != 1)
            return "RHI-WEBGL1-TEXTURE-UPLOAD-VERSION";
        if (d.format != TextureFormat::Rgba8Unorm
            || (d.usage & TextureUsage::CopyDst) == TextureUsage::None)
            return "RHI-WEBGL1-TEXTURE-UPLOAD-USAGE";
        const TextureSubresourceRange range{u.mipLevel, 1, 0, 1, TextureAspect::Color};
        if (!TextureTransferValidation::textureUploadRegionValid(d, range, u.extent, u.origin))
            return "RHI-WEBGL1-TEXTURE-UPLOAD-REGION";
        const auto row = TextureTransferValidation::copyRowBytes(d.format, u.extent.width);
        if (u.bytesPerRow < row || u.bytesPerRow % bytesPerPixel(d.format) || !u.bytes.data()
            || std::uint64_t{u.bytesPerRow} * u.extent.height > u.bytes.size())
            return "RHI-WEBGL1-TEXTURE-UPLOAD-ROWS";
        return nullptr;
    }

    static const char* view(const TextureDesc& d, const TextureViewDesc& v) noexcept
    {
        if (v.format != d.format || v.dimension != TextureViewDimension::Texture2D || v.baseMipLevel
            || v.mipLevelCount != d.mipLevelCount || v.baseArrayLayer || v.arrayLayerCount != 1
            || v.usage == TextureUsage::None || (v.usage & d.usage) != v.usage
            || v.aspect
                != (isDepthTextureFormat(d.format) ? TextureAspect::Depth : TextureAspect::Color))
            return "RHI-WEBGL1-TEXTURE-VIEW-NOT-EQUIVALENT";
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
