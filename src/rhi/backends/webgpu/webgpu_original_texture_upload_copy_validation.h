#pragma once
#include "rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h"
#include "rhi/backends/webgpu/webgpu_original_texture_upload_transaction.h"
#include "rhi/frontend/transfer_upload_copy.h"

namespace arp::rhi_webgpu {
// OriginalTextureUploadCopyLowering/1 exact current native fact binding only.
// No ordinary Transfer lowering, native emit or additional command proof issuer.
class WebGpuOriginalTextureUploadCopyValidation final {
public:
    static const char* validateCurrent(
        const std::shared_ptr<const WebGpuOriginalTextureUploadTransaction>& transaction,
        std::size_t mipIndex,
        const rhi::semantics::TextureUploadCopyLowering::Decision& decision,
        void* expectedDevice) noexcept
    {
        using Lowering = rhi::semantics::TextureUploadCopyLowering;
        rhi::semantics::TextureUploadPackedCopy mapped;
        if (const auto* id = WebGpuOriginalTextureUploadTransaction::mapPackedCopyCurrent(
                transaction, mipIndex, mapped))
            return id;
        const auto& source = transaction->states()
                                 ->inputs()
                                 ->scalars()
                                 ->usage()
                                 ->projection()
                                 ->originalTextureUploads()[0];
        if (const auto* id = Lowering::validateOriginal(rhi::Backend::WebGPU,
                1u,
                true,
                source->packing(),
                source->resource().id,
                mipIndex,
                mapped,
                decision))
            return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#if !defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        (void)expectedDevice;
        return "RHI-WEBGPU-TEXTURE-COPY-LOWERING-API-UNAVAILABLE";
#else
        const auto& buffer = *source->resource().buffer;
        const auto& texture = *source->destination()->resource().texture;
        if (!expectedDevice || buffer.ownerDevice != expectedDevice
            || texture.ownerDevice != expectedDevice)
            return "RHI-WEBGPU-TEXTURE-COPY-LOWERING-DEVICE";
        const auto dimension = decision.dimension == Lowering::Dimension::Texture3D
            ? WGPUTextureDimension_3D
            : WGPUTextureDimension_2D;
        if (texture.dimension != dimension || texture.rhiFormat != mapped.format || !buffer.buffer
            || !texture.texture || !texture.contentAccessPolicy().originalUploadExclusive())
            return "RHI-WEBGPU-TEXTURE-COPY-LOWERING-NATIVE-FACTS";
        if (!mapped.bytesPerRow || mapped.bytesPerRow % rhi::kTextureUploadRowPitchAlignment
            || !mapped.rowsPerImage)
            return "RHI-WEBGPU-TEXTURE-COPY-LOWERING-PITCH";
        return WebGpuNativeTransferCopyLowering::textureDiagnostic(
            texture, mapped.subresources, mapped.extent);
#endif
#else
        (void)expectedDevice;
        return "RHI-WEBGPU-TEXTURE-COPY-LOWERING-NATIVE-UNAVAILABLE";
#endif
    }
};
} // namespace arp::rhi_webgpu
