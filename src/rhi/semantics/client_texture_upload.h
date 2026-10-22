#pragma once

#include "core/container/array_view.h"
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Keep the original public type and mangled signatures. Rows are top-left,
// straight-alpha RGBA8; no colorspace conversion, flip or mip generation.
struct WebGlColdTextureUpload {
    std::uint32_t contractVersion = 0;
    std::uint32_t mipLevel = 0;
    Extent3D origin{0, 0, 0}, extent;
    std::uint32_t bytesPerRow = 0;
    core::ArrayView<std::uint8_t> bytes;
};

using ClientTextureUpload = WebGlColdTextureUpload;
} // namespace arp::rhi::semantics
