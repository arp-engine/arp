#pragma once

#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/command_attachment_validation.h"

namespace arp::rhi_webgpu {
// DepthStencilAttachmentInput/1. Native-only mapping consumes complete L2
// values and the resolved format; it supplies no recording/ownership authority.
class WebGpuAttachmentOperationLowering final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static WGPULoadOp load(rhi::LoadOp op) noexcept
    {
        switch (op) {
        case rhi::LoadOp::Load:
            return WGPULoadOp_Load;
        case rhi::LoadOp::Clear:
            return WGPULoadOp_Clear;
        case rhi::LoadOp::DontCare:
            return WGPULoadOp_Clear; // Deterministic refinement of undefined contents.
        }
        return WGPULoadOp_Undefined;
    }

    static WGPUStoreOp store(rhi::StoreOp op) noexcept
    {
        switch (op) {
        case rhi::StoreOp::Store:
            return WGPUStoreOp_Store;
        case rhi::StoreOp::DontCare:
            return WGPUStoreOp_Discard;
        }
        return WGPUStoreOp_Undefined;
    }

    static const char* depthStencil(const rhi::RenderPassDepthStencilAttachmentDesc& desc,
        rhi::TextureFormat format,
        WGPUTextureView view,
        WGPURenderPassDepthStencilAttachment& output) noexcept
    {
        if (const auto* id = rhi::semantics::DepthStencilAttachmentInputContract::diagnostic(desc))
            return id;
        if (!view || !rhi::isDepthTextureFormat(format))
            return "RHI-WEBGPU-RENDER-PASS-DEPTH-METADATA-INVALID";
        auto lowered = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        lowered.view = view;
        lowered.depthLoadOp = load(desc.depthLoadOp);
        lowered.depthStoreOp = store(desc.depthStoreOp);
        lowered.depthClearValue = desc.depthClearValue;
        lowered.depthReadOnly = desc.depthReadOnly;
        if (format == rhi::TextureFormat::Depth24Stencil8) {
            lowered.stencilLoadOp = load(desc.stencilLoadOp);
            lowered.stencilStoreOp = store(desc.stencilStoreOp);
            lowered.stencilClearValue = desc.stencilClearValue;
            lowered.stencilReadOnly = desc.stencilReadOnly;
        } else {
            // Resolved depth-only format explicitly has no stencil attachment.
            lowered.stencilLoadOp = WGPULoadOp_Undefined;
            lowered.stencilStoreOp = WGPUStoreOp_Undefined;
            lowered.stencilClearValue = 0u;
            lowered.stencilReadOnly = true;
        }
        output = lowered;
        return nullptr;
    }
#endif
};
} // namespace arp::rhi_webgpu
