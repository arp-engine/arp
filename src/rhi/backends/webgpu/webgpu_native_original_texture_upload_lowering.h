#pragma once
#include "rhi/backends/webgpu/webgpu_original_texture_upload_copy_validation.h"

namespace arp::rhi_webgpu {
// Private native consumer of OriginalTextureUploadCopyLowering/1. Only the
// existing recorder can emit into its original encoder after applying intent.
class WebGpuNativeOriginalTextureUploadLowering final {
    friend class WebGpuOrdinaryRecording;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static const char* emit(WGPUCommandEncoder encoder,
        const std::shared_ptr<const WebGpuOriginalTextureUploadTransaction>& transaction,
        std::size_t index,
        void* device) noexcept
    {
        if (!encoder || !transaction
            || transaction->status() != WebGpuOriginalTextureUploadTransaction::Status::Pending)
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-EMIT-STATE";
        if (index >= transaction->decisions().size())
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-EMIT-INDEX";
        const auto& decision = transaction->decisions()[index];
        if (const auto* id = WebGpuOriginalTextureUploadCopyValidation::validateCurrent(
                transaction, index, decision, device))
            return id;
#if defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        const auto& source = transaction->states()
                                 ->inputs()
                                 ->scalars()
                                 ->usage()
                                 ->projection()
                                 ->originalTextureUploads()[0];
        auto buffer = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        buffer.buffer = source->resource().buffer->buffer;
        buffer.layout.offset = decision.original.bufferOffsetBytes;
        buffer.layout.bytesPerRow = decision.original.bytesPerRow;
        buffer.layout.rowsPerImage = decision.original.rowsPerImage;
        auto texture = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        texture.texture = source->destination()->resource().texture->texture;
        texture.mipLevel = decision.original.subresources.baseMipLevel;
        texture.origin = {decision.physicalOrigin.width,
            decision.physicalOrigin.height,
            decision.physicalOrigin.depth};
        // Version1 validates Color explicitly; no implicit native aspect default.
        texture.aspect = WGPUTextureAspect_All;
        const WGPUExtent3D extent{decision.physicalExtent.width,
            decision.physicalExtent.height,
            decision.physicalExtent.depth};
        wgpuCommandEncoderCopyBufferToTexture(encoder, &buffer, &texture, &extent);
        return nullptr;
#else
        return "RHI-WEBGPU-TEXTURE-COPY-LOWERING-API-UNAVAILABLE";
#endif
    }
#endif
};
} // namespace arp::rhi_webgpu
