#pragma once

#include "rhi/gpu_texture_format.h"

namespace arp::editor {

// Explicit display policy shared by native and ordered viewport executors.
// Choosing the baseline never rewrites a caller's HDR/D32 resource descriptor.
struct EditorSceneViewportSurfaceProfile {
    rhi::TextureFormat color = rhi::TextureFormat::Rgba16Float;
    rhi::TextureFormat depth = rhi::TextureFormat::D32Float;

    static constexpr EditorSceneViewportSurfaceProfile baseline()
    {
        return {rhi::TextureFormat::Rgba8Unorm, rhi::TextureFormat::Depth24Stencil8};
    }

    constexpr bool valid() const noexcept
    {
        return (color == rhi::TextureFormat::Rgba16Float && depth == rhi::TextureFormat::D32Float)
            || (color == baseline().color && depth == baseline().depth);
    }
};
} // namespace arp::editor
