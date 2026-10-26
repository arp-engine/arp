#pragma once

#include "rhi/gpu_commands.h"

namespace arp::rhi::semantics {

// OpaqueDepthGraphics/1 is an independently opted-in subdomain of ordinary
// Preparation6/Recording3. Existing budgets own every resource and visit.
// It grants no optional native feature or public execution capability.
class OpaqueDepthGraphicsContract final {
public:
    static constexpr const char* validateIntent(std::uint32_t version, bool optIn) noexcept
    {
        if (!version && !optIn)
            return nullptr;
        if (version != 1u)
            return "RHI-OPAQUE-DEPTH-GRAPHICS-VERSION";
        return optIn ? nullptr : "RHI-OPAQUE-DEPTH-GRAPHICS-OPT-IN";
    }

    static const char* validateAttachments(
        const RenderPipelineDesc& pipeline, const RenderPassDesc& pass, bool admitted) noexcept
    {
        const auto format = effectiveDepthStencilFormat(pipeline);
        if (format == TextureFormat::Unknown && !pass.hasDepthStencilAttachment)
            return nullptr;
        if (!admitted)
            return "RHI-ORDINARY-GRAPHICS-PIPELINE-PASS-PROFILE";
        const auto& attachment = pass.depthStencilAttachment;
        const auto& depth = pipeline.depthStencil;
        if (format != TextureFormat::D32Float || !pass.hasDepthStencilAttachment
            || attachment.format != format || attachment.depthReadOnly
            || attachment.depthLoadOp != LoadOp::Clear || attachment.depthStoreOp != StoreOp::Store
            || attachment.stencilLoadOp != LoadOp::DontCare
            || attachment.stencilStoreOp != StoreOp::DontCare || !depth.depthTestEnabled
            || !depth.depthWriteEnabled || depth.depthCompare != CompareOp::Less
            || depth.stencilEnabled || depth.depthBiasEnabled
            || depth.depthBiasConstantFactor != 0.f || depth.depthBiasSlopeFactor != 0.f
            || depth.depthBiasClamp != 0.f)
            return "RHI-OPAQUE-DEPTH-GRAPHICS-ATTACHMENT-PROFILE";
        return nullptr;
    }
};

} // namespace arp::rhi::semantics
