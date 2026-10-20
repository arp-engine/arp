#include "rhi/backends/metal/metal_native_secondary.h"

#include "rhi/backends/metal/metal_native_runtime.h"

#include <Metal/Metal.h>
#include <algorithm>
#include <limits>

namespace arp::rhi_metal {

namespace {

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

std::shared_ptr<void> retainNativeObject(id object)
{
    if (object == nil)
        return {};
    return std::shared_ptr<void>(reinterpret_cast<void*>(object), [](void* nativeObject) {
        if (nativeObject)
            [reinterpret_cast<id>(nativeObject) release];
    });
}

std::shared_ptr<void> retainBorrowedNativeObject(id object)
{
    if (object == nil)
        return {};
    [object retain];
    return retainNativeObject(object);
}

bool tryNativePrimitiveType(rhi::PrimitiveTopology topology, MTLPrimitiveType& result)
{
    switch (topology) {
    case rhi::PrimitiveTopology::TriangleList:
        result = MTLPrimitiveTypeTriangle;
        return true;
    case rhi::PrimitiveTopology::TriangleStrip:
        result = MTLPrimitiveTypeTriangleStrip;
        return true;
    case rhi::PrimitiveTopology::LineList:
        result = MTLPrimitiveTypeLine;
        return true;
    case rhi::PrimitiveTopology::PointList:
        result = MTLPrimitiveTypePoint;
        return true;
    case rhi::PrimitiveTopology::PatchList:
        return false;
    }
    return false;
}

MTLIndexType nativeIndexType(rhi::IndexFormat format)
{
    return format == rhi::IndexFormat::Uint16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

id<MTLIndirectCommandBuffer> nativeBuffer(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload)
{
    return payload && payload->nativeIndirectCommandBuffer
        ? reinterpret_cast<id<MTLIndirectCommandBuffer>>(payload->nativeIndirectCommandBuffer.get())
        : nil;
}

bool reserveCommand(const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::pmr::string& diagnostics)
{
    if (!payload || !payload->nativeRuntimeBacked || !nativeBuffer(payload)) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-PAYLOAD-MISSING";
        return false;
    }
    if (payload->commandCount >= payload->maxCommandCount) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-CAPACITY-EXCEEDED";
        return false;
    }
    return true;
}

bool validFactorBufferBinding(const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& factorBuffer,
    std::uint64_t offsetBytes,
    std::uint32_t instanceStrideBytes,
    std::pmr::string& diagnostics)
{
    id<MTLBuffer> nativeFactor = factorBuffer && factorBuffer->resource.nativeObject
        ? reinterpret_cast<id<MTLBuffer>>(factorBuffer->resource.nativeObject.get())
        : nil;
    if (!payload || payload->primitiveTopology != rhi::PrimitiveTopology::PatchList
        || payload->patchControlPointCount == 0u || !factorBuffer
        || !factorBuffer->resource.nativeRuntimeBacked || !payload->nativeDevice
        || !factorBuffer->resource.nativeDevice
        || payload->nativeDevice.get() != factorBuffer->resource.nativeDevice.get()
        || nativeFactor == nil || instanceStrideBytes == 0u || (offsetBytes & 3u) != 0u
        || (instanceStrideBytes & 3u) != 0u || offsetBytes > nativeFactor.length
        || nativeFactor.length - offsetBytes < sizeof(std::uint32_t)) {
        diagnostics = "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-RANGE-INVALID";
        return false;
    }
    if (payload->commandCount != 0u) {
        diagnostics = "RHI-METAL-SECONDARY-TESSELLATION-FACTOR-BUFFER-REBIND-AFTER-DRAW";
        return false;
    }
    return true;
}

} // namespace

