#include "rhi/backends/metal/metal_native_features.h"

#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_native_queries.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_native_secondary.h"
#include "rhi/backends/metal/metal_native_sync.h"

#include <Metal/Metal.h>

namespace arp::rhi_metal {

namespace {

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

id<MTLLibrary> compileLibrary(id<MTLDevice> device, NSString* source)
{
    if (device == nil || source == nil)
        return nil;
    NSError* error = nil;
    return [device newLibraryWithSource:source options:nil error:&error];
}

bool framebufferFetchPipelineProbe(id<MTLDevice> device, NSString* fragmentSource, bool depthFetch)
{
    if (device == nil || fragmentSource == nil) {
        return false;
    }
    NSString* source = [[NSString alloc]
        initWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\n"
                        "struct ProbeVertex { float4 position [[position]]; };\n"
                        "vertex ProbeVertex probeVertex(uint vertexId [[vertex_id]]) { "
                        "float4 positions[3] = { float4(-1.0, -1.0, 0.0, 1.0), "
                        "float4(3.0, -1.0, 0.0, 1.0), float4(-1.0, 3.0, 0.0, 1.0) }; "
                        "ProbeVertex result; result.position = positions[vertexId %% 3u]; "
                        "return result; }\n%@",
        fragmentSource];
    id<MTLLibrary> library = compileLibrary(device, source);
    [source release];
    id<MTLFunction> vertexFunction =
        library != nil ? [library newFunctionWithName:@"probeVertex"] : nil;
    id<MTLFunction> fragmentFunction =
        library != nil ? [library newFunctionWithName:@"probeFragment"] : nil;
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = vertexFunction;
    descriptor.fragmentFunction = fragmentFunction;
    descriptor.sampleCount = 1u;
    if (depthFetch) {
        descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    } else {
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    }
    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline = (vertexFunction != nil && fragmentFunction != nil)
        ? [device newRenderPipelineStateWithDescriptor:descriptor error:&error]
        : nil;
    const bool result = pipeline != nil;
    [pipeline release];
    [descriptor release];
    [fragmentFunction release];
    [vertexFunction release];
    [library release];
    return result;
}

bool storageTextureFormatProbe(id<MTLDevice> device, MTLPixelFormat format, NSString* typeName)
{
    MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = format;
    descriptor.width = 1u;
    descriptor.height = 1u;
    descriptor.depth = 1u;
    descriptor.mipmapLevelCount = 1u;
    descriptor.arrayLength = 1u;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    [descriptor release];
    if (texture == nil)
        return false;
    NSString* value = [typeName isEqualToString:@"half2"] ? @"half2(0.0h)" : @"half4(0.0h)";
    NSString* source = [[NSString alloc]
        initWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\n"
                        "kernel void probe(texture2d<%@, access::write> out [[texture(0)]], "
                        "uint3 tid [[thread_position_in_grid]]) { if (tid.x == 0 && tid.y == 0) "
                        "out.write(%@, uint2(0)); }",
        typeName,
        value];
    id<MTLLibrary> library = compileLibrary(device, source);
    [source release];
    id<MTLFunction> function = library != nil ? [library newFunctionWithName:@"probe"] : nil;
    NSError* error = nil;
    id<MTLComputePipelineState> pipeline =
        function != nil ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
    const bool result = pipeline != nil;
    [pipeline release];
    [function release];
    [library release];
    [texture release];
    return result;
}

bool softwareBvhProbe(id<MTLDevice> device)
{
    NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                        "struct BvhNode { uint leftChild; uint rightChild; };\n"
                        "struct BvhLeaf { float3 v0; float3 v1; float3 v2; };\n"
                        "kernel void probe(device const BvhNode* nodes [[buffer(0)]], "
                        "device const BvhLeaf* leaves [[buffer(1)]], "
                        "uint3 tid [[thread_position_in_grid]]) { if (tid.x == 0) "
                        "{ uint value = nodes[0].leftChild; (void)leaves; (void)value; } }";
    id<MTLLibrary> library = compileLibrary(device, source);
    id<MTLFunction> function = library != nil ? [library newFunctionWithName:@"probe"] : nil;
    NSError* error = nil;
    id<MTLComputePipelineState> pipeline =
        function != nil ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
    id<MTLBuffer> nodes = pipeline != nil
        ? [device newBufferWithLength:16 options:MTLResourceStorageModePrivate]
        : nil;
    id<MTLBuffer> leaves = pipeline != nil
        ? [device newBufferWithLength:64 options:MTLResourceStorageModePrivate]
        : nil;
    const bool result = pipeline != nil && nodes != nil && leaves != nil;
    [leaves release];
    [nodes release];
    [pipeline release];
    [function release];
    [library release];
    return result;
}

bool memorylessTextureProbe(id<MTLDevice> device)
{
    if (device == nil || !@available(macOS 10.13, iOS 10.0, *))
        return false;
    MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.width = 4u;
    descriptor.height = 4u;
    descriptor.mipmapLevelCount = 1u;
    descriptor.sampleCount = 1u;
    descriptor.arrayLength = 1u;
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModeMemoryless;
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    const bool result = texture != nil;
    [texture release];
    [descriptor release];
    return result;
}

bool bcTextureProbe(id<MTLDevice> device)
{
    if (device == nil || !@available(macOS 10.13, iOS 11.0, *))
        return false;
    if (!device.supportsBCTextureCompression)
        return false;

    NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                        "struct ProbeVertex { float4 position [[position]]; };\n"
                        "vertex ProbeVertex probeVertex(uint vertexId [[vertex_id]]) { "
                        "float4 positions[3] = { float4(-1.0, -1.0, 0.0, 1.0), "
                        "float4(3.0, -1.0, 0.0, 1.0), float4(-1.0, 3.0, 0.0, 1.0) }; "
                        "ProbeVertex result; result.position = positions[vertexId % 3u]; "
                        "return result; }\n"
                        "fragment float4 probeFragment(texture2d<float> source [[texture(0)]], "
                        "sampler sourceSampler [[sampler(0)]], float4 position [[position]]) "
                        "{ return source.sample(sourceSampler, position.xy / 4.0); }";
    id<MTLLibrary> library = compileLibrary(device, source);
    id<MTLFunction> function =
        library != nil ? [library newFunctionWithName:@"probeFragment"] : nil;
    id<MTLFunction> vertexFunction =
        library != nil ? [library newFunctionWithName:@"probeVertex"] : nil;
    if (function == nil || vertexFunction == nil) {
        [vertexFunction release];
        [function release];
        [library release];
        return false;
    }
    MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = function;
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    NSError* pipelineError = nil;
    id<MTLRenderPipelineState> pipeline =
        [device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&pipelineError];
    [pipelineDescriptor release];
    [vertexFunction release];
    [function release];
    [library release];
    if (pipeline == nil)
        return false;

    MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
    samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
    samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
    id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:samplerDescriptor];
    [samplerDescriptor release];
    if (sampler == nil) {
        [pipeline release];
        return false;
    }

