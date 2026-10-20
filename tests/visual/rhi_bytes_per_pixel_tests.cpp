// Contract tests for the shared rhi::bytesPerPixel(TextureFormat) helper.
//
// This function was previously duplicated verbatim across 5 sites:
//   - src/rhi/backends/vulkan/vulkan_resources.cpp
//   - src/rhi/backends/metal/metal_backend_objects.cpp
//   - src/rhi/backends/webgpu/webgpu_backend_objects.cpp
//   - src/rhi/null_resources.cpp
//   - src/rhi/backends/vulkan/vulkan_allocator.cpp (inlined in estimatedTextureBytes)
// It was consolidated into src/rhi/gpu_common.h. These golden-value tests
// lock the per-format byte size contract shared by all backends, so any
// future format addition or byte-size change must update this single source
// of truth and its contract test together.

#include "rhi/gpu_common.h"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void requireEqual(std::uint64_t actual, std::uint64_t expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL %s: expected %llu, got %llu\n",
            label,
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(actual));
        ++failures;
    } else {
        std::printf("ok   %s: %llu\n", label, static_cast<unsigned long long>(actual));
    }
}

} // namespace

int main()
{
    using arp::rhi::bytesPerPixel;
    using arp::rhi::TextureFormat;

    // 4-byte-per-pixel formats (color, single-channel float, depth, depth-stencil).
    requireEqual(bytesPerPixel(TextureFormat::Bgra8Unorm), 4, "Bgra8Unorm");
    requireEqual(bytesPerPixel(TextureFormat::Rgba8Unorm), 4, "Rgba8Unorm");
    requireEqual(bytesPerPixel(TextureFormat::R32Float), 4, "R32Float");
    requireEqual(bytesPerPixel(TextureFormat::D32Float), 4, "D32Float");
    requireEqual(bytesPerPixel(TextureFormat::Depth24Stencil8), 4, "Depth24Stencil8");

    // 8-byte-per-pixel format (half-precision RGBA).
    requireEqual(bytesPerPixel(TextureFormat::Rgba16Float), 8, "Rgba16Float");

    // Unknown / sentinel format must report zero so size estimators short-circuit.
    requireEqual(bytesPerPixel(TextureFormat::Unknown), 0, "Unknown");

    if (failures != 0) {
        std::printf("%d assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all bytesPerPixel contract tests passed\n");
    return EXIT_SUCCESS;
}