std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> createMetalRhiNativeSecondaryCommandPayload(
    const std::pmr::string& label,
    rhi::PrimitiveTopology primitiveTopology,
    std::uint32_t maxCommandCount,
    bool allowCpuReplay,
    std::uint32_t patchControlPointCount)
{
    auto payload = arp::core::makeShared<MetalRhiNativeSecondaryCommandPayload>();
    payload->primitiveTopology = primitiveTopology;
    payload->patchControlPointCount = patchControlPointCount;
    if (payload->primitiveTopology == rhi::PrimitiveTopology::PatchList
        && payload->patchControlPointCount == 0u) {
        payload->diagnostics = "RHI-METAL-PATCH-LIST-CONTROL-POINT-COUNT-MISSING";
        return payload;
    }
    if (maxCommandCount == 0u) {
        payload->diagnostics = "RHI-METAL-SECONDARY-ICB-CAPACITY-INVALID";
        return payload;
    }
    payload->maxCommandCount = maxCommandCount;
    payload->cpuReplay = allowCpuReplay;
    if (allowCpuReplay) {
        // CPU replay is an explicit RHI lowering. It records no Metal ICB;
        // execution replays the command IR against the consuming encoder.
        payload->nativeRuntimeDiagnosticOnly = true;
        return payload;
    }
    if (!@available(macOS 10.13, iOS 11.0, *)) {
        payload->diagnostics = "RHI-METAL-SECONDARY-ICB-API-UNAVAILABLE";
        return payload;
    }
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil) {
        payload->diagnostics = "RHI-METAL-SECONDARY-ICB-FEATURE-UNAVAILABLE";
        return payload;
    }
    MTLIndirectCommandBufferDescriptor* descriptor =
        [[MTLIndirectCommandBufferDescriptor alloc] init];
    descriptor.commandTypes = MTLIndirectCommandTypeDraw | MTLIndirectCommandTypeDrawIndexed;
    descriptor.inheritPipelineState = YES;
    descriptor.inheritBuffers = YES;
    descriptor.maxVertexBufferBindCount = 31u;
    descriptor.maxFragmentBufferBindCount = 31u;
    descriptor.maxKernelBufferBindCount = 0u;
    id<MTLIndirectCommandBuffer> commandBuffer =
        [device newIndirectCommandBufferWithDescriptor:descriptor
                                       maxCommandCount:payload->maxCommandCount
                                               options:MTLResourceStorageModeShared];
    [descriptor release];
    if (commandBuffer == nil) {
        payload->diagnostics = "RHI-METAL-SECONDARY-ICB-CREATE-FAILED";
        return payload;
    }
    commandBuffer.label = [NSString stringWithUTF8String:label.c_str()];
    payload->nativeIndirectCommandBuffer = retainNativeObject(commandBuffer);
    payload->nativeDevice = metalRhiNativeDeviceIdentity();
    payload->nativeRuntimeBacked = true;
    payload->nativeRuntimeDiagnosticOnly = false;
    return payload;
}

bool recordMetalRhiNativeSecondaryTessellationFactorBufferBind(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& factorBuffer,
    std::uint64_t offsetBytes,
    std::uint32_t instanceStrideBytes,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.13, iOS 11.0, *)) {
        diagnostics = "RHI-METAL-PATCH-LIST-API-UNAVAILABLE";
        return false;
    }
    if (!validFactorBufferBinding(
            payload, factorBuffer, offsetBytes, instanceStrideBytes, diagnostics)) {
        return false;
    }
    payload->tessellationFactorBuffer = factorBuffer;
    payload->tessellationFactorBufferOffset = offsetBytes;
    payload->tessellationFactorInstanceStrideBytes = instanceStrideBytes;
    payload->tessellationFactorBufferBound = true;
    return true;
}

bool recordMetalRhiNativeSecondaryDraw(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::uint32_t vertexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics)
{
    if (vertexCount == 0u || instanceCount == 0u || !reserveCommand(payload, diagnostics)) {
        if (diagnostics.empty())
            diagnostics = "RHI-METAL-SECONDARY-DRAW-ARGUMENTS-INVALID";
        return false;
    }
    if (payload->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        if (!@available(macOS 10.13, iOS 11.0, *)) {
            diagnostics = "RHI-METAL-PATCH-LIST-API-UNAVAILABLE";
            return false;
        }
        if (!payload->tessellationFactorBufferBound || payload->patchControlPointCount == 0u
            || vertexCount % payload->patchControlPointCount != 0u
            || firstVertex % payload->patchControlPointCount != 0u) {
            diagnostics = "RHI-METAL-SECONDARY-PATCH-DRAW-REQUIRES-VALID-TESSELLATION-STATE";
            return false;
        }
        id<MTLIndirectRenderCommand> command =
            [nativeBuffer(payload) indirectRenderCommandAtIndex:payload->commandCount];
        if (command == nil) {
            diagnostics = "RHI-METAL-SECONDARY-ICB-COMMAND-MISSING";
            return false;
        }
        [command drawPatches:payload->patchControlPointCount
                        patchStart:firstVertex / payload->patchControlPointCount
                        patchCount:vertexCount / payload->patchControlPointCount
                  patchIndexBuffer:nil
            patchIndexBufferOffset:0u
                     instanceCount:instanceCount
                      baseInstance:firstInstance];
        payload->maxTessellationInstanceCount =
            std::max(payload->maxTessellationInstanceCount, instanceCount);
        ++payload->commandCount;
        return true;
    }
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    if (!tryNativePrimitiveType(payload->primitiveTopology, primitiveType)) {
        diagnostics = "RHI-METAL-PATCH-LIST-NO-EXECUTABLE-TESSELLATION-LOWERING";
        return false;
    }
    id<MTLIndirectRenderCommand> command =
        [nativeBuffer(payload) indirectRenderCommandAtIndex:payload->commandCount];
    if (command == nil) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-COMMAND-MISSING";
        return false;
    }
    [command drawPrimitives:primitiveType
                vertexStart:firstVertex
                vertexCount:vertexCount
              instanceCount:instanceCount
               baseInstance:firstInstance];
    ++payload->commandCount;
    return true;
}