    const MTLPixelFormat formats[] = {
        MTLPixelFormatBC1_RGBA,
        MTLPixelFormatBC3_RGBA,
        MTLPixelFormatBC7_RGBAUnorm,
        MTLPixelFormatBC7_RGBAUnorm_sRGB,
    };
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (queue == nil) {
        [sampler release];
        [pipeline release];
        return false;
    }
    for (const auto format : formats) {
        MTLTextureDescriptor* sourceDescriptor = [[MTLTextureDescriptor alloc] init];
        sourceDescriptor.textureType = MTLTextureType2D;
        sourceDescriptor.pixelFormat = format;
        sourceDescriptor.width = 4u;
        sourceDescriptor.height = 4u;
        sourceDescriptor.depth = 1u;
        sourceDescriptor.mipmapLevelCount = 1u;
        sourceDescriptor.arrayLength = 1u;
        sourceDescriptor.usage = MTLTextureUsageShaderRead;
        sourceDescriptor.storageMode = MTLStorageModePrivate;
        MTLTextureDescriptor* destinationDescriptor = [[MTLTextureDescriptor alloc] init];
        destinationDescriptor.textureType = MTLTextureType2D;
        destinationDescriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
        destinationDescriptor.width = 4u;
        destinationDescriptor.height = 4u;
        destinationDescriptor.depth = 1u;
        destinationDescriptor.mipmapLevelCount = 1u;
        destinationDescriptor.arrayLength = 1u;
        destinationDescriptor.usage = MTLTextureUsageRenderTarget;
        destinationDescriptor.storageMode = MTLStorageModePrivate;
        id<MTLTexture> sourceTexture = [device newTextureWithDescriptor:sourceDescriptor];
        id<MTLTexture> destinationTexture = [device newTextureWithDescriptor:destinationDescriptor];
        [destinationDescriptor release];
        [sourceDescriptor release];
        bool sampled = false;
        if (sourceTexture != nil && destinationTexture != nil) {
            id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
            MTLRenderPassDescriptor* renderPass = [[MTLRenderPassDescriptor alloc] init];
            renderPass.colorAttachments[0].texture = destinationTexture;
            renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
            renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
            renderPass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            id<MTLRenderCommandEncoder> encoder = commandBuffer != nil
                ? [commandBuffer renderCommandEncoderWithDescriptor:renderPass]
                : nil;
            if (encoder != nil) {
                [encoder setRenderPipelineState:pipeline];
                [encoder setFragmentTexture:sourceTexture atIndex:0u];
                [encoder setFragmentSamplerState:sampler atIndex:0u];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0u vertexCount:3u];
                [encoder endEncoding];
                [commandBuffer commit];
                [commandBuffer waitUntilCompleted];
                sampled = commandBuffer.status == MTLCommandBufferStatusCompleted;
            }
            [renderPass release];
        }
        [destinationTexture release];
        [sourceTexture release];
        if (!sampled) {
            [queue release];
            [sampler release];
            [pipeline release];
            return false;
        }
    }
    [queue release];
    [sampler release];
    [pipeline release];
    return true;
}

} // namespace

