#pragma once

namespace arp::rhi {

// Public L2 format identity, independent of runtime/allocator initialization.
// Preserve numeric values: serialized and cross-backend consumers share them.
enum class TextureFormat {
    Unknown,
    Bgra8Unorm,
    Rgba8Unorm,
    Rgba8UnormSrgb,
    R8Unorm, // Single-channel normalized mask (TAA reactive mask)
    Rgba16Float,
    Rg16Float, // Two-channel 16-bit float (TAA history moments)
    R32Float,  // Single-channel 32-bit float (shadow map, depth-only data)
    D32Float,
    Depth24Stencil8,
    // Block-compressed formats (added for offline-baked BC7 KTX2 textures).
    // These are stored as 4x4 texel blocks rather than per-texel bytes, so
    // bytesPerPixel() returns 0 for them -- use bytesPerBlock()/blockExtent()/
    // bytesForTextureMip() instead. The sRGB variant only affects sampler
    // decode (the on-disk block bytes are identical to the UNORM form).
    Bc1Unorm,
    Bc3Unorm,
    Bc7Unorm,
    Bc7UnormSrgb,
};

} // namespace arp::rhi