bool recordMetalRhiNativeSecondaryDrawIndexed(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::uint32_t indexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstIndex,
    std::int32_t vertexOffset,
    std::uint32_t firstInstance,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& indexBuffer,
    rhi::IndexFormat indexFormat,
    std::uint64_t indexBufferOffset,
    std::pmr::string& diagnostics)
{
    if (indexCount == 0u || instanceCount == 0u || !indexBuffer
        || !indexBuffer->resource.nativeRuntimeBacked || !payload || !payload->nativeDevice
        || !indexBuffer->resource.nativeDevice
        || payload->nativeDevice.get() != indexBuffer->resource.nativeDevice.get()
        || !reserveCommand(payload, diagnostics)) {
        if (diagnostics.empty())
            diagnostics = "RHI-METAL-SECONDARY-INDEXED-DRAW-ARGUMENTS-INVALID";
        return false;
    }
    id<MTLBuffer> nativeIndexBuffer =
        reinterpret_cast<id<MTLBuffer>>(indexBuffer->resource.nativeObject.get());
    const auto indexSize = indexFormat == rhi::IndexFormat::Uint16 ? 2u : 4u;
    const auto firstIndexOffset = static_cast<std::uint64_t>(firstIndex) * indexSize;
    const auto indexBytes = static_cast<std::uint64_t>(indexCount) * indexSize;
    if (nativeIndexBuffer == nil
        || firstIndex > std::numeric_limits<std::uint64_t>::max() / indexSize
        || indexBufferOffset > std::numeric_limits<std::uint64_t>::max() - firstIndexOffset
        || indexBufferOffset + firstIndexOffset > nativeIndexBuffer.length
        || indexBytes > nativeIndexBuffer.length - (indexBufferOffset + firstIndexOffset)) {
        diagnostics = "RHI-METAL-SECONDARY-INDEXED-DRAW-RANGE-INVALID";
        return false;
    }
    if (payload->primitiveTopology == rhi::PrimitiveTopology::PatchList) {
        if (!@available(macOS 10.13, iOS 11.0, *)) {
            diagnostics = "RHI-METAL-PATCH-LIST-API-UNAVAILABLE";
            return false;
        }
        if (!payload->tessellationFactorBufferBound || payload->patchControlPointCount == 0u
            || vertexOffset != 0 || indexCount % payload->patchControlPointCount != 0u) {
            diagnostics = "RHI-METAL-SECONDARY-PATCH-DRAW-REQUIRES-VALID-TESSELLATION-STATE";
            return false;
        }
        id<MTLIndirectRenderCommand> command =
            [nativeBuffer(payload) indirectRenderCommandAtIndex:payload->commandCount];
        if (command == nil) {
            diagnostics = "RHI-METAL-SECONDARY-ICB-COMMAND-MISSING";
            return false;
        }
        const auto offset = indexBufferOffset + firstIndexOffset;
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
            diagnostics = "RHI-METAL-SECONDARY-INDEXED-DRAW-OFFSET-OVERFLOW";
            return false;
        }
        [command drawIndexedPatches:payload->patchControlPointCount
                               patchCount:indexCount / payload->patchControlPointCount
                         patchIndexBuffer:nil
                   patchIndexBufferOffset:0u
                  controlPointIndexBuffer:nativeIndexBuffer
            controlPointIndexBufferOffset:static_cast<NSUInteger>(offset)
                            instanceCount:instanceCount
                             baseInstance:firstInstance];
        payload->maxTessellationInstanceCount =
            std::max(payload->maxTessellationInstanceCount, instanceCount);
        payload->retainedIndexBuffers.push_back(indexBuffer);
        ++payload->commandCount;
        return true;
    }
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    if (!tryNativePrimitiveType(payload->primitiveTopology, primitiveType)) {
        diagnostics = "RHI-METAL-PATCH-LIST-NO-EXECUTABLE-TESSELLATION-LOWERING";
        return false;
    }
    id<MTLIndirectRenderCommand> command =
        [nativeBuffer(payload) indirectRenderCommandAtIndex:payload->commandCount];
    if (command == nil) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-COMMAND-MISSING";
        return false;
    }
    const auto offset = indexBufferOffset + firstIndexOffset;
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        diagnostics = "RHI-METAL-SECONDARY-INDEXED-DRAW-OFFSET-OVERFLOW";
        return false;
    }
    [command drawIndexedPrimitives:primitiveType
                        indexCount:indexCount
                         indexType:nativeIndexType(indexFormat)
                       indexBuffer:nativeIndexBuffer
                 indexBufferOffset:static_cast<NSUInteger>(offset)
                     instanceCount:instanceCount
                        baseVertex:vertexOffset
                      baseInstance:firstInstance];
    payload->retainedIndexBuffers.push_back(indexBuffer);
    ++payload->commandCount;
    return true;
}

