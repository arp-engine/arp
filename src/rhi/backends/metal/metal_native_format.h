#pragma once

// Metal-native texture format mapping. Declared in a dedicated header (rather
// than metal_native_resources.h) because it returns MTLPixelFormat, which
// requires <Metal/Metal.h>. The metal_backend_objects.h umbrella header is
// included by non-Apple compilation units (stubs), so it must stay free of
// Metal framework types. This header is only included by .mm translation units
// that already import <Metal/Metal.h>.
//
// Previously nativePixelFormat(rhi::TextureFormat) -> MTLPixelFormat was
// duplicated verbatim across metal_native_resources.mm, metal_native_pipelines.mm
// and metal_native_swapchain.mm. It is now defined once (in metal_native_resources.mm)
// and shared by the other two, satisfying the "no duplicated utility functions
// in backend paths" constraint.

#include "rhi/gpu_common.h"
#include "rhi/gpu_pipeline.h"

#if defined(__APPLE__)
#include <Metal/Metal.h>
#else
// Non-Apple translation units must not include this header. Provide a clear
// diagnostic if they do, rather than letting the MTLPixelFormat reference fail
// with an opaque error.
#error                                                                                             \
    "metal_native_format.h must only be included by Metal (.mm) translation units on Apple platforms."
#endif

namespace arp::rhi_metal {

// Maps an rhi::TextureFormat to its MTLPixelFormat equivalent. Returns
// MTLPixelFormatInvalid for Unknown / unrecognized formats. Used by native
// resource creation, native pipeline configuration and native swapchain layer
// configuration — all three paths must agree on a single mapping.
MTLPixelFormat nativePixelFormat(rhi::TextureFormat format) noexcept;

// Backend-private vertex mapping; invalid L2 values stay invalid.
MTLVertexFormat metalVertexFormat(rhi::VertexFormat format);

} // namespace arp::rhi_metal
