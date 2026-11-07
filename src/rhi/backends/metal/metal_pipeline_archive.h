#pragma once

#if !defined(__OBJC__)
#error "Metal pipeline archive bridge is private to Objective-C++ translation units"
#endif

// Backend-private Objective-C++ boundary. No native type enters L2 or a game.
#include <Metal/Metal.h>

namespace arp::rhi_metal {
id<MTLRenderPipelineState> newMetalRhiCachedRenderPipeline(
    MTLRenderPipelineDescriptor*, MTLPipelineOption, MTLRenderPipelineReflection**, NSError**);
id<MTLComputePipelineState> newMetalRhiCachedComputePipeline(
    id<MTLFunction>, MTLPipelineOption, MTLComputePipelineReflection**, NSError**);
} // namespace arp::rhi_metal