bool executeMetalRhiNativeSecondary(const std::shared_ptr<void>& nativeRenderEncoder,
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.13, iOS 11.0, *)) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-API-UNAVAILABLE";
        return false;
    }
    if (!nativeRenderEncoder || !payload || !payload->nativeRuntimeBacked || !nativeBuffer(payload)
        || payload->commandCount == 0u) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-EXECUTION-PAYLOAD-MISSING";
        return false;
    }
    id<MTLRenderCommandEncoder> encoder =
        reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-SECONDARY-RENDER-ENCODER-MISSING";
        return false;
    }
    id<MTLDevice> encoderDevice = encoder.device;
    id<MTLDevice> payloadDevice =
        payload->nativeDevice ? reinterpret_cast<id<MTLDevice>>(payload->nativeDevice.get()) : nil;
    if (encoderDevice == nil || payloadDevice == nil || encoderDevice != payloadDevice) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-DEVICE-MISMATCH";
        return false;
    }
    [encoder executeCommandsInBuffer:nativeBuffer(payload)
                           withRange:NSMakeRange(0u, payload->commandCount)];
    return true;
}

bool executeMetalRhiNativeSecondaryCommand(const std::shared_ptr<void>& nativeRenderEncoder,
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::uint32_t commandIndex,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.13, iOS 11.0, *)) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-API-UNAVAILABLE";
        return false;
    }
    if (!nativeRenderEncoder || !payload || !payload->nativeRuntimeBacked || !nativeBuffer(payload)
        || commandIndex >= payload->commandCount) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-COMMAND-PAYLOAD-MISSING";
        return false;
    }
    id<MTLRenderCommandEncoder> encoder =
        reinterpret_cast<id<MTLRenderCommandEncoder>>(nativeRenderEncoder.get());
    if (encoder == nil) {
        diagnostics = "RHI-METAL-SECONDARY-RENDER-ENCODER-MISSING";
        return false;
    }
    id<MTLDevice> encoderDevice = encoder.device;
    id<MTLDevice> payloadDevice =
        payload->nativeDevice ? reinterpret_cast<id<MTLDevice>>(payload->nativeDevice.get()) : nil;
    if (encoderDevice == nil || payloadDevice == nil || encoderDevice != payloadDevice) {
        diagnostics = "RHI-METAL-SECONDARY-ICB-DEVICE-MISMATCH";
        return false;
    }
    [encoder executeCommandsInBuffer:nativeBuffer(payload) withRange:NSMakeRange(commandIndex, 1u)];
    return true;
}

bool metalRhiNativeSecondaryCommandBuffersAvailable()
{
    if (!@available(macOS 10.13, iOS 11.0, *))
        return false;
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil)
        return false;
    MTLIndirectCommandBufferDescriptor* descriptor =
        [[MTLIndirectCommandBufferDescriptor alloc] init];
    descriptor.commandTypes = MTLIndirectCommandTypeDraw | MTLIndirectCommandTypeDrawIndexed;
    descriptor.inheritPipelineState = YES;
    descriptor.inheritBuffers = YES;
    descriptor.maxVertexBufferBindCount = 1u;
    descriptor.maxFragmentBufferBindCount = 1u;
    descriptor.maxKernelBufferBindCount = 0u;
    id<MTLIndirectCommandBuffer> commandBuffer =
        [device newIndirectCommandBufferWithDescriptor:descriptor
                                       maxCommandCount:1u
                                               options:MTLResourceStorageModeShared];
    // Match the host-recorded allocation intent of the actual secondary path.
    const bool available =
        commandBuffer != nil && [commandBuffer indirectRenderCommandAtIndex:0u] != nil;
    [commandBuffer release];
    [descriptor release];
    return available;
}

} // namespace arp::rhi_metal
