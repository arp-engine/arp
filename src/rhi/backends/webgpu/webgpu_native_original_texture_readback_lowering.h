#pragma once
#include "rhi/backends/webgpu/webgpu_original_texture_readback_copies.h"

namespace arp::rhi_webgpu {
// Only the common recorder consumes exact original decisions into its encoder.
class WebGpuNativeOriginalTextureReadbackLowering final {
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuConformanceClearFrame;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static const char* emit(WGPUCommandEncoder encoder,
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& copies,
        std::size_t index,
        void* device) noexcept
    {
        if (!encoder || !copies
            || copies->transaction()->status() != WebGpuOriginalStateTransaction::Status::Pending)
            return "RHI-WEBGPU-ORIGINAL-READBACK-EMIT-STATE";
        if (index >= copies->decisions().size())
            return "RHI-WEBGPU-ORIGINAL-READBACK-EMIT-INDEX";
        const auto& decision = copies->decisions()[index];
        if (const auto* id = WebGpuOriginalTextureReadbackCopies::validateCurrent(
                copies, index, decision, device))
            return id;
#if defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        const auto& pair = copies->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        return emitFrozen(encoder,
            *pair->destination()->source()->resource().texture,
            *pair->destination()->nativeResource(),
            decision);
#else
        return "RHI-WEBGPU-READBACK-COPIES-API-UNAVAILABLE";
#endif
    }

    // Recorder-private: callers must authenticate the exact root/journal and
    // frozen Join inputs before reaching this common native emission helper.
    static const char* emitFrozen(WGPUCommandEncoder encoder,
        const WebGpuNativeTexturePayload& source,
        const WebGpuNativeBufferPayload& destination,
        const rhi::semantics::TextureReadbackCopyLowering::Decision& decision) noexcept
    {
        if (const auto* id =
                rhi::semantics::TextureReadbackCopyLowering::validateOriginal(rhi::Backend::WebGPU,
                    1u,
                    true,
                    decision.layout,
                    decision.mipIndex,
                    decision.original,
                    decision))
            return id;
#if defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        if (!encoder || !source.texture || !destination.buffer || !source.ownerDevice
            || source.ownerDevice != destination.ownerDevice)
            return "RHI-WEBGPU-ORIGINAL-READBACK-EMIT-BINDING";
        const auto& mip = decision.original;
        auto texture = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        texture.texture = source.texture;
        texture.mipLevel = mip.range.baseMipLevel;
        switch (mip.range.aspect) {
        case rhi::TextureAspect::Color:
            texture.aspect = WGPUTextureAspect_All;
            break;
        case rhi::TextureAspect::Depth:
            texture.aspect = WGPUTextureAspect_DepthOnly;
            break;
        case rhi::TextureAspect::Stencil:
            texture.aspect = WGPUTextureAspect_StencilOnly;
            break;
        default:
            return "RHI-WEBGPU-TRANSFER-ASPECT-UNSUPPORTED";
        }
        texture.origin = {
            mip.physicalOrigin.width, mip.physicalOrigin.height, mip.physicalOrigin.depth};
        auto buffer = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        buffer.buffer = destination.buffer;
        buffer.layout.offset = mip.bufferOffsetBytes;
        buffer.layout.bytesPerRow = mip.bytesPerRow;
        buffer.layout.rowsPerImage = mip.rowsPerImage;
        const WGPUExtent3D extent{
            mip.physicalExtent.width, mip.physicalExtent.height, mip.physicalExtent.depth};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &texture, &buffer, &extent);
        return nullptr;
#else
        return "RHI-WEBGPU-READBACK-COPIES-API-UNAVAILABLE";
#endif
    }
#endif
};
} // namespace arp::rhi_webgpu
