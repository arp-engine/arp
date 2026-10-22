#pragma once
#include "core/algorithm/strided_index_range.h"
#include "core/math/unsigned_range.h"
#include "rhi/frontend/resource_texture_aspect_validation.h"
#include "rhi/gpu_commands.h"

namespace arp::rhi::semantics {
// Scalar L2 texel/range validation. Buffer-copy pitch alignment is the explicit
// WebGPU lowering contract (256 bytes); host uploads remain a separate domain.
class TextureTransferValidation final {
public:
    static bool textureAspectMatchesFormat(rhi::TextureFormat format, rhi::TextureAspect aspect)
    {
        return TextureAspectValidation::textureAspectMatchesFormat(format, aspect);
    }

    static std::uint32_t mipExtent(std::uint32_t value, std::uint32_t mipLevel)
    {
        const auto shifted = mipLevel < 32u ? (value >> mipLevel) : 0u;
        return shifted == 0u ? 1u : shifted;
    }

    static bool textureRangeValid(
        const rhi::TextureDesc& texture, const rhi::TextureSubresourceRange& range)
    {
        return range.mipLevelCount == 1u && range.arrayLayerCount > 0u
            && range.baseMipLevel < texture.mipLevelCount
            && range.baseArrayLayer < texture.arrayLayerCount
            && range.arrayLayerCount <= texture.arrayLayerCount
            && range.baseArrayLayer <= texture.arrayLayerCount - range.arrayLayerCount
            && textureAspectMatchesFormat(texture.format, range.aspect);
    }

    static bool copyExtentValid(const rhi::TextureDesc& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent)
    {
        if (!textureRangeValid(texture, range) || extent.width == 0u || extent.height == 0u
            || extent.depth == 0u || texture.sampleCount != 1u) {
            return false;
        }
        const auto width = mipExtent(texture.extent.width, range.baseMipLevel);
        const auto height = mipExtent(texture.extent.height, range.baseMipLevel);
        if (extent.width > width || extent.height > height) {
            return false;
        }
        if (texture.extent.depth > 1u) {
            return range.baseArrayLayer == 0u && range.arrayLayerCount == 1u
                && extent.depth <= mipExtent(texture.extent.depth, range.baseMipLevel);
        }
        return extent.depth == 1u;
    }

    // A partial block at the right/bottom edge is legal only at that mip's
    // physical edge. Shared by transfer, upload and readback layout consumers.
    static bool copyBlockExtentValid(const rhi::TextureDesc& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent,
        const rhi::Extent3D& origin = {0u, 0u, 0u}) noexcept
    {
        if ((!rhi::bytesPerPixel(texture.format) && !rhi::bytesPerBlock(texture.format))
            || !textureUploadRegionValid(texture, range, extent, origin))
            return false;
        const auto block = rhi::blockExtent(texture.format);
        const auto width = mipExtent(texture.extent.width, range.baseMipLevel),
                   height = mipExtent(texture.extent.height, range.baseMipLevel);
        return block && !(origin.width % block) && !(origin.height % block)
            && (!(extent.width % block) || extent.width == width - origin.width)
            && (!(extent.height % block) || extent.height == height - origin.height);
    }

    static std::uint32_t copySliceCount(const rhi::TextureDesc& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent)
    {
        return texture.extent.depth > 1u ? extent.depth : range.arrayLayerCount;
    }

    static std::uint32_t copyRowCount(rhi::TextureFormat format, std::uint32_t height)
    {
        if (!rhi::isBlockCompressed(format)) {
            return height;
        }
        return rhi::blocksForExtent(height, rhi::blockExtent(format));
    }

    static std::uint64_t copyRowBytes(rhi::TextureFormat format, std::uint32_t width)
    {
        if (rhi::isBlockCompressed(format)) {
            return static_cast<std::uint64_t>(rhi::blocksForExtent(width, rhi::blockExtent(format)))
                * rhi::bytesPerBlock(format);
        }
        return static_cast<std::uint64_t>(width) * rhi::bytesPerPixel(format);
    }

    static bool copyLayoutValid(const rhi::TextureDesc& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent,
        std::uint32_t bytesPerRow,
        std::uint32_t rowsPerImage,
        std::uint64_t bufferOffsetBytes,
        std::uint64_t bufferSizeBytes)
    {
        if (!copyExtentValid(texture, range, extent) || bytesPerRow == 0u || rowsPerImage == 0u
            || (bytesPerRow % rhi::kTextureUploadRowPitchAlignment) != 0u
            || (bufferOffsetBytes & 3u) != 0u) {
            return false;
        }
        const auto rowBytes = copyRowBytes(texture.format, extent.width);
        const auto rowCount = copyRowCount(texture.format, extent.height);
        if (rowBytes == 0u || bytesPerRow < rowBytes || rowsPerImage < rowCount) {
            return false;
        }
        const auto slices = copySliceCount(texture, range, extent);
        const core::StridedIndexRange sliceRows(0u, slices, rowsPerImage);
        std::uint64_t lastSliceRow = 0u;
        if (!sliceRows.index(slices - 1u, lastSliceRow))
            return false;
        const core::StridedIndexRange finalRows(lastSliceRow, rowCount, 1u);
        std::uint64_t finalRow = 0u;
        if (!finalRows.index(rowCount - 1u, finalRow) || finalRow == UINT64_MAX)
            return false;
        const core::StridedIndexRange rowAddresses(bufferOffsetBytes, finalRow + 1u, bytesPerRow);
        std::uint64_t finalRowAddress = 0u;
        return rowAddresses.index(finalRow, finalRowAddress)
            && core::containsUnsignedRange<std::uint64_t>(
                0u, bufferSizeBytes, finalRowAddress, rowBytes);
    }

    static bool textureUploadRegionValid(const rhi::TextureDesc& texture,
        const rhi::TextureSubresourceRange& range,
        const rhi::Extent3D& extent,
        const rhi::Extent3D& destinationOffset)
    {
        if (!copyExtentValid(texture, range, extent)) {
            return false;
        }
        const auto width = mipExtent(texture.extent.width, range.baseMipLevel);
        const auto height = mipExtent(texture.extent.height, range.baseMipLevel);
        if (destinationOffset.width > width || extent.width > width - destinationOffset.width
            || destinationOffset.height > height
            || extent.height > height - destinationOffset.height) {
            return false;
        }
        if (texture.extent.depth > 1u) {
            const auto depth = mipExtent(texture.extent.depth, range.baseMipLevel);
            return destinationOffset.depth <= depth
                && extent.depth <= depth - destinationOffset.depth;
        }
        return destinationOffset.depth == 0u;
    }
};
} // namespace arp::rhi::semantics
