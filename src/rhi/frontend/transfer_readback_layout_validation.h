#pragma once
#include "rhi/frontend/resource_texture_extent.h"
#include "rhi/frontend/resource_texture_resource_layout.h"
#include "rhi/frontend/transfer_readback_arguments.h"
#include "rhi/frontend/transfer_validation.h"

namespace arp::rhi::semantics {
struct TextureReadbackMip {
    TextureSubresourceRange range;
    Extent3D extent, physicalOrigin, physicalExtent;
    std::uint32_t bytesPerRow = 0u, rowsPerImage = 0u, rowCount = 0u, sliceCount = 0u;
    std::uint64_t rowBytes = 0u, bufferOffsetBytes = 0u, footprintBytes = 0u;

    bool matches(const TextureReadbackMip& other) const noexcept
    {
        return range == other.range && extent == other.extent
            && physicalOrigin == other.physicalOrigin && physicalExtent == other.physicalExtent
            && bytesPerRow == other.bytesPerRow && rowsPerImage == other.rowsPerImage
            && rowCount == other.rowCount && sliceCount == other.sliceCount
            && rowBytes == other.rowBytes && bufferOffsetBytes == other.bufferOffsetBytes
            && footprintBytes == other.footprintBytes;
    }
};

// OriginalTextureReadbackLayout/1. Descriptor comparison and checked physical
// geometry only; native source/staging ownership and result proof stay separate.
class TextureReadbackLayoutValidation final {
public:
    static std::size_t mipCount(const TextureReadbackInputSnapshot& input) noexcept
    {
        return input.mipLayouts().empty() ? 1u : input.mipLayouts().size();
    }

    static const char* validate(
        const TextureReadbackInputSnapshot& input, const TextureResourceLayout& source) noexcept
    {
        ReadbackTextureDesc d;
        input.scalarFields().writeScalarFields(d);
        if (!d.sourceResource.isValid() || !d.destinationReadbackBuffer.isValid()
            || d.sourceResource == d.destinationReadbackBuffer || !d.sizeBytes
            || !d.backendOwnedStaging || d.stagingIntent != StagingIntent::Readback)
            return "RHI-TEXTURE-READBACK-LAYOUT-INPUT";
        if (!d.extent.width || !d.extent.height || !d.extent.depth)
            return "RHI-TEXTURE-READBACK-LAYOUT-LEGACY-EXTENT";
        if ((!bytesPerPixel(source.format) && !bytesPerBlock(source.format))
            || isDepthTextureFormat(source.format)
            || d.sourceSubresources.aspect != TextureAspect::Color)
            return "RHI-TEXTURE-READBACK-LAYOUT-FORMAT-ASPECT";
        if (!source.extent.width || !source.extent.height || !source.extent.depth
            || !source.mipLevelCount || !source.arrayLayerCount || source.sampleCount != 1u
            || (source.usage & TextureUsage::CopySrc) == TextureUsage::None)
            return "RHI-TEXTURE-READBACK-LAYOUT-SOURCE";
        if ((source.extent.depth > 1u
                && (source.arrayLayerCount != 1u || source.cubeCompatible
                    || isBlockCompressed(source.format)))
            || (source.cubeCompatible
                && (source.extent.width != source.extent.height || source.arrayLayerCount % 6u)))
            return "RHI-TEXTURE-READBACK-LAYOUT-DIMENSION";
        Extent3D base;
        if (TextureSubresourceExtent::resolve(source.extent, source.mipLevelCount, 0u, base))
            return "RHI-TEXTURE-READBACK-LAYOUT-SOURCE-MIPS";
        const auto& range = d.sourceSubresources;
        if (!core::containsUnsignedRange<std::uint32_t>(
                0u, source.mipLevelCount, range.baseMipLevel, range.mipLevelCount)
            || !core::containsUnsignedRange<std::uint32_t>(
                0u, source.arrayLayerCount, range.baseArrayLayer, range.arrayLayerCount)
            || range.mipLevelCount != mipCount(input))
            return "RHI-TEXTURE-READBACK-LAYOUT-RANGE";
        if (!input.mipLayouts().empty()) {
            const auto& first = input.mipLayouts()[0];
            if (first.extent != d.extent || first.bytesPerRow != d.bytesPerRow
                || first.rowsPerImage != d.rowsPerImage
                || first.bufferOffsetBytes != d.destinationOffsetBytes)
                return "RHI-TEXTURE-READBACK-LAYOUT-FIRST-MIP";
        }
        return nullptr;
    }

    class Mapper final {
    public:
        Mapper(
            const TextureReadbackInputSnapshot& input, const TextureResourceLayout& source) noexcept
            : input_(input), source_(source)
        {}

        bool operator()(std::size_t index, TextureReadbackMip& output) noexcept
        {
            if (diagnostic_)
                return false;
            TextureReadbackMip mapped;
            diagnostic_ = map(index, mapped);
            if (diagnostic_)
                return false;
            output = mapped;
            return true;
        }

