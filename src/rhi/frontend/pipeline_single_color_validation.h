#pragma once
#include "rhi/frontend/command_attachment_validation.h"
#include "rhi/frontend/resource_texture_extent.h"
#include "rhi/semantics/viewport_scissor_validation.h"

#include <cmath>

namespace arp::rhi::semantics {
// SingleColor2DRendering/1. Pure, allocation-free closed L2 compatibility.
class SingleColor2DRendering final {
public:
    static const char* validatePass(const RenderPassDesc& pass) noexcept
    {
        if (pass.colorAttachments.size() != 1u || !pass.colorTargetName.empty()
            || pass.hasDepthStencilAttachment || pass.layeredRendering || !pass.subpasses.empty()
            || !pass.subpassDependencies.empty())
            return "RHI-SINGLE-COLOR-PASS-DOMAIN";
        const auto& color = pass.colorAttachments.front();
        if (!color.target.value || color.hasResolveTarget || color.resolveTarget.value
            || color.format != TextureFormat::Rgba8Unorm)
            return "RHI-SINGLE-COLOR-PASS-TARGET";
        if (!validAttachmentLoadOp(color.loadOp) || !validAttachmentStoreOp(color.storeOp))
            return "RHI-RENDER-PASS-ATTACHMENT-OP-INVALID";
        if (color.loadOp != LoadOp::Clear || color.storeOp != StoreOp::Store)
            return "RHI-SINGLE-COLOR-PASS-OPERATIONS";
        const auto& c = color.clearColor;
        if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b)
            || !std::isfinite(c.a))
            return "RHI-SINGLE-COLOR-PASS-CLEAR";
        return nullptr;
    }

    static const char* resolveTarget(const TextureDesc& parent,
        const TextureViewDesc& view,
        TextureFormat format,
        Extent3D& extent) noexcept
    {
        extent = {0u, 0u, 0u};
        if (format != TextureFormat::Rgba8Unorm || parent.format != format || view.format != format
            || parent.sampleCount != 1u || parent.extent.depth != 1u || parent.cubeCompatible
            || (parent.usage & TextureUsage::RenderAttachment) == TextureUsage::None
            || (view.usage & TextureUsage::RenderAttachment) == TextureUsage::None
            || view.dimension != TextureViewDimension::Texture2D
            || view.aspect != TextureAspect::Color || view.mipLevelCount != 1u
            || view.arrayLayerCount != 1u || !parent.arrayLayerCount
            || view.baseArrayLayer >= parent.arrayLayerCount)
            return "RHI-SINGLE-COLOR-VIEW-COMPATIBILITY";
        return TextureSubresourceExtent::resolve(
            parent.extent, parent.mipLevelCount, view.baseMipLevel, extent);
    }

    template <class Graphics>
    static const char* validatePipeline(
        const Graphics& pipeline, TextureFormat format, Extent3D extent) noexcept
    {
        if (!pipeline.graphics || pipeline.colorFormats.size() != 1u
            || pipeline.colorFormats.front() != format
            || pipeline.depthStencilFormat != TextureFormat::Unknown || pipeline.sampleCount != 1u
            || pipeline.vertexBufferCount || pipeline.topology != PrimitiveTopology::TriangleList
            || !pipeline.hasViewport || !pipeline.hasScissor)
            return "RHI-SINGLE-COLOR-PIPELINE-COMPATIBILITY";
        const auto& v = pipeline.viewport;
        if (const auto* id = viewportCommandDiagnostic(v))
            return id;
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.width)
            || !std::isfinite(v.height) || !std::isfinite(v.minDepth) || !std::isfinite(v.maxDepth)
            || v.x < 0.0f || v.y < 0.0f || v.minDepth < 0.0f || v.maxDepth > 1.0f
            || double(v.x) + double(v.width) > double(extent.width)
            || double(v.y) + double(v.height) > double(extent.height))
            return "RHI-SINGLE-COLOR-VIEWPORT-RANGE";
        const auto& s = pipeline.scissor;
        if (const auto* id = scissorCommandDiagnostic(s))
            return id;
        if (s.x < 0 || s.y < 0 || std::uint64_t(s.x) + s.width > extent.width
            || std::uint64_t(s.y) + s.height > extent.height)
            return "RHI-SINGLE-COLOR-SCISSOR-RANGE";
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