MetalRhiNativeFeatureProbe probeMetalRhiNativeFeatures()
{
    static const MetalRhiNativeFeatureProbe result = [] {
        MetalRhiNativeFeatureProbe probe;
        id<MTLDevice> device = defaultMetalDevice();
        if (device == nil)
            return probe;

        probe.argumentBuffers = metalRhiNativeArgumentBuffersAvailable();
        if (@available(macOS 10.13, iOS 11.0, *)) {
            probe.argumentBuffersTier2 = device.argumentBuffersSupport == MTLArgumentBuffersTier2;
        }
        probe.placementAliasing = metalRhiNativePlacementAliasingAvailable();
        probe.secondaryCommandBuffers = metalRhiNativeSecondaryCommandBuffersAvailable();
        probe.indirectCommandBuffers = probe.secondaryCommandBuffers;
        probe.timelineSynchronization = metalRhiNativeTimelineSynchronizationAvailable();
        probe.nativeSync = probe.timelineSynchronization;
        probe.memorylessTransientTextures = memorylessTextureProbe(device);
        probe.textureCompressionBc = bcTextureProbe(device);
        // R32SampledUpload/1 needs no filtering or storage-write feature.
        // Metal's core blit copy supports a private shader-readable texture;
        // failure to create that exact native representation stays closed.
        MTLTextureDescriptor* r32Desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                               width:1
                                                              height:1
                                                           mipmapped:NO];
        r32Desc.storageMode = MTLStorageModePrivate;
        r32Desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> r32Texture = [device newTextureWithDescriptor:r32Desc];
        probe.r32FloatSampledUpload = r32Texture != nil;
        [r32Texture release];
        probe.timestampQueries = metalRhiNativeTimestampQueriesAvailable();
        probe.pipelineStatisticsQueries = metalRhiNativePipelineStatisticsQueriesAvailable();
        // Metal's texture2d<T> template takes the scalar channel type; the
        // vector width is selected by the pixel format and by write()'s
        // value. Passing half2/half4 probes an invalid MSL type and falsely
        // disables both storage formats on Apple GPUs.
        probe.rg16FloatStorageTexture =
            storageTextureFormatProbe(device, MTLPixelFormatRG16Float, @"half");
        probe.rgba16FloatStorageTexture =
            storageTextureFormatProbe(device, MTLPixelFormatRGBA16Float, @"half");
        probe.sceneGeometryRayQuery = softwareBvhProbe(device);
        MTLTextureDescriptor* depthDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:4
                                                              height:4
                                                           mipmapped:NO];
        depthDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        probe.sampledDepthTexture = [device newTextureWithDescriptor:depthDesc] != nil;
        const MTLSize workgroupSize = device.maxThreadsPerThreadgroup;
        probe.maxComputeWorkgroupInvocations = static_cast<std::uint32_t>(
            workgroupSize.width * workgroupSize.height * workgroupSize.depth);
        probe.maxComputeWorkgroupSizeX = static_cast<std::uint32_t>(workgroupSize.width);
        probe.maxComputeWorkgroupSizeY = static_cast<std::uint32_t>(workgroupSize.height);
        probe.maxComputeWorkgroupSizeZ = static_cast<std::uint32_t>(workgroupSize.depth);
        probe.maxComputeWorkgroupMemoryBytes =
            static_cast<std::uint32_t>(device.maxThreadgroupMemoryLength);
        // Metal guarantees 31 buffer argument slots per shader stage. This is
        // a specification limit, not a platform-name heuristic.
        probe.maxStorageBufferBindingsPerStage = 31u;
        probe.maxStorageBufferBindingSizeBytes = static_cast<std::uint64_t>(device.maxBufferLength);
        probe.maxUniformBufferBindingsPerStage = 31u;
        probe.maxUniformBufferBindingSizeBytes = static_cast<std::uint64_t>(device.maxBufferLength);
        // Apple feature tables (2026-05-21), p7: every listed Apple family
        // exposes at least 31 direct texture argument entries per function.
        probe.maxSampledTextureBindingsPerStage = 31u;
        // Apple Metal feature tables, 2026-05-21 footnote1: every Metal3
        // device guarantees at least Apple7's limits. These are conservative
        // ceilings, not a guessed hardware maximum. Physical layers include
        // cube faces; do not multiply the common layer bound by six.
