// BC7 block-compressed texture format contract test.
//
// Verifies the RHI contract for the BC7 formats added to support offline-baked
// KTX2 textures (replacing the runtime nearest-point downscale + CPU mipgen):
//   1. L2 semantics: TextureFormat::Bc7Unorm/Bc7UnormSrgb/Bc1Unorm/Bc3Unorm
//      enums exist; isBlockCompressed()==true; bytesPerPixel()==0 (BC formats
//      are block-based, not per-texel); bytesPerBlock()==16 (BC7/BC3) or 8 (BC1);
//      blockExtent()==4.
//   2. bytesForTextureMip(): BC7 4x4=16 bytes (1 block), 8x8=64 bytes (4 blocks),
//      1x1=16 bytes (still one full block, edge padding), 5x5=64 bytes
//      (ceil(5/4)*ceil(5/4)=2*2=4 blocks).
//   3. estimateTexturePlacementRequirements(): BC7 mip chain sums correctly
//      (e.g. 8x8 mip0 + 4x4 mip1 + 2x2 mip2 + 1x1 mip3 = 64+16+16+16=112 bytes).
//   4. Null device createTexture(Bc7Unorm, Sampled|CopyDst, mipLevelCount=N)
//      succeeds with correct estimated size for both UNORM and SRGB variants.
//   5. TextureView creation on a BC7 texture succeeds (view format round-trips).
//
// This mirrors the rhi_texture_format_rg16f_contract_tests shape: no live
// Vulkan device required, asserts the L2 decision surface and Null texture
// creation. The block-aware upload math (bufferRowLength) is exercised by the
// Vulkan device contract test; here we cover the sizing/creation contract.

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_resources.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    using namespace arp::rhi;

    // === Facet 1: BC7/BC1/BC3 L2 semantics ===
    {
        const TextureFormat formats[] = {
            TextureFormat::Bc1Unorm,
            TextureFormat::Bc3Unorm,
            TextureFormat::Bc7Unorm,
            TextureFormat::Bc7UnormSrgb,
        };
        for (const auto f : formats) {
            assert(f != TextureFormat::Unknown);
            assert(isBlockCompressed(f));
            assert(bytesPerPixel(f) == 0u); // BC is block-based, not per-texel
            assert(blockExtent(f) == 4u);
        }
        // BC1 is 8 bytes/block (4:1 over RGBA8), BC3/BC7 are 16 bytes/block.
        assert(bytesPerBlock(TextureFormat::Bc1Unorm) == 8u);
        assert(bytesPerBlock(TextureFormat::Bc3Unorm) == 16u);
        assert(bytesPerBlock(TextureFormat::Bc7Unorm) == 16u);
        assert(bytesPerBlock(TextureFormat::Bc7UnormSrgb) == 16u);
        // Non-compressed formats are NOT block-compressed.
        assert(!isBlockCompressed(TextureFormat::Rgba8Unorm));
        assert(!isBlockCompressed(TextureFormat::Rgba8UnormSrgb));
        assert(bytesPerPixel(TextureFormat::Rgba8UnormSrgb) == 4u);
        assert(!isBlockCompressed(TextureFormat::Unknown));
        assert(bytesPerBlock(TextureFormat::Rgba8Unorm) == 0u);
        std::printf("[ok] Facet 1: BC7/BC1/BC3 enum + block helpers\n");
    }

    // === Facet 2: bytesForTextureMip edge cases ===
    {
        // 4x4 BC7 = 1 block = 16 bytes
        assert(bytesForTextureMip(TextureFormat::Bc7Unorm, 4, 4) == 16u);
        // 8x8 BC7 = 4 blocks = 64 bytes
        assert(bytesForTextureMip(TextureFormat::Bc7Unorm, 8, 8) == 64u);
        // 1x1 BC7 = 1 block (edge padding) = 16 bytes (NOT 1*1*16=16 by luck,
        // but the point is ceil(1/4)=1 block)
        assert(bytesForTextureMip(TextureFormat::Bc7Unorm, 1, 1) == 16u);
        // 5x5 BC7 = ceil(5/4)*ceil(5/4) = 2*2 = 4 blocks = 64 bytes
        assert(bytesForTextureMip(TextureFormat::Bc7Unorm, 5, 5) == 64u);
        // BC1 is half the bytes (8 bytes/block): 8x8 = 4 blocks * 8 = 32 bytes
        assert(bytesForTextureMip(TextureFormat::Bc1Unorm, 8, 8) == 32u);
        // SRGB variant has identical block bytes to UNORM (only sampler decode differs).
        assert(bytesForTextureMip(TextureFormat::Bc7UnormSrgb, 8, 8)
            == bytesForTextureMip(TextureFormat::Bc7Unorm, 8, 8));
        // Uncompressed still works through the same helper: 8x8 RGBA8 = 256 bytes.
        assert(bytesForTextureMip(TextureFormat::Rgba8Unorm, 8, 8) == 256u);
        assert(bytesForTextureMip(TextureFormat::Rgba8UnormSrgb, 8, 8) == 256u);
        std::printf("[ok] Facet 2: bytesForTextureMip edge cases (1x1, 5x5, BC1 vs BC7)\n");
    }

    // === Facet 3: estimateTexturePlacementRequirements mip chain sum ===
    {
        // 8x8 BC7 with 4 mip levels (8x8, 4x4, 2x2, 1x1):
        //   8x8  = 4 blocks * 16 = 64
        //   4x4  = 1 block  * 16 = 16
        //   2x2  = 1 block  * 16 = 16  (ceil(2/4)=1)
        //   1x1  = 1 block  * 16 = 16
        //   total = 112 bytes
        TextureDesc desc;
        desc.extent = {8, 8, 1u};
        desc.format = TextureFormat::Bc7Unorm;
        desc.mipLevelCount = 4u;
        desc.arrayLayerCount = 1u;
        const auto req = estimateTexturePlacementRequirements(desc);
        assert(req.sizeBytes == 112u);
        // Alignment floors to 256 (block byte size 16 < 256).
        assert(req.alignmentBytes == 256u);
        std::printf("[ok] Facet 3: BC7 mip chain placement estimate = %llu bytes\n",
            static_cast<unsigned long long>(req.sizeBytes));
    }

    // === Facet 4: Null device createTexture(BC7, Sampled|CopyDst, mips) ===
    // Mirrors the wocc uploadTextureFromMippedImage TextureDesc shape.
    {
        auto device = createNullDevice();
        assert(device);

        // BC7 UNORM, 1024x1024, full mip chain (11 levels: 1024..1).
        TextureDesc desc;
        desc.label = "WoccBc7TestUnorm";
        desc.extent = {1024, 1024, 1u};
        desc.format = TextureFormat::Bc7Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        desc.memory = MemoryUsage::GpuOnly;
        desc.initialState = ResourceState::Undefined;
        desc.mipLevelCount = 11u; // 1024 -> 1
        desc.arrayLayerCount = 1u;
        desc.cubeCompatible = false;

        auto texture = device->createTexture(desc);
        assert(texture);
        assert(texture->id().isValid());
        assert(texture->desc().format == TextureFormat::Bc7Unorm);
        assert(texture->desc().mipLevelCount == 11u);
        // Base level (1024x1024) BC7 = (256*256)*16 = 1048576 bytes. The Null
        // estimateTextureSize only sizes the base level (not the full chain),
        // so we expect the base-level byte count here.
        const auto expectedBaseLevel = 256u * 256u * 16u;
        assert(texture->allocation().estimatedSizeBytes == expectedBaseLevel);

        // SRGB variant: identical block bytes, different enum.
        TextureDesc srgbDesc = desc;
        srgbDesc.label = "WoccBc7TestSrgb";
        srgbDesc.format = TextureFormat::Bc7UnormSrgb;
        auto srgbTex = device->createTexture(srgbDesc);
        assert(srgbTex);
        assert(srgbTex->desc().format == TextureFormat::Bc7UnormSrgb);
        assert(srgbTex->allocation().estimatedSizeBytes == expectedBaseLevel);
        std::printf("[ok] Facet 4: Null createTexture(BC7 UNORM+SRGB, 11 mips): size=%llu\n",
            static_cast<unsigned long long>(texture->allocation().estimatedSizeBytes));
    }

    // === Facet 5: TextureView on a BC7 texture ===
    {
        auto device = createNullDevice();
        assert(device);

        TextureDesc desc;
        desc.label = "WoccBc7ViewTest";
        desc.extent = {64, 64, 1u};
        desc.format = TextureFormat::Bc7Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        desc.memory = MemoryUsage::GpuOnly;
        desc.initialState = ResourceState::Undefined;
        desc.mipLevelCount =
            4u; // partial chain (64,32,16,8) -- full chain not required for view test
        desc.arrayLayerCount = 1u;

        auto texture = device->createTexture(desc);
        assert(texture);

        TextureViewDesc viewDesc;
        viewDesc.format = TextureFormat::Bc7Unorm;
        viewDesc.usage = TextureUsage::Sampled;
        viewDesc.dimension = TextureViewDimension::Texture2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = texture->desc().mipLevelCount;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = TextureAspect::Color;
        auto view = device->createTextureView(*texture, viewDesc);
        assert(view);
        assert(view->id().isValid());
        std::printf("[ok] Facet 5: TextureView on BC7 texture succeeds\n");
    }

    std::printf("[ok] rhi_texture_format_bc7_contract_tests: all facets passed\n");
    return 0;
}
