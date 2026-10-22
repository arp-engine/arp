#pragma once

// KTX2 container reader/writer for BC7-compressed 2D textures.
//
// Scope: this is a narrow, hand-written KTX2 implementation covering only the
// wocc offline-baking use case -- 2D (non-array, non-cube, non-3D) textures
// stored as BC7 (VK_FORMAT_BC7_UNORM_BLOCK=145 or BC7_SRGB_BLOCK=146) with NO
// supercompression (supercompressionScheme=0, so byteLength==uncompressedByteLength
// per level). It does NOT support BasisLZ/UASTC transcode, Zstd/Zlib, layers,
// cubemaps, 3D volume textures, or non-BC7 vkFormats. The full KTX2 spec is
// far broader; this implementation intentionally handles only the subset the
// wocc texture baker emits.
//
// The reader is dependency-free (no Vulkan headers, no bc7enc) -- it only
// validates the container and hands back raw BC7 block bytes per mip level.
// The GPU decodes BC7 at sampling time; no CPU-side block decode happens here.
// The writer links bc7enc (compiled into arp_assets) to compress RGBA8 mip
// pixels into BC7 blocks before wrapping them in the KTX2 container.
//
// Mirrors the assets::decodeImageToRgba8 / DecodedImage style (PMR containers,
// arp::core::defaultCpuAllocator routing, ARP_API linkage, diagnostics string
// on failure rather than exceptions).

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace arp::assets {

// A single mip level of BC7 block data. The block bytes are opaque 16-byte
// BC7 blocks (one per 4x4 texel footprint); the GPU decodes them. Width and
// height are in texels (not blocks) so callers can size the GPU texture and
// compute upload extents without re-deriving from block counts.
struct Ktx2MipLevel {
    std::uint32_t width = 0;                   // texels
    std::uint32_t height = 0;                  // texels
    std::pmr::vector<std::uint8_t> blockBytes; // ceil(w/4)*ceil(h/4)*16 bytes
};

// A decoded KTX2/BC7 image: full mip chain (largest first), format metadata.
struct Ktx2Image {
    std::uint32_t width = 0;               // base level texel width
    std::uint32_t height = 0;              // base level texel height
    std::uint16_t vkFormat = 0;            // 145=BC7_UNORM_BLOCK, 146=BC7_SRGB_BLOCK
    bool srgb = false;                     // derived from vkFormat (146 => true)
    std::pmr::vector<Ktx2MipLevel> levels; // levels[0] is largest
};

struct Ktx2DecodeResult {
    bool success = false;
    std::pmr::string diagnostics;
    Ktx2Image image;
};

// Decode a KTX2/BC7 file from an in-memory byte buffer. Validates the 12-byte
// magic, parses the 80-byte header + level index + DFD, and copies out each
// mip level's BC7 block bytes. Returns success=false with diagnostics on any
// validation failure (bad magic, unsupported format/supercompression, truncated
// data, mismatched level counts). No filesystem access -- mirror decodeImageToRgba8.
ARP_API Ktx2DecodeResult decodeKtx2(const std::pmr::vector<std::uint8_t>& fileBytes);

// Encode a BC7 mip chain (RGBA8 source pixels per level) into a KTX2 file on
// disk. Each level's RGBA8 pixels are compressed to BC7 blocks via bc7enc
// (16 bytes per 4x4 block), then wrapped in a minimal KTX2 container:
// 80-byte header + 24-byte-per-level index + a fixed ~44-byte BC7 DFD +
// a minimal KV block with a KTXwriter entry. supercompressionScheme=0.
// srgb selects vkFormat 146 (BC7_SRGB_BLOCK) vs 145 (BC7_UNORM_BLOCK) and
// the DFD transferFunction (SRGB=2 vs LINEAR=1). Returns false on I/O or
// encode failure (diagnostics populated). The base level (levels[0]) must
// be the largest; subsequent levels must be exact half-dimensions down to 1x1.
ARP_API bool writeKtx2Bc7(const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    bool srgb,
    const std::pmr::vector<Ktx2MipLevel>& levels);

// Compress a single RGBA8 mip level into BC7 block bytes using bc7enc.
// pixels is width*height*4 bytes (RGBA, R first in memory). Output is
// ceil(width/4)*ceil(height/4)*16 bytes. perceptual=true uses YCbCr error
// (better for color maps); false uses linear RGB (better for data maps like
// normals/roughness). This is exposed so the wocc baker can control encoding
// parameters per texture semantic.
ARP_API std::pmr::vector<std::uint8_t> encodeRgba8ToBc7Blocks(
    const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height, bool perceptual);

} // namespace arp::assets
