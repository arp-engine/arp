#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/transfer_copy.h"
#include "rhi/frontend/transfer_validation.h"

namespace arp::rhi_webgpu {

// One validation entry for cold input preparation and registered recording.
class WebGpuTransferCopyValidation final {
public:
    static const char* validate(const WebGpuTransferResourceObservation& s,
        const WebGpuTransferResourceObservation& d,
        const rhi::BufferCopyDesc& a) noexcept
    {
        if (!s.buffer || !d.buffer || !a.sizeBytes
            || (s.bufferDesc.usage & rhi::ResourceUsage::CopySrc) == rhi::ResourceUsage::None
            || (d.bufferDesc.usage & rhi::ResourceUsage::CopyDst) == rhi::ResourceUsage::None
            || s.buffer == d.buffer || (a.sourceOffsetBytes & 3u) || (a.destinationOffsetBytes & 3u)
            || (a.sizeBytes & 3u)
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, s.bufferDesc.sizeBytes, a.sourceOffsetBytes, a.sizeBytes)
            || !core::containsUnsignedRange<std::uint64_t>(
                0u, d.bufferDesc.sizeBytes, a.destinationOffsetBytes, a.sizeBytes))
            return "RHI-WEBGPU-TRANSFER-BUFFER-COPY-CONTRACT";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (s.buffer->buffer == d.buffer->buffer)
            return "RHI-WEBGPU-TRANSFER-BUFFER-COPY-CONTRACT";
#endif
        return nullptr;
    }

    static const char* validate(const WebGpuTransferResourceObservation& s,
        const WebGpuTransferResourceObservation& d,
        const rhi::BufferToTextureCopyDesc& a) noexcept
    {
        if (d.texture)
            if (const auto* id = d.texture->ordinaryContentAccessDiagnostic())
                return id;
        return inspectBufferTextureLayout(s, d, a);
    }

    // Geometry/usage inspection only. Exclusive content authority still belongs
    // to the canonical original join and exact root, never this metadata result.
    static const char* inspectBufferTextureLayout(const WebGpuTransferResourceObservation& s,
        const WebGpuTransferResourceObservation& d,
        const rhi::BufferToTextureCopyDesc& a) noexcept
    {
        if (!s.buffer || !d.texture || !a.mipLayouts.empty()
            || (s.bufferDesc.usage & rhi::ResourceUsage::CopySrc) == rhi::ResourceUsage::None
            || (d.textureDesc.usage & rhi::TextureUsage::CopyDst) == rhi::TextureUsage::None
            || !rhi::semantics::TextureTransferValidation::copyLayoutValid(d.textureDesc,
                a.textureSubresources,
                a.extent,
                a.bytesPerRow,
                a.rowsPerImage,
                a.bufferOffsetBytes,
                s.bufferDesc.sizeBytes))
            return "RHI-WEBGPU-TRANSFER-BUFFER-TEXTURE-CONTRACT";
        return nullptr;
    }

    static const char* validate(const WebGpuTransferResourceObservation& s,
        const WebGpuTransferResourceObservation& d,
        const rhi::TextureToBufferCopyDesc& a) noexcept
    {
        if (s.texture)
            if (const auto* id = s.texture->ordinaryContentAccessDiagnostic())
                return id;
        return inspectTextureBufferLayout(s, d, a);
    }

    // Pure layout inspection, parallel to inspectBufferTextureLayout. Exclusive
    // attachment authority must come from the authentic original Join2/root.
    static const char* inspectTextureBufferLayout(const WebGpuTransferResourceObservation& s,
        const WebGpuTransferResourceObservation& d,
        const rhi::TextureToBufferCopyDesc& a) noexcept
    {
        if (!s.texture || !d.buffer || !a.mipLayouts.empty()
            || (s.textureDesc.usage & rhi::TextureUsage::CopySrc) == rhi::TextureUsage::None
            || (d.bufferDesc.usage & rhi::ResourceUsage::CopyDst) == rhi::ResourceUsage::None
            || !rhi::semantics::TextureTransferValidation::copyLayoutValid(s.textureDesc,
                a.textureSubresources,
                a.extent,
                a.bytesPerRow,
                a.rowsPerImage,
                a.bufferOffsetBytes,
                d.bufferDesc.sizeBytes))
            return "RHI-WEBGPU-TRANSFER-TEXTURE-BUFFER-CONTRACT";
        return nullptr;
    }

    static const char* validate(const WebGpuTransferResourceObservation& s,
        const WebGpuTransferResourceObservation& d,
        const rhi::TextureToTextureCopyDesc& a) noexcept
    {
        if (s.texture)
            if (const auto* id = s.texture->ordinaryContentAccessDiagnostic())
                return id;
        if (d.texture)
            if (const auto* id = d.texture->ordinaryContentAccessDiagnostic())
                return id;
        if (!s.texture || !d.texture || !a.mipLayouts.empty() || s.texture == d.texture
            || (s.textureDesc.usage & rhi::TextureUsage::CopySrc) == rhi::TextureUsage::None
            || (d.textureDesc.usage & rhi::TextureUsage::CopyDst) == rhi::TextureUsage::None
            || s.textureDesc.format != d.textureDesc.format
            || a.sourceSubresources.arrayLayerCount != a.destinationSubresources.arrayLayerCount
            || !rhi::semantics::TextureTransferValidation::copyExtentValid(
                s.textureDesc, a.sourceSubresources, a.extent)
            || !rhi::semantics::TextureTransferValidation::copyExtentValid(
                d.textureDesc, a.destinationSubresources, a.extent))
            return "RHI-WEBGPU-TRANSFER-TEXTURE-COPY-CONTRACT";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (s.texture->texture == d.texture->texture)
            return "RHI-WEBGPU-TRANSFER-TEXTURE-COPY-CONTRACT";
#endif
        return nullptr;
    }

    template <class Desc>
    static const char* validateTyped(const WebGpuTransferResourceObservation& s,
        const WebGpuTransferResourceObservation& d,
        const rhi::TransferCopyArguments& a) noexcept
    {
        Desc desc;
        if (!a.write(desc))
            return "RHI-WEBGPU-TRANSFER-ARGUMENTS-KIND";
        return validate(s, d, desc);
    }

    static const char* validate(
        const WebGpuOrdinaryTransferRetention& root, const rhi::TransferCopyArguments& a) noexcept
    {
        using Kind = rhi::CommandUsageKind;
        if (a.kind() == Kind::Finish)
            return nullptr;
        const auto* s = root.find(a.source());
        const auto* d = root.find(a.destination());
        if (!s || !d)
            return "RHI-WEBGPU-TRANSFER-RESOURCE-NOT-PREPARED";
        if (a.source() == a.destination())
            return "RHI-WEBGPU-TRANSFER-SELF-COPY-UNSUPPORTED";
        if (d->writeSeal && d->writeSeal->transferWritesSealed())
            return "RHI-BUFFER-GPU-WRITES-SEALED";
        switch (a.kind()) {
        case Kind::BufferCopy:
            return validateTyped<rhi::BufferCopyDesc>(*s, *d, a);
        case Kind::BufferToTextureCopy:
            return validateTyped<rhi::BufferToTextureCopyDesc>(*s, *d, a);
        case Kind::TextureToBufferCopy:
            return validateTyped<rhi::TextureToBufferCopyDesc>(*s, *d, a);
        case Kind::TextureToTextureCopy:
            return validateTyped<rhi::TextureToTextureCopyDesc>(*s, *d, a);
        default:
            return "RHI-WEBGPU-TRANSFER-ARGUMENTS-KIND";
        }
    }
};
} // namespace arp::rhi_webgpu
