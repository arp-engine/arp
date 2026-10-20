#pragma once

#include "rhi/gpu_commands.h"

#include <cmath>
#include <limits>

namespace arp::rhi::semantics {
// Closed L2 enums; an unknown value never chooses a native default action.
inline constexpr bool validAttachmentLoadOp(LoadOp op) noexcept
{
    return op == LoadOp::Load || op == LoadOp::Clear || op == LoadOp::DontCare;
}

inline constexpr bool validAttachmentStoreOp(StoreOp op) noexcept
{
    return op == StoreOp::Store || op == StoreOp::DontCare;
}

class DepthStencilAttachmentInputContract final {
public:
    static constexpr std::uint32_t version = 1u;

    static const char* diagnostic(const RenderPassDepthStencilAttachmentDesc& attachment) noexcept
    {
        if (!validAttachmentLoadOp(attachment.depthLoadOp)
            || !validAttachmentStoreOp(attachment.depthStoreOp)
            || !validAttachmentLoadOp(attachment.stencilLoadOp)
            || !validAttachmentStoreOp(attachment.stencilStoreOp))
            return "RHI-RENDER-PASS-ATTACHMENT-OP-INVALID";
        if (!std::isfinite(attachment.depthClearValue) || attachment.depthClearValue < 0.0f
            || attachment.depthClearValue > 1.0f)
            return "RHI-RENDER-PASS-DEPTH-CLEAR-INVALID";
        if (attachment.stencilClearValue > std::numeric_limits<std::uint8_t>::max())
            return "RHI-RENDER-PASS-STENCIL-CLEAR-INVALID";
        if (attachment.depthReadOnly || attachment.stencilReadOnly)
            return "RHI-RENDER-PASS-DEPTH-STENCIL-READ-ONLY-UNPROVEN";
        return nullptr;
    }
};

inline ValidationResult validateRenderPassAttachmentOperations(const RenderPassDesc& desc)
{
    for (const auto& attachment : desc.colorAttachments)
        if (!validAttachmentLoadOp(attachment.loadOp)
            || !validAttachmentStoreOp(attachment.storeOp))
            return {false, "RHI-RENDER-PASS-ATTACHMENT-OP-INVALID"};
    if (desc.hasDepthStencilAttachment) {
        const auto& attachment = desc.depthStencilAttachment;
        if (const auto* id = DepthStencilAttachmentInputContract::diagnostic(attachment))
            return {false, id};
    }
    return {true, {}};
}
} // namespace arp::rhi::semantics
