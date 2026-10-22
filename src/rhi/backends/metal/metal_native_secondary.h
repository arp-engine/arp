#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_resources.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_pipeline.h"

#include <cstdint>
#include <memory>

namespace arp::rhi_metal {

struct MetalRhiNativeSecondaryCommandPayload {
    std::shared_ptr<void> nativeDevice;
    std::shared_ptr<void> nativeIndirectCommandBuffer;
    core::pmr::Vector<std::shared_ptr<MetalRhiNativeBufferPayload>> retainedIndexBuffers{
        &core::defaultCpuMemoryResource()};
    std::shared_ptr<MetalRhiNativeBufferPayload> tessellationFactorBuffer;
    std::uint64_t tessellationFactorBufferOffset = 0u;
    std::uint32_t tessellationFactorInstanceStrideBytes = 0u;
    std::uint32_t maxTessellationInstanceCount = 0u;
    bool tessellationFactorBufferBound = false;
    bool tessellationFactorBufferRebindRejected = false;
    std::uint32_t commandCount = 0;
    std::uint32_t maxCommandCount = 0;
    rhi::PrimitiveTopology primitiveTopology = rhi::PrimitiveTopology::TriangleList;
    std::uint32_t patchControlPointCount = 0u;
    bool cpuReplay = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    std::pmr::string diagnostics;
};

std::shared_ptr<MetalRhiNativeSecondaryCommandPayload> createMetalRhiNativeSecondaryCommandPayload(
    const std::pmr::string& label,
    rhi::PrimitiveTopology primitiveTopology,
    std::uint32_t maxCommandCount,
    bool allowCpuReplay,
    std::uint32_t patchControlPointCount);

bool recordMetalRhiNativeSecondaryDraw(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::uint32_t vertexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics);

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
    std::pmr::string& diagnostics);

bool recordMetalRhiNativeSecondaryTessellationFactorBufferBind(
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    const std::shared_ptr<MetalRhiNativeBufferPayload>& factorBuffer,
    std::uint64_t offsetBytes,
    std::uint32_t instanceStrideBytes,
    std::pmr::string& diagnostics);

bool executeMetalRhiNativeSecondary(const std::shared_ptr<void>& nativeRenderEncoder,
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::pmr::string& diagnostics);

bool executeMetalRhiNativeSecondaryCommand(const std::shared_ptr<void>& nativeRenderEncoder,
    const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& payload,
    std::uint32_t commandIndex,
    std::pmr::string& diagnostics);

bool metalRhiNativeSecondaryCommandBuffersAvailable();

} // namespace arp::rhi_metal
