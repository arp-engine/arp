#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/gpu_device.h"

#include <Metal/Metal.h>
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Metal texture usage fixture requires active assertions
#endif

void verifyMetalStorageTextureUsage(arp::rhi::GpuDevice& device)
{
    using namespace arp::rhi;
    using namespace arp::rhi_metal;
    for (const auto format : {TextureFormat::R32Float, TextureFormat::Rgba16Float}) {
        for (const auto usage : {TextureUsage::Storage,
                 TextureUsage::Sampled,
                 TextureUsage::Storage | TextureUsage::Sampled}) {
            auto texture = device.createTexture({"StorageUsageFixture",
                {4u, 4u, 1u},
                format,
                usage,
                MemoryUsage::GpuOnly,
                ResourceState::Undefined});
            assert(texture);
            const auto payload = metalRhiNativePayloadFor(*texture);
            assert(payload && payload->resource.nativeRuntimeBacked);
            id<MTLTexture> native = (id<MTLTexture>)payload->resource.nativeObject.get();
            const bool storage = (usage & TextureUsage::Storage) != TextureUsage::None;
            const auto expected = storage ? MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite
                                          : MTLTextureUsageShaderRead;
            std::fprintf(stderr,
                "native texture usage format=%u logical=%u actual=%lu expected=%lu\n",
                unsigned(format),
                unsigned(usage),
                (unsigned long)native.usage,
                (unsigned long)expected);
            assert(native.usage == expected);
            auto view = device.createTextureView(*texture,
                {"UsageView", format, storage ? TextureUsage::Storage : TextureUsage::Sampled});
            assert(view);
            const auto viewPayload = metalRhiNativePayloadFor(*view);
            assert(viewPayload && viewPayload->resource.nativeRuntimeBacked);
            id<MTLTexture> nativeView = (id<MTLTexture>)viewPayload->resource.nativeObject.get();
            assert(nativeView.usage == expected);
            if (usage == TextureUsage::Storage) {
                assert(!device.createTextureView(
                    *texture, {"UnauthorizedSampledView", format, TextureUsage::Sampled}));
            } else if (usage == TextureUsage::Sampled) {
                assert(!device.createTextureView(
                    *texture, {"UnauthorizedStorageView", format, TextureUsage::Storage}));
            }
        }
    }
}
