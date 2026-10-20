#pragma once
#include "rhi/gpu_resource_types.h"

namespace arp::rhi {
// A single cube is six explicitly authored square array faces. Whole-view
// ranges and completed content authority are checked by the caller separately.
class OriginalCompletedColorTextureViewContract final {
public:
    static constexpr bool isSingleCube(
        const TextureDesc& texture, const TextureViewDesc& view) noexcept
    {
        return texture.cubeCompatible && texture.extent.depth == 1u && texture.extent.width
            && texture.extent.width == texture.extent.height && texture.arrayLayerCount == 6u
            && view.dimension == TextureViewDimension::TextureCube;
    }
};
} // namespace arp::rhi
