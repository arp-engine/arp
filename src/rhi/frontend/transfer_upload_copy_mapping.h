#pragma once
#include "rhi/frontend/transfer_upload_packing.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi::semantics {
// OriginalTextureUploadPackedCopy/1: comparison-only L2 mapping. Logical region
// geometry is preserved; this value does not grant target-write authority.
struct TextureUploadPackedCopy {
    GpuResourceId sourceBuffer, destinationTexture;
    TextureSubresourceRange subresources;
    Extent3D extent, origin;
    TextureFormat format = TextureFormat::Unknown;
    std::uint64_t bufferOffsetBytes = 0u, packedFootprintBytes = 0u, frameSerial = 0u,
                  contentGeneration = 0u;
    std::uint32_t bytesPerRow = 0u, rowsPerImage = 0u;

    bool matches(const TextureUploadPackedCopy& other) const noexcept
    {
        return sourceBuffer == other.sourceBuffer && destinationTexture == other.destinationTexture
            && subresources == other.subresources && extent == other.extent
            && origin == other.origin && format == other.format
            && bufferOffsetBytes == other.bufferOffsetBytes
            && packedFootprintBytes == other.packedFootprintBytes
            && frameSerial == other.frameSerial && contentGeneration == other.contentGeneration
            && bytesPerRow == other.bytesPerRow && rowsPerImage == other.rowsPerImage;
    }
};

class TextureUploadPackedCopyMapping final {
public:
    struct Lowering {
        bool metadataEquivalent = false, nativeExecution = false;
        const char* diagnostic = "RHI-TEXTURE-PACKED-COPY-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::TransferLowering::TextureUploadPackedCopyMapping_lowering<Lowering>(
            backend);
    }

    static const char* mapOriginal(std::uint32_t contractVersion,
        bool prototypeOptIn,
        const std::shared_ptr<const TextureUploadPacking>& packing,
        GpuResourceId sourceBuffer,
        std::size_t mipIndex,
        TextureUploadPackedCopy& output) noexcept
    {
        if (contractVersion != 1u)
            return "RHI-TEXTURE-PACKED-COPY-VERSION";
        if (!prototypeOptIn)
            return "RHI-TEXTURE-PACKED-COPY-OPT-IN";
        if (!packing || !packing->hasQuotaBoundSharedOwner(packing))
            return "RHI-TEXTURE-PACKED-COPY-ORIGINAL-PACKING";
        if (!packing->inputOwner()
            || !packing->inputOwner()->hasQuotaBoundSharedOwner(packing->inputOwner()))
            return "RHI-TEXTURE-PACKED-COPY-ORIGINAL-INPUT";
        UploadTextureDesc fields;
        packing->inputOwner()->scalarFields().writeScalarFields(fields);
        if (!sourceBuffer.isValid() || !fields.destinationTexture.isValid()
            || sourceBuffer == fields.destinationTexture)
            return "RHI-TEXTURE-PACKED-COPY-RESOURCE-IDS";
        if (mipIndex >= packing->mipLayouts().size())
            return "RHI-TEXTURE-PACKED-COPY-MIP-INDEX";
        const auto& mip = packing->mipLayouts()[mipIndex];
        if (!core::containsUnsignedRange<std::uint64_t>(
                0u, packing->packedBytes().size(), mip.packedOffset, mip.packedSize))
            return "RHI-TEXTURE-PACKED-COPY-PACKED-RANGE";
        TextureUploadPackedCopy value;
        value.sourceBuffer = sourceBuffer;
        value.destinationTexture = fields.destinationTexture;
        value.subresources = mip.range;
        value.extent = mip.extent;
        value.origin = mip.origin;
        value.format = fields.format;
        value.bufferOffsetBytes = mip.packedOffset;
        value.packedFootprintBytes = mip.packedSize;
        value.bytesPerRow = mip.packedBytesPerRow;
        value.rowsPerImage = mip.rowCount;
        value.frameSerial = fields.frameSerial;
        value.contentGeneration = fields.resourceGeneration;
        output = value;
        return nullptr;
    }

    static const char* validateOriginal(std::uint32_t contractVersion,
        bool prototypeOptIn,
        const std::shared_ptr<const TextureUploadPacking>& packing,
        GpuResourceId sourceBuffer,
        std::size_t mipIndex,
        const TextureUploadPackedCopy& input) noexcept
    {
        TextureUploadPackedCopy expected;
        if (const auto* id = mapOriginal(
                contractVersion, prototypeOptIn, packing, sourceBuffer, mipIndex, expected))
            return id;
        return expected.matches(input) ? nullptr : "RHI-TEXTURE-PACKED-COPY-MAPPING-MISMATCH";
    }
};
} // namespace arp::rhi::semantics
