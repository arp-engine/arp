#pragma once
#include "rhi/backends/webgpu/webgpu_transfer_copy_validation.h"

#include <limits>

namespace arp::rhi_webgpu {
// NativeTransferCopyLowering/1: no public execution or proof authority.
class WebGpuNativeTransferCopyLowering final {
public:
    static const char* validate(const WebGpuOrdinaryTransferRetention& root,
        const rhi::TransferCopyArguments& copy,
        void* expectedDevice) noexcept
    {
        if (!root.matches())
            return "RHI-WEBGPU-TRANSFER-IDENTITY-CHANGED";
        if (const auto* id = WebGpuTransferCopyValidation::validate(root, copy))
            return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        for (const auto& resource : root.resources()) {
            const auto device =
                resource.buffer ? resource.buffer->ownerDevice : resource.texture->ownerDevice;
            if (!expectedDevice || device != expectedDevice)
                return "RHI-WEBGPU-ORDINARY-RECORDING-DEVICE-MISMATCH";
        }
        switch (copy.kind()) {
        case rhi::CommandUsageKind::BufferCopy:
            return nullptr;
#if defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        case rhi::CommandUsageKind::BufferToTextureCopy: {
            rhi::BufferToTextureCopyDesc desc;
            copy.write(desc);
            return textureDiagnostic(
                *root.find(copy.destination())->texture, desc.textureSubresources, desc.extent);
        }
        case rhi::CommandUsageKind::TextureToBufferCopy: {
            rhi::TextureToBufferCopyDesc desc;
            copy.write(desc);
            return textureDiagnostic(
                *root.find(copy.source())->texture, desc.textureSubresources, desc.extent);
        }
        case rhi::CommandUsageKind::TextureToTextureCopy: {
            rhi::TextureToTextureCopyDesc desc;
            copy.write(desc);
            if (const auto* id = textureDiagnostic(
                    *root.find(copy.source())->texture, desc.sourceSubresources, desc.extent))
                return id;
            return textureDiagnostic(
                *root.find(copy.destination())->texture, desc.destinationSubresources, desc.extent);
        }
#endif
        default:
            return "RHI-WEBGPU-TRANSFER-TEXEL-COPY-API-UNAVAILABLE";
        }
#else
        (void)expectedDevice;
        return "RHI-WEBGPU-TRANSFER-NATIVE-UNAVAILABLE";
#endif
    }

private:
    friend class WebGpuNativeCommandPayload;
    friend class WebGpuOrdinaryRecording;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static const char* emitOriginal(WGPUCommandEncoder encoder,
        const WebGpuOrdinaryTransferRetention& root,
        const rhi::TransferCopyArguments& copy) noexcept
    {
        const auto& source = *root.find(copy.source());
        const auto& destination = *root.find(copy.destination());
        const char* diagnostic = nullptr;
        bool success = false;
        switch (copy.kind()) {
        case rhi::CommandUsageKind::BufferCopy: {
            rhi::BufferCopyDesc d;
            copy.write(d);
            success = emit(encoder, *source.buffer, *destination.buffer, d, diagnostic);
            break;
        }
        case rhi::CommandUsageKind::BufferToTextureCopy: {
            rhi::BufferToTextureCopyDesc d;
            copy.write(d);
            success = emit(encoder, *source.buffer, *destination.texture, d, diagnostic);
            break;
        }
        case rhi::CommandUsageKind::TextureToBufferCopy: {
            rhi::TextureToBufferCopyDesc d;
            copy.write(d);
            success = emit(encoder, *source.texture, *destination.buffer, d, diagnostic);
            break;
        }
        case rhi::CommandUsageKind::TextureToTextureCopy: {
            rhi::TextureToTextureCopyDesc d;
            copy.write(d);
            success = emit(encoder, *source.texture, *destination.texture, d, diagnostic);
            break;
        }
        default:
            return "RHI-WEBGPU-TRANSFER-ARGUMENTS-KIND";
        }
        return success ? nullptr : diagnostic;
    }

    static bool validCopyExtent(const rhi::Extent3D& extent) noexcept
    {
        return extent.width && extent.height && extent.depth;
    }

public:
    // Pure native fact validation; does not record or grant authority.
    static const char* textureDiagnostic(const WebGpuNativeTexturePayload& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent) noexcept
    {
        switch (range.aspect) {
        case rhi::TextureAspect::Color:
        case rhi::TextureAspect::Depth:
        case rhi::TextureAspect::Stencil:
            break;
        default:
            return "RHI-WEBGPU-TRANSFER-ASPECT-UNSUPPORTED";
        }
        if (texture.dimension != WGPUTextureDimension_2D
            && texture.dimension != WGPUTextureDimension_3D)
            return "RHI-WEBGPU-TRANSFER-DIMENSION-UNSUPPORTED";
        if (!validCopyExtent(extent) || !range.arrayLayerCount)
            return "RHI-WEBGPU-TRANSFER-EXTENT-INVALID";
        if (!rhi::bytesPerPixel(texture.rhiFormat) && !rhi::bytesPerBlock(texture.rhiFormat))
            return "RHI-WEBGPU-TRANSFER-FORMAT-UNSUPPORTED";
        const auto block = rhi::blockExtent(texture.rhiFormat);
        if (extent.width > std::numeric_limits<std::uint32_t>::max() - (block - 1u)
            || extent.height > std::numeric_limits<std::uint32_t>::max() - (block - 1u))
            return "RHI-WEBGPU-TRANSFER-EXTENT-OVERFLOW";
        return nullptr;
    }

private:
    static WGPUTextureAspect nativeTextureAspect(rhi::TextureAspect aspect) noexcept
    {
        switch (aspect) {
        case rhi::TextureAspect::Color:
            return WGPUTextureAspect_All;
        case rhi::TextureAspect::Depth:
            return WGPUTextureAspect_DepthOnly;
        case rhi::TextureAspect::Stencil:
            return WGPUTextureAspect_StencilOnly;
        }
        return WGPUTextureAspect_Undefined; // Unreachable after textureDiagnostic; never defaults to All.
    }

