#pragma once
#include "core/memory_budget_ledger.h"
#include "rhi/frontend/resource_texture_extent.h"
#include "rhi/frontend/resource_texture_resource_layout.h"
#include "rhi/frontend/transfer_upload_input.h"
#include "rhi/frontend/transfer_validation.h"

namespace arp::rhi::semantics {
// Comparison value only. An original registered resource/revision is a separate
// mandatory execution prerequisite; copying a caller generation cannot prove it.
using TextureUploadDestinationLayout = TextureResourceLayout;

struct TextureUploadPackedMip {
    TextureSubresourceRange range;
    Extent3D extent, origin;
    std::uint32_t sourceBytesPerRow = 0u, sourceRowsPerImage = 0u;
    std::uint32_t rowCount = 0u, sliceCount = 0u, packedBytesPerRow = 0u;
    std::uint64_t rowBytes = 0u, sourceOffset = 0u, sourceSize = 0u, packedOffset = 0u,
                  packedSize = 0u;
};

// OriginalTextureUploadPacking/1: shared deterministic L2/CPU lowering. No
// backend capability, native format support or resource-generation authority.
class TextureUploadLayoutValidation final {
public:
    static std::size_t mipCount(const TextureUploadInputSnapshot& input) noexcept
    {
        return input.mipLayouts().empty() ? 1u : input.mipLayouts().size();
    }

    static const char* validate(const TextureUploadInputSnapshot& input,
        const TextureUploadDestinationLayout& destination) noexcept
    {
        UploadTextureDesc d;
        input.scalarFields().writeScalarFields(d);
        const auto& t = destination;
        if (!d.destinationTexture.value || !input.callerPixelPointerPresent() || !d.pixelDataSize
            || d.stagingIntent != StagingIntent::Upload || !d.backendOwnedStaging)
            return "RHI-TEXTURE-UPLOAD-LAYOUT-INPUT";
        if (!bytesPerPixel(d.format) && !bytesPerBlock(d.format))
            return "RHI-TEXTURE-UPLOAD-LAYOUT-FORMAT";
        if (isDepthTextureFormat(d.format) || d.subresources.aspect != TextureAspect::Color)
            return "RHI-TEXTURE-UPLOAD-LAYOUT-ASPECT";
        if (d.format != t.format)
            return "RHI-TEXTURE-UPLOAD-LAYOUT-FORMAT-MISMATCH";
        if (!t.extent.width || !t.extent.height || !t.extent.depth || !t.mipLevelCount
            || !t.arrayLayerCount || t.sampleCount != 1u
            || (t.usage & TextureUsage::CopyDst) == TextureUsage::None)
            return "RHI-TEXTURE-UPLOAD-LAYOUT-DESTINATION";
        if ((t.extent.depth > 1u
                && (t.arrayLayerCount != 1u || t.cubeCompatible || isBlockCompressed(t.format)))
            || (t.cubeCompatible && (t.extent.width != t.extent.height || t.arrayLayerCount % 6u)))
            return "RHI-TEXTURE-UPLOAD-LAYOUT-DIMENSION";
        Extent3D baseExtent;
        if (TextureSubresourceExtent::resolve(t.extent, t.mipLevelCount, 0u, baseExtent))
            return "RHI-TEXTURE-UPLOAD-LAYOUT-DESTINATION-MIPS";
        if (d.mipLevel != d.subresources.baseMipLevel
            || d.arrayLayer != d.subresources.baseArrayLayer)
            return "RHI-TEXTURE-UPLOAD-LAYOUT-INDEPENDENT-RANGE";
        if (!core::containsUnsignedRange<std::uint32_t>(
                0u, t.mipLevelCount, d.subresources.baseMipLevel, d.subresources.mipLevelCount)
            || !core::containsUnsignedRange<std::uint32_t>(0u,
                t.arrayLayerCount,
                d.subresources.baseArrayLayer,
                d.subresources.arrayLayerCount)
            || d.subresources.mipLevelCount != mipCount(input))
            return "RHI-TEXTURE-UPLOAD-LAYOUT-RANGE";
        if (!input.mipLayouts().empty()) {
            const auto& first = input.mipLayouts()[0];
            if (first.extent != d.extent || first.bytesPerRow != d.bytesPerRow
                || first.rowsPerImage != d.rowsPerImage)
                return "RHI-TEXTURE-UPLOAD-LAYOUT-FIRST-MIP";
        }
        return nullptr;
    }

    // Cursor belongs to a fresh deterministic pass. Sorted nonoverlapping full
    // source footprints, including padding, avoid a local interval structure.
    class Mapper final {
    public:
        Mapper(const TextureUploadInputSnapshot& input,
            const TextureUploadDestinationLayout& destination) noexcept
            : input_(input), destination_(destination)
        {}

        bool operator()(std::size_t index, TextureUploadPackedMip& out) noexcept
        {
            if (diagnostic_)
                return false;
            diagnostic_ = map(index, out);
            return diagnostic_ == nullptr;
        }

        const char* diagnostic() const noexcept { return diagnostic_; }

        std::uint64_t packedBytes() const noexcept { return packedEnd_; }

