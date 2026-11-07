#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_pipeline_archive.h"

#include <cassert>
#include <cstdio>
#include <string_view>

int main(int argc, char** argv)
{
    @autoreleasepool {
        auto device = reinterpret_cast<id<MTLDevice>>(arp::rhi_metal::metalRhiNativeDeviceHandle());
        assert(device);
        NSError* error = nil;
        NSString* source =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "vertex float4 vsMain(uint id [[vertex_id]]) { return float4(float(id),0,0,1); }\n"
             "fragment float4 fsMain() { return float4(1); }\n"
             "kernel void csMain(uint id [[thread_position_in_grid]]) {}\n";
        auto library = [device newLibraryWithSource:source options:nil error:&error];
        assert(library);
        auto vertex = [library newFunctionWithName:@"vsMain"];
        auto fragment = [library newFunctionWithName:@"fsMain"];
        auto compute = [library newFunctionWithName:@"csMain"];
        assert(vertex && fragment && compute);
        auto descriptor = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        descriptor.vertexFunction = vertex;
        descriptor.fragmentFunction = fragment;
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        descriptor.rasterSampleCount = 1;
        descriptor.alphaToCoverageEnabled = NO;
        descriptor.alphaToOneEnabled = NO;
        descriptor.rasterizationEnabled = YES;
        descriptor.colorAttachments[0].blendingEnabled = NO;
        descriptor.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
        MTLRenderPipelineReflection* renderReflection = nil;
        MTLComputePipelineReflection* computeReflection = nil;
        const auto options = MTLPipelineOptionArgumentInfo | MTLPipelineOptionBufferTypeInfo;
        auto render = arp::rhi_metal::newMetalRhiCachedRenderPipeline(
            descriptor, options, &renderReflection, &error);
        auto kernel = arp::rhi_metal::newMetalRhiCachedComputePipeline(
            compute, options, &computeReflection, &error);
        assert(render && kernel && renderReflection && computeReflection);
        auto report = arp::rhi_metal::describeMetalRhiPipelineArchive();
        if (@available(macOS 11.0, iOS 14.0, *)) {
            assert(std::string_view(report.status) == "NativeBinaryArchive" && report.uses == 2);
            if (argc > 1 && std::string_view(argv[1]) == "warm")
                assert(
                    std::string_view(report.input) == "CompatibleLoaded" && report.loadedBytes > 0);
            else
                assert(report.loadedBytes == 0);
        } else
            assert(std::string_view(report.status) == "EquivalentUncached:ArchiveApiUnavailable");
        [render release];
        [kernel release];
        [vertex release];
        [fragment release];
        [compute release];
        [library release];
    }
    std::puts("Metal native render/compute archive: PASS");
}