#if defined(__MAC_13_0) || defined(__IPHONE_16_0) || defined(__TVOS_16_0)
        if (@available(macOS 13.0, iOS 16.0, tvOS 16.0, *))
            if ([device supportsFamily:MTLGPUFamilyMetal3]) {
                probe.maxTexture2DDimension = 16384u;
                probe.maxTexture3DDimension = 2048u;
                probe.maxTextureArrayLayers = 2048u;
            }
#endif
        // Metal's core function argument limits guarantee these conservative
        // combined texture/sampler ceilings; Tier2 arrays use no larger claim.
        probe.maxTextureBindingsPerStage = 128u;
        probe.maxSamplerBindingsPerStage = 16u;
        if (@available(macOS 10.13, iOS 11.0, *))
            probe.r32FloatReadWriteTexture =
                device.readWriteTextureSupport >= MTLReadWriteTextureTier1;

        // Color framebuffer fetch and depth framebuffer fetch are separate
        // shader contracts. Compile the functions and create the corresponding
        // render pipeline so source-level MSL support cannot be mistaken for
        // an executable pipeline lowering.
        probe.framebufferFetch = framebufferFetchPipelineProbe(device,
            @"fragment float4 probeFragment(float4 current [[color(0)]]) "
             "{ return current; }",
            false);
        probe.framebufferFetchDepth = framebufferFetchPipelineProbe(device,
            @"fragment float probeFragment(float current [[depth(any)]]) "
             "{ return current; }",
            true);
        return probe;
    }();
    return result;
}

} // namespace arp::rhi_metal
