// Contract tests for the shared Metal nativePixelFormat(rhi::TextureFormat)
// mapping. Previously this mapping was duplicated verbatim across
// metal_native_resources.mm, metal_native_pipelines.mm and
// metal_native_swapchain.mm. It is now defined once in metal_native_resources.mm
// and declared in metal_native_format.h.
//
// This test only compiles and runs on Apple platforms (it returns 0/skip on
// non-Apple builds) because it depends on <Metal/Metal.h>. On Apple it locks
// the per-format MTLPixelFormat contract so any future format addition or
// mapping change must update the single source of truth and this contract
// test together. Status: Prototype Complete — Apple-platform validation
// required before Backend Validated.

#include "rhi/backends/metal/metal_native_format.h"

#include <cstdio>
#include <cstdlib>

#if defined(__APPLE__)

namespace {

int failures = 0;

void requireEqual(MTLPixelFormat actual, MTLPixelFormat expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL %s: expected %d, got %d\n",
            label,
            static_cast<int>(expected),
            static_cast<int>(actual));
        ++failures;
    } else {
        std::printf("ok   %s: %d\n", label, static_cast<int>(actual));
    }
}

} // namespace

#endif

int main()
{
#if !defined(__APPLE__)
    // Non-Apple build: metal_native_format.h would have #error'd at include
    // time, but the CMake stub path excludes this translation unit from
    // non-Apple targets. If it ever does get built here, skip gracefully.
    std::printf("skipped metal_pixel_format_mapping on non-Apple platform\n");
    return EXIT_SUCCESS;
#else
    using arp::rhi_metal::nativePixelFormat;

    requireEqual(
        nativePixelFormat(rhi::TextureFormat::Bgra8Unorm), MTLPixelFormatBGRA8Unorm, "Bgra8Unorm");
    requireEqual(
        nativePixelFormat(rhi::TextureFormat::Rgba8Unorm), MTLPixelFormatRGBA8Unorm, "Rgba8Unorm");
    requireEqual(nativePixelFormat(rhi::TextureFormat::Rgba16Float),
        MTLPixelFormatRGBA16Float,
        "Rgba16Float");
    requireEqual(
        nativePixelFormat(rhi::TextureFormat::R32Float), MTLPixelFormatR32Float, "R32Float");
    requireEqual(
        nativePixelFormat(rhi::TextureFormat::D32Float), MTLPixelFormatDepth32Float, "D32Float");
    requireEqual(nativePixelFormat(rhi::TextureFormat::Depth24Stencil8),
        MTLPixelFormatDepth24Unorm_Stencil8,
        "Depth24Stencil8");
    requireEqual(nativePixelFormat(rhi::TextureFormat::Unknown), MTLPixelFormatInvalid, "Unknown");

    if (failures != 0) {
        std::printf("%d assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all metal pixel format mapping contract tests passed\n");
    return EXIT_SUCCESS;
#endif
}
