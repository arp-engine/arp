#pragma once

#include "core/algorithm/row_major_index_grid.h"
#include "core/math/unsigned_range.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/resource_texture_aspect_validation.h"
#include "rhi/frontend/resource_texture_resource_layout.h"
#include "rhi/semantics/command_state_lowering.h"

#include <optional>

namespace arp::rhi::semantics {
// Value geometry only. Canonical cells do not manufacture original controls.
class OriginalTextureStateRange final {
public:
    static std::optional<OriginalTextureStateRange> capture(
        const TextureDesc& desc, std::uint64_t maxCells, const char*& diagnostic) noexcept
    {
        diagnostic = nullptr;
        const auto formatBytes = isBlockCompressed(desc.format) ? bytesPerBlock(desc.format)
                                                                : bytesPerPixel(desc.format);
        if (!formatBytes) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-RANGE-FORMAT";
            return {};
        }
        if (!desc.extent.width || !desc.extent.height || !desc.extent.depth
            || (desc.extent.depth > 1u && desc.arrayLayerCount != 1u)) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-RANGE-DIMENSION";
            return {};
        }
        if (!desc.mipLevelCount || !desc.arrayLayerCount) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-RANGE-EMPTY";
            return {};
        }
        const std::uint64_t planes = desc.format == TextureFormat::Depth24Stencil8 ? 2u : 1u;
        const core::RowMajorIndexGrid grid(
            desc.mipLevelCount, desc.arrayLayerCount, maxCells / planes);
        if (!grid.valid()) {
            diagnostic = "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY";
            return {};
        }
        return OriginalTextureStateRange(
            TextureResourceLayout::capture(desc), grid.size() * planes);
    }

    const char* validate(const TextureSubresourceRange& range) const noexcept
    {
        if (!core::containsUnsignedRange<std::uint32_t>(
                0u, layout_.mipLevelCount, range.baseMipLevel, range.mipLevelCount)
            || !core::containsUnsignedRange<std::uint32_t>(
                0u, layout_.arrayLayerCount, range.baseArrayLayer, range.arrayLayerCount))
            return "RHI-ORIGINAL-TEXTURE-RANGE-BOUNDS";
        return TextureAspectValidation::textureAspectMatchesFormat(layout_.format, range.aspect)
            ? nullptr
            : "RHI-ORIGINAL-TEXTURE-RANGE-ASPECT";
    }

    bool isWholeAspect(const TextureSubresourceRange& range) const noexcept
    {
        return !validate(range) && !range.baseMipLevel && !range.baseArrayLayer
            && range.mipLevelCount == layout_.mipLevelCount
            && range.arrayLayerCount == layout_.arrayLayerCount;
    }

    std::uint64_t rangeCellCount(const TextureSubresourceRange& range) const noexcept
    {
        return validate(range)
            ? 0u
            : core::RowMajorIndexGrid(range.mipLevelCount, range.arrayLayerCount).size();
    }

    bool cellIndex(const TextureSubresourceRange& range,
        std::uint64_t ordinal,
        std::uint64_t& output) const noexcept
    {
        output = 0u;
        if (validate(range))
            return false;
        std::uint64_t mip = 0u, layer = 0u;
        if (!core::RowMajorIndexGrid(range.mipLevelCount, range.arrayLayerCount)
                .coordinates(ordinal, mip, layer))
            return false;
        const core::RowMajorIndexGrid grid(layout_.mipLevelCount, layout_.arrayLayerCount);
        if (!grid.index(range.baseMipLevel + mip, range.baseArrayLayer + layer, output))
            return false;
        if (range.aspect == TextureAspect::Stencil)
            output += grid.size();
        return true;
    }

    // Reverse the same canonical grid used by cellIndex; aspect planes never
    // alias. A single-cell range is the stable key for ordered state replay.
    bool cellRange(std::uint64_t cell, TextureSubresourceRange& output) const noexcept
    {
        output = {};
        if (cell >= cells_)
            return false;
        const core::RowMajorIndexGrid grid(layout_.mipLevelCount, layout_.arrayLayerCount);
        const bool stencil = cell >= grid.size();
        std::uint64_t mip = 0u, layer = 0u;
        if (!grid.coordinates(stencil ? cell - grid.size() : cell, mip, layer))
            return false;
        output = {static_cast<std::uint32_t>(mip),
            1u,
            static_cast<std::uint32_t>(layer),
            1u,
            stencil                                    ? TextureAspect::Stencil
                : isDepthTextureFormat(layout_.format) ? TextureAspect::Depth
                                                       : TextureAspect::Color};
        return true;
    }

    bool containsCell(const TextureSubresourceRange& range, std::uint64_t cell) const noexcept
    {
        TextureSubresourceRange point;
        return !validate(range) && cellRange(cell, point) && point.aspect == range.aspect
            && core::containsUnsignedRange<std::uint32_t>(
                range.baseMipLevel, range.mipLevelCount, point.baseMipLevel, 1u)
            && core::containsUnsignedRange<std::uint32_t>(
                range.baseArrayLayer, range.arrayLayerCount, point.baseArrayLayer, 1u);
    }

    std::uint64_t cellCount() const noexcept { return cells_; }

    bool matches(const TextureDesc& desc) const noexcept { return layout_.matches(desc); }

    bool originalRangeIssuerAvailable() const noexcept { return false; }

    static constexpr auto lowering(Backend backend) noexcept
    {
        return arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend);
    }

private:
    OriginalTextureStateRange(TextureResourceLayout layout, std::uint64_t cells) noexcept
        : layout_(layout), cells_(cells)
    {}

    const TextureResourceLayout layout_;
    const std::uint64_t cells_;
};
} // namespace arp::rhi::semantics
