#pragma once

#include <cstdint>

namespace arp::renderer::visibility {

struct DepthPyramidExtent {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

constexpr bool operator==(DepthPyramidExtent lhs, DepthPyramidExtent rhs) noexcept
{
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

constexpr bool operator!=(DepthPyramidExtent lhs, DepthPyramidExtent rhs) noexcept
{
    return !(lhs == rhs);
}

constexpr bool validDepthPyramidExtent(DepthPyramidExtent extent) noexcept
{
    return extent.width != 0u && extent.height != 0u;
}

constexpr std::uint32_t depthPyramidMipLevelCount(DepthPyramidExtent extent) noexcept
{
    if (!validDepthPyramidExtent(extent)) {
        return 0u;
    }

    std::uint32_t levels = 1u;
    while (extent.width > 1u || extent.height > 1u) {
        // Match the portable texture-mip definition: each level halves with
        // floor division, clamped at one. Using ceil here can request one
        // more level than Vulkan/Metal/WebGPU permit for non-power-of-two
        // extents (for example 1280x720).
        extent.width = extent.width > 1u ? extent.width / 2u : 1u;
        extent.height = extent.height > 1u ? extent.height / 2u : 1u;
        ++levels;
    }
    return levels;
}

constexpr DepthPyramidExtent depthPyramidMipExtent(
    DepthPyramidExtent baseExtent, std::uint32_t mipLevel) noexcept
{
    if (!validDepthPyramidExtent(baseExtent) || mipLevel >= depthPyramidMipLevelCount(baseExtent)) {
        return {};
    }
    while (mipLevel-- != 0u) {
        baseExtent.width = baseExtent.width > 1u ? baseExtent.width / 2u : 1u;
        baseExtent.height = baseExtent.height > 1u ? baseExtent.height / 2u : 1u;
    }
    return baseExtent;
}

// The pyramid stores a max-depth reduction for the engine's non-reversed
// [0, 1] depth convention. This rejects only objects entirely behind every
// depth sample in their selected conservative screen-space footprint.
constexpr bool isConservativelyOccludedByMaxDepth(
    float objectNearestDepth, float pyramidMaxDepth) noexcept
{
    return objectNearestDepth > pyramidMaxDepth;
}

} // namespace arp::renderer::visibility