    static WGPUExtent3D nativeCopyExtent(const WebGpuNativeTexturePayload& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent) noexcept
    {
        const auto block = rhi::blockExtent(texture.rhiFormat);
        return {rhi::blocksForExtent(extent.width, block) * block,
            rhi::blocksForExtent(extent.height, block) * block,
            nativeCopySliceCount(texture, range, extent)};
    }

    static std::uint32_t nativeCopySliceCount(const WebGpuNativeTexturePayload& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent) noexcept
    {
        return texture.dimension == WGPUTextureDimension_2D ? range.arrayLayerCount : extent.depth;
    }

    static std::uint32_t nativeCopyOriginZ(const WebGpuNativeTexturePayload& texture,
        const rhi::TextureSubresourceRange& range) noexcept
    {
        return texture.dimension == WGPUTextureDimension_2D ? range.baseArrayLayer : 0u;
    }
#endif

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static bool emit(WGPUCommandEncoder encoder,
        const WebGpuNativeBufferPayload& source,
        const WebGpuNativeBufferPayload& destination,
        const rhi::BufferCopyDesc& desc,
        const char*& diagnostics)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!encoder || !source.buffer || !destination.buffer || desc.sizeBytes == 0) {
            diagnostics = "WebGPU RHI native buffer copy requires buffers and bytes";
            return false;
        }
        wgpuCommandEncoderCopyBufferToBuffer(encoder,
            source.buffer,
            desc.sourceOffsetBytes,
            destination.buffer,
            desc.destinationOffsetBytes,
            desc.sizeBytes);
        return true;
#else
        (void)source;
        (void)destination;
        (void)desc;
        diagnostics = "WebGPU RHI native command lowering requires Emscripten WebGPU";
        return false;
#endif
    }

#endif

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static bool emit(WGPUCommandEncoder encoder,
        const WebGpuNativeBufferPayload& source,
        const WebGpuNativeTexturePayload& destination,
        const rhi::BufferToTextureCopyDesc& desc,
        const char*& diagnostics)
    {
        if (const auto* id = destination.ordinaryContentAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
        return emitBufferTextureLayout(encoder, source, destination, desc, diagnostics);
    }

    // Only existing Recording can call this private encoder operation after
    // exact same-seal initialized state/content Pending validation. Public and
    // ordinary native paths still reject exclusive content resources.
    static bool emitOriginalContentUpload(WGPUCommandEncoder encoder,
        const WebGpuNativeBufferPayload& source,
        const WebGpuNativeTexturePayload& destination,
        const rhi::BufferToTextureCopyDesc& desc,
        const char*& diagnostics)
    {
        if (!destination.contentAccessPolicy().originalUploadExclusive()) {
            diagnostics = "RHI-WEBGPU-CANONICAL-RECORDING-UPLOAD-CONTENT-POLICY";
            return false;
        }
        return emitBufferTextureLayout(encoder, source, destination, desc, diagnostics);
    }

    static bool emitBufferTextureLayout(WGPUCommandEncoder encoder,
        const WebGpuNativeBufferPayload& source,
        const WebGpuNativeTexturePayload& destination,
        const rhi::BufferToTextureCopyDesc& desc,
        const char*& diagnostics)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE) && defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        if (!encoder || !source.buffer || !destination.texture || !validCopyExtent(desc.extent)) {
            diagnostics = "WebGPU RHI native buffer-to-texture copy requires resources and extent";
            return false;
        }
        if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
            diagnostics = "WebGPU RHI native texture upload requires explicit row and slice pitch";
            return false;
        }
        if ((desc.bytesPerRow % rhi::kTextureUploadRowPitchAlignment) != 0u) {
            diagnostics = "WebGPU RHI native texture upload row pitch must be 256-byte aligned";
            return false;
        }
        if (const auto* id =
                textureDiagnostic(destination, desc.textureSubresources, desc.extent)) {
            diagnostics = id;
            return false;
        }
        auto sourceInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        sourceInfo.buffer = source.buffer;
        sourceInfo.layout.offset = desc.bufferOffsetBytes;
        sourceInfo.layout.bytesPerRow = desc.bytesPerRow;
        sourceInfo.layout.rowsPerImage = desc.rowsPerImage;

        auto destinationInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destinationInfo.texture = destination.texture;
        destinationInfo.mipLevel = desc.textureSubresources.baseMipLevel;
        destinationInfo.origin = WGPUOrigin3D{
            0u,
            0u,
            nativeCopyOriginZ(destination, desc.textureSubresources),
        };
        destinationInfo.aspect = nativeTextureAspect(desc.textureSubresources.aspect);

        const auto extent = nativeCopyExtent(destination, desc.textureSubresources, desc.extent);
        wgpuCommandEncoderCopyBufferToTexture(encoder, &sourceInfo, &destinationInfo, &extent);
        return true;
