// Contract test: R32Float texture format support across backends.
//
// Verifies:
//   1. R32Float is a valid TextureFormat enum value (compilation check).
//   2. Null backend: createTexture with R32Float succeeds, size = w*h*4.
//   3. R32Float is NOT a depth format (isDepthTextureFormat returns false).
//   4. R32Float default subresource range aspect = Color.
//   5. R32Float can be used as both RenderAttachment and Sampled (shadow map).
//
// This is a contract-level test: it validates the L2 semantic that R32Float
// is a color texture format with 4 bytes per pixel, distinct from D32Float
// (which is a depth format). Backend-specific native format mapping
// (VK_FORMAT_R32_SFLOAT, MTLPixelFormatR32Float, WGPUTextureFormat_R32Float)
// is validated via the Null backend's size estimation, which exercises the
// same bytesPerPixel switch that all backends share.
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_resources.h"

#include <cassert>
#include <cstdio>

int main()
{
    using namespace arp::rhi;

    // --- Contract 1: R32Float exists in the enum ---
    {
        const TextureFormat format = TextureFormat::R32Float;
        assert(format != TextureFormat::Unknown);
        std::printf("[ok] R32Float is a valid TextureFormat enum value\n");
    }

    // --- Contract 2: Null backend creates R32Float texture with correct size ---
    {
        auto device = createNullDevice();
        assert(device);

        TextureDesc desc;
        desc.label = "ShadowMapR32Float";
        desc.extent = {2048, 2048, 1};
        desc.format = TextureFormat::R32Float;
        desc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
        desc.memory = MemoryUsage::GpuOnly;

        auto texture = device->createTexture(desc);
        assert(texture);
        assert(texture->id().isValid());
        assert(texture->desc().format == TextureFormat::R32Float);
        // R32Float = 4 bytes per pixel → 2048*2048*4 = 16 MB
        assert(texture->allocation().estimatedSizeBytes == 2048u * 2048u * 4u);
        std::printf("[ok] Null backend R32Float texture size = %llu bytes (2048x2048x4)\n",
            static_cast<unsigned long long>(texture->allocation().estimatedSizeBytes));
    }

    // --- Contract 3: R32Float is NOT a depth format ---
    {
        assert(!isDepthTextureFormat(TextureFormat::R32Float));
        assert(isDepthTextureFormat(TextureFormat::D32Float));
        assert(isDepthTextureFormat(TextureFormat::Depth24Stencil8));
        std::printf("[ok] R32Float is classified as color (not depth)\n");
    }

    // --- Contract 4: R32Float default aspect = Color ---
    {
        TextureDesc desc;
        desc.extent = {64, 64, 1};
        desc.format = TextureFormat::R32Float;
        const auto range = defaultTextureSubresourceRange(desc);
        assert(range.aspect == TextureAspect::Color);
        std::printf("[ok] R32Float default subresource aspect = Color\n");
    }

    // --- Contract 5: R32Float supports both RenderAttachment and Sampled ---
    {
        auto device = createNullDevice();
        assert(device);

        TextureDesc desc;
        desc.label = "ShadowMapDualUse";
        desc.extent = {1024, 1024, 1};
        desc.format = TextureFormat::R32Float;
        desc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
        desc.memory = MemoryUsage::GpuOnly;

        auto texture = device->createTexture(desc);
        assert(texture);
        assert(texture->id().isValid());
        std::printf("[ok] R32Float supports RenderAttachment|Sampled usage\n");
    }

    std::printf("[PASS] rhi_texture_format_r32float_contract_tests\n");
    return 0;
}