        const char* diagnostic() const noexcept { return diagnostic_; }

    private:
        const char* map(std::size_t index, TextureReadbackMip& out) noexcept
        {
            if (index != next_ || index >= mipCount(input_))
                return "RHI-TEXTURE-READBACK-LAYOUT-MIP-ORDER";
            if (!index)
                if (const auto* id = validate(input_, source_))
                    return id;
            ReadbackTextureDesc d;
            input_.scalarFields().writeScalarFields(d);
            const auto level =
                d.sourceSubresources.baseMipLevel + static_cast<std::uint32_t>(index);
            TextureToBufferCopyDesc::MipLayout mip{
                level, d.extent, d.bytesPerRow, d.rowsPerImage, d.destinationOffsetBytes};
            if (!input_.mipLayouts().empty())
                mip = input_.mipLayouts()[index];
            if (mip.mipLevel != level)
                return "RHI-TEXTURE-READBACK-LAYOUT-MIP-ORDER";
            TextureDesc texture;
            source_.write(texture);
            out.range = d.sourceSubresources;
            out.range.baseMipLevel = level;
            out.range.mipLevelCount = 1u;
            out.extent = mip.extent;
            if (!TextureTransferValidation::copyExtentValid(texture, out.range, out.extent))
                return "RHI-TEXTURE-READBACK-LAYOUT-REGION";
            Extent3D full;
            if (TextureSubresourceExtent::resolve(
                    source_.extent, source_.mipLevelCount, level, full))
                return "RHI-TEXTURE-READBACK-LAYOUT-REGION";
            const auto block = blockExtent(source_.format);
            const auto unit = isBlockCompressed(source_.format) ? bytesPerBlock(source_.format)
                                                                : bytesPerPixel(source_.format);
            if (!TextureTransferValidation::copyBlockExtentValid(texture, out.range, out.extent))
                return "RHI-TEXTURE-READBACK-LAYOUT-BLOCK-EDGE";
            if (!unit || mip.bytesPerRow % unit || mip.bufferOffsetBytes % unit
                || !TextureTransferValidation::copyLayoutValid(texture,
                    out.range,
                    out.extent,
                    mip.bytesPerRow,
                    mip.rowsPerImage,
                    mip.bufferOffsetBytes,
                    d.sizeBytes))
                return "RHI-TEXTURE-READBACK-LAYOUT-PITCH-RANGE";
            out.bytesPerRow = mip.bytesPerRow;
            out.rowsPerImage = mip.rowsPerImage;
            out.bufferOffsetBytes = mip.bufferOffsetBytes;
            out.rowBytes =
                TextureTransferValidation::copyRowBytes(source_.format, mip.extent.width);
            out.rowCount =
                TextureTransferValidation::copyRowCount(source_.format, mip.extent.height);
            out.sliceCount =
                TextureTransferValidation::copySliceCount(texture, out.range, mip.extent);
            std::uint64_t rows = 0u;
            if (!core::StridedIndexRange(
                    0u, static_cast<std::uint64_t>(out.sliceCount) + 1u, out.rowsPerImage)
                    .index(out.sliceCount, rows)
                || rows == UINT64_MAX
                || !core::StridedIndexRange(0u, rows + 1u, out.bytesPerRow)
                    .index(rows, out.footprintBytes))
                return "RHI-TEXTURE-READBACK-LAYOUT-OVERFLOW";
            if (!core::containsUnsignedRange<std::uint64_t>(
                    0u, d.sizeBytes, out.bufferOffsetBytes, out.footprintBytes))
                return "RHI-TEXTURE-READBACK-LAYOUT-FOOTPRINT";
            if (out.bufferOffsetBytes < previousEnd_)
                return "RHI-TEXTURE-READBACK-LAYOUT-OVERLAP";
            std::uint64_t width = 0u, height = 0u;
            const auto blocksX = blocksForExtent(mip.extent.width, block),
                       blocksY = blocksForExtent(mip.extent.height, block);
            if (!core::StridedIndexRange(
                    0u, static_cast<std::uint64_t>(blocksX) + 1u, block, UINT32_MAX)
                    .index(blocksX, width)
                || !core::StridedIndexRange(
                    0u, static_cast<std::uint64_t>(blocksY) + 1u, block, UINT32_MAX)
                    .index(blocksY, height))
                return "RHI-TEXTURE-READBACK-LAYOUT-PHYSICAL-OVERFLOW";
            out.physicalOrigin = {
                0u, 0u, source_.extent.depth > 1u ? 0u : out.range.baseArrayLayer};
            out.physicalExtent = {static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                out.sliceCount};
            previousEnd_ = out.bufferOffsetBytes + out.footprintBytes;
            ++next_;
            return nullptr;
        }

        const TextureReadbackInputSnapshot& input_;
        const TextureResourceLayout& source_;
        std::size_t next_ = 0u;
        std::uint64_t previousEnd_ = 0u;
        const char* diagnostic_ = nullptr;
    };
};
} // namespace arp::rhi::semantics
