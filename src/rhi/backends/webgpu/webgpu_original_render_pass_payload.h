#pragma once
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
// The selected native descriptor contract requires the emdawn WebGPU header.
// Older SDK headers are explicitly unavailable, never a compatibility lowering.
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE) && defined(WGPU_RENDER_PASS_DESCRIPTOR_INIT)
#define ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE 1
#include "rhi/backends/webgpu/webgpu_attachment_operation_lowering.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"
#endif
#include "core/bounded_sequence.h"
#include "rhi/frontend/command_arguments.h"

namespace arp::rhi_webgpu {
// Shared original attachment builder. The caller owns the authentic arguments
// and closure, validates bindings before allocation and supplies an exact draw
// bound. Finalize only at the final address; never move afterwards.
class WebGpuOriginalRenderPassPayload final {
public:
    WebGpuOriginalRenderPassPayload(const rhi::RenderPassArguments* original, std::uint64_t draws)
        : pass(original)
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
          ,
          colors(original->desc().colorAttachments.size())
#endif
    {
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        maxDraw.maxDrawCount = draws;
#else
        (void)draws;
#endif
    }
    WebGpuOriginalRenderPassPayload(WebGpuOriginalRenderPassPayload&&) = default;
    const rhi::RenderPassArguments* pass;
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
    template <class Observation>
    static bool validViewBinding(
        const Observation& observation, rhi::TextureFormat format, bool depth) noexcept
    {
        return format == observation.viewDesc().format && depth == rhi::isDepthTextureFormat(format)
            && observation.view() && observation.view()->view
            && observation.view()->format == nativeTextureFormat(format);
    }

    core::BoundedSequence<WGPURenderPassColorAttachment> colors;
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPURenderPassMaxDrawCount maxDraw{};
    WGPURenderPassDescriptor renderPass = WGPU_RENDER_PASS_DESCRIPTOR_INIT;

    template <class ResolveView> const char* finalize(ResolveView&& view) noexcept
    {
        if (renderPass.nextInChain || !colors.values().empty())
            return "RHI-WEBGPU-ORIGINAL-PASS-ALREADY-FINALIZED";
        for (const auto& desc : pass->desc().colorAttachments) {
            auto color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.nextInChain = nullptr;
            color.view = view(desc.target);
            color.resolveTarget = desc.hasResolveTarget ? view(desc.resolveTarget) : nullptr;
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp = WebGpuAttachmentOperationLowering::load(desc.loadOp);
            color.storeOp = WebGpuAttachmentOperationLowering::store(desc.storeOp);
            const auto& clear = desc.clearColor;
            color.clearValue = {clear.r, clear.g, clear.b, clear.a};
            if (!color.view || (desc.hasResolveTarget && !color.resolveTarget))
                return "RHI-WEBGPU-ORIGINAL-PASS-VIEW-MISSING";
            if (color.loadOp == WGPULoadOp_Undefined || color.storeOp == WGPUStoreOp_Undefined)
                return "RHI-RENDER-PASS-ATTACHMENT-OP-INVALID";
            if (!colors.append(color))
                return "RHI-WEBGPU-CANONICAL-LOWERING-COLOR-CAPACITY";
        }
        if (pass->desc().hasDepthStencilAttachment) {
            const auto& d = pass->desc().depthStencilAttachment;
            if (const auto* id = WebGpuAttachmentOperationLowering::depthStencil(
                    d, d.format, view(d.target), depth))
                return id;
        }
        maxDraw.chain.next = nullptr;
        maxDraw.chain.sType = WGPUSType_RenderPassMaxDrawCount;
        renderPass.nextInChain = &maxDraw.chain;
        renderPass.label = webGpuNativeStringView(pass->desc().label);
        renderPass.colorAttachmentCount = colors.values().size();
        renderPass.colorAttachments = colors.values().data();
        renderPass.depthStencilAttachment =
            pass->desc().hasDepthStencilAttachment ? &depth : nullptr;
        renderPass.occlusionQuerySet = nullptr;
        renderPass.timestampWrites = nullptr;
        return nullptr;
    }

    template <class ResolveView>
    bool matches(ResolveView&& view, std::uint64_t expectedDrawCount) const noexcept
    {
        const auto& desc = pass->desc();
        if (renderPass.nextInChain != &maxDraw.chain || maxDraw.chain.next
            || maxDraw.chain.sType != WGPUSType_RenderPassMaxDrawCount
            || maxDraw.maxDrawCount != expectedDrawCount
            || renderPass.label.data != desc.label.data()
            || renderPass.label.length != desc.label.size()
            || renderPass.colorAttachmentCount != desc.colorAttachments.size()
            || colors.values().size() != desc.colorAttachments.size()
            || renderPass.colorAttachments != colors.values().data()
            || renderPass.depthStencilAttachment
                != (desc.hasDepthStencilAttachment ? &depth : nullptr)
            || renderPass.occlusionQuerySet || renderPass.timestampWrites)
            return false;
        for (std::size_t i = 0u; i < desc.colorAttachments.size(); ++i) {
            const auto& original = desc.colorAttachments[i];
            const auto& native = colors.values()[i];
            const auto& clear = original.clearColor;
            if (native.nextInChain || native.view != view(original.target)
                || native.resolveTarget
                    != (original.hasResolveTarget ? view(original.resolveTarget) : nullptr)
                || native.depthSlice != WGPU_DEPTH_SLICE_UNDEFINED
                || native.loadOp != WebGpuAttachmentOperationLowering::load(original.loadOp)
                || native.storeOp != WebGpuAttachmentOperationLowering::store(original.storeOp)
                || native.clearValue.r != clear.r || native.clearValue.g != clear.g
                || native.clearValue.b != clear.b || native.clearValue.a != clear.a)
                return false;
        }
        if (desc.hasDepthStencilAttachment) {
            auto expected = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            const auto& d = desc.depthStencilAttachment;
            if (WebGpuAttachmentOperationLowering::depthStencil(
                    d, d.format, view(d.target), expected))
                return false;
            if (depth.view != expected.view || depth.depthLoadOp != expected.depthLoadOp
                || depth.depthStoreOp != expected.depthStoreOp
                || depth.depthClearValue != expected.depthClearValue
                || depth.depthReadOnly != expected.depthReadOnly
                || depth.stencilLoadOp != expected.stencilLoadOp
                || depth.stencilStoreOp != expected.stencilStoreOp
                || depth.stencilClearValue != expected.stencilClearValue
                || depth.stencilReadOnly != expected.stencilReadOnly)
                return false;
        }
        return true;
    }
#endif
};
} // namespace arp::rhi_webgpu