#else
        (void)source;
        (void)destination;
        (void)desc;
        diagnostics = "WebGPU RHI native buffer-to-texture copy requires texel copy API support";
        return false;
#endif
    }

#endif

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static bool emit(WGPUCommandEncoder encoder,
        const WebGpuNativeTexturePayload& source,
        const WebGpuNativeBufferPayload& destination,
        const rhi::TextureToBufferCopyDesc& desc,
        const char*& diagnostics)
    {
        if (const auto* id = source.ordinaryContentAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE) && defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        if (!encoder || !source.texture || !destination.buffer || !validCopyExtent(desc.extent)) {
            diagnostics = "WebGPU RHI native texture-to-buffer copy requires resources and extent";
            return false;
        }
        if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0
            || (desc.bytesPerRow % rhi::kTextureUploadRowPitchAlignment) != 0u) {
            diagnostics = "WebGPU RHI native texture-to-buffer copy requires a 256-byte row pitch";
            return false;
        }
        if (const auto* id = textureDiagnostic(source, desc.textureSubresources, desc.extent)) {
            diagnostics = id;
            return false;
        }
        auto sourceInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        sourceInfo.texture = source.texture;
        sourceInfo.mipLevel = desc.textureSubresources.baseMipLevel;
        sourceInfo.origin = WGPUOrigin3D{
            0u,
            0u,
            nativeCopyOriginZ(source, desc.textureSubresources),
        };
        sourceInfo.aspect = nativeTextureAspect(desc.textureSubresources.aspect);

        auto destinationInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destinationInfo.buffer = destination.buffer;
        destinationInfo.layout.offset = desc.bufferOffsetBytes;
        destinationInfo.layout.bytesPerRow = desc.bytesPerRow;
        destinationInfo.layout.rowsPerImage = desc.rowsPerImage;

        const auto extent = nativeCopyExtent(source, desc.textureSubresources, desc.extent);
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &sourceInfo, &destinationInfo, &extent);
        return true;
#else
        (void)source;
        (void)destination;
        (void)desc;
        diagnostics = "WebGPU RHI native texture-to-buffer copy requires texel copy API support";
        return false;
#endif
    }

#endif

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static bool emit(WGPUCommandEncoder encoder,
        const WebGpuNativeTexturePayload& source,
        const WebGpuNativeTexturePayload& destination,
        const rhi::TextureToTextureCopyDesc& desc,
        const char*& diagnostics)
    {
        if (const auto* id = source.ordinaryContentAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
        if (const auto* id = destination.ordinaryContentAccessDiagnostic()) {
            diagnostics = id;
            return false;
        }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE) && defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        if (!encoder || !source.texture || !destination.texture || !validCopyExtent(desc.extent)) {
            diagnostics = "WebGPU RHI native texture copy requires resources and extent";
            return false;
        }
        if (const auto* id = textureDiagnostic(source, desc.sourceSubresources, desc.extent)) {
            diagnostics = id;
            return false;
        }
        if (const auto* id =
                textureDiagnostic(destination, desc.destinationSubresources, desc.extent)) {
            diagnostics = id;
            return false;
        }
        auto sourceInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        sourceInfo.texture = source.texture;
        sourceInfo.mipLevel = desc.sourceSubresources.baseMipLevel;
        sourceInfo.origin = WGPUOrigin3D{
            0u,
            0u,
            nativeCopyOriginZ(source, desc.sourceSubresources),
        };
        sourceInfo.aspect = nativeTextureAspect(desc.sourceSubresources.aspect);

        auto destinationInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destinationInfo.texture = destination.texture;
        destinationInfo.mipLevel = desc.destinationSubresources.baseMipLevel;
        destinationInfo.origin = WGPUOrigin3D{
            0u,
            0u,
            nativeCopyOriginZ(destination, desc.destinationSubresources),
        };
        destinationInfo.aspect = nativeTextureAspect(desc.destinationSubresources.aspect);

        const auto extent = nativeCopyExtent(source, desc.sourceSubresources, desc.extent);
        wgpuCommandEncoderCopyTextureToTexture(encoder, &sourceInfo, &destinationInfo, &extent);
        return true;
#else
        (void)source;
        (void)destination;
        (void)desc;
        diagnostics = "WebGPU RHI native texture copy requires texel copy API support";
        return false;
#endif
    }

#endif
};
} // namespace arp::rhi_webgpu