    private:
        const char* map(std::size_t index, TextureUploadPackedMip& out) noexcept
        {
            if (index != next_ || index >= mipCount(input_))
                return "RHI-TEXTURE-UPLOAD-LAYOUT-MIP-ORDER";
            if (!index)
                if (const auto* id = validate(input_, destination_))
                    return id;
            UploadTextureDesc d;
            input_.scalarFields().writeScalarFields(d);
            const auto mip = d.subresources.baseMipLevel + static_cast<std::uint32_t>(index);
            UploadTextureDesc::MipLayout source{
                d.mipLevel, d.extent, d.bytesPerRow, d.rowsPerImage, 0u};
            if (!input_.mipLayouts().empty())
                source = input_.mipLayouts()[index];
            if (source.mipLevel != mip)
                return "RHI-TEXTURE-UPLOAD-LAYOUT-MIP-ORDER";
            TextureDesc texture;
            destination_.write(texture);
            out.range = d.subresources;
            out.range.baseMipLevel = mip;
            out.range.mipLevelCount = 1u;
            out.extent = source.extent;
            out.origin = d.destinationOffset;
            if (!TextureTransferValidation::textureUploadRegionValid(
                    texture, out.range, out.extent, out.origin))
                return "RHI-TEXTURE-UPLOAD-LAYOUT-REGION";
            const auto unit =
                isBlockCompressed(d.format) ? bytesPerBlock(d.format) : bytesPerPixel(d.format);
            if (!TextureTransferValidation::copyBlockExtentValid(
                    texture, out.range, out.extent, out.origin))
                return "RHI-TEXTURE-UPLOAD-LAYOUT-BLOCK-REGION";
            out.rowBytes = TextureTransferValidation::copyRowBytes(d.format, out.extent.width);
            out.rowCount = TextureTransferValidation::copyRowCount(d.format, out.extent.height);
            out.sliceCount =
                TextureTransferValidation::copySliceCount(texture, out.range, out.extent);
            out.sourceBytesPerRow = source.bytesPerRow;
            out.sourceRowsPerImage = source.rowsPerImage;
            out.sourceOffset = source.bufferOffsetBytes;
            if (!out.sourceBytesPerRow || out.sourceBytesPerRow < out.rowBytes
                || out.sourceBytesPerRow % unit || out.sourceRowsPerImage < out.rowCount
                || out.sourceOffset % unit)
                return "RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-PITCH";
            std::uint64_t sourceRows = 0u, packedRows = 0u;
            if (!core::StridedIndexRange(
                    0u, static_cast<std::uint64_t>(out.sliceCount) + 1u, out.sourceRowsPerImage)
                    .index(out.sliceCount, sourceRows)
                || !core::StridedIndexRange(0u, sourceRows + 1u, out.sourceBytesPerRow)
                    .index(sourceRows, out.sourceSize))
                return "RHI-TEXTURE-UPLOAD-LAYOUT-OVERFLOW";
            if (!core::containsUnsignedRange<std::uint64_t>(
                    0u, input_.pixelBytes().size(), out.sourceOffset, out.sourceSize))
                return "RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-RANGE";
            if (out.sourceOffset < sourceEnd_)
                return "RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-OVERLAP";
            constexpr auto alignment = kTextureUploadRowPitchAlignment;
            if (out.rowBytes > SIZE_MAX)
                return "RHI-TEXTURE-UPLOAD-LAYOUT-OVERFLOW";
            const auto aligned =
                core::alignCpuAllocationOffsetUp(static_cast<std::size_t>(out.rowBytes), alignment);
            if (!aligned.success || aligned.alignedValue > UINT32_MAX)
                return "RHI-TEXTURE-UPLOAD-LAYOUT-OVERFLOW";
            out.packedBytesPerRow = static_cast<std::uint32_t>(aligned.alignedValue);
            out.packedOffset = packedEnd_;
            if (!core::StridedIndexRange(
                    0u, static_cast<std::uint64_t>(out.sliceCount) + 1u, out.rowCount)
                    .index(out.sliceCount, packedRows)
                || !core::StridedIndexRange(0u, packedRows + 1u, out.packedBytesPerRow)
                    .index(packedRows, out.packedSize)
                || !core::MemoryBudgetLedger::canAdd(out.packedOffset, out.packedSize))
                return "RHI-TEXTURE-UPLOAD-LAYOUT-OVERFLOW";
            sourceEnd_ = out.sourceOffset + out.sourceSize;
            packedEnd_ = out.packedOffset + out.packedSize;
            ++next_;
            return nullptr;
        }

        const TextureUploadInputSnapshot& input_;
        const TextureUploadDestinationLayout& destination_;
        std::size_t next_ = 0u;
        std::uint64_t sourceEnd_ = 0u, packedEnd_ = 0u;
        const char* diagnostic_ = nullptr;
    };

    // Inspect the complete normative packed footprint without allocating or
    // packing bytes. Failed inspection never publishes a partial byte count.
    static const char* inspectPackedBytes(const TextureUploadInputSnapshot& input,
        const TextureUploadDestinationLayout& destination,
        std::uint64_t& bytes) noexcept
    {
        bytes = 0u;
        Mapper mapper(input, destination);
        for (std::size_t i = 0u; i < mipCount(input); ++i) {
            TextureUploadPackedMip mip;
            if (!mapper(i, mip))
                return mapper.diagnostic();
        }
        bytes = mapper.packedBytes();
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
