#pragma once
#include "rhi/gpu_resource_types.h"

#include <algorithm>

namespace arp::rhi::semantics {
// Texture mip dimensions are floor(parent / 2^level), clamped to one texel.
// This is descriptor mathematics, never native allocation or upload evidence.
class TextureSubresourceExtent final {
public:
    static const char* resolve(
        Extent3D parent, std::uint32_t mipCount, std::uint32_t level, Extent3D& output) noexcept
    {
        output = {0u, 0u, 0u};
        if (!parent.width || !parent.height || !parent.depth || !mipCount)
            return "RHI-TEXTURE-SUBRESOURCE-EXTENT-INVALID";
        std::uint32_t maximumLevels = 1u;
        for (auto dimension = std::max({parent.width, parent.height, parent.depth}); dimension > 1u;
            dimension >>= 1u)
            ++maximumLevels;
        if (mipCount > maximumLevels || level >= mipCount)
            return "RHI-TEXTURE-SUBRESOURCE-MIP-RANGE";
        output = {std::max(1u, parent.width >> level),
            std::max(1u, parent.height >> level),
            std::max(1u, parent.depth >> level)};
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
